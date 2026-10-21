// SPDX-License-Identifier: GPL-2.0
/* MT6893 SSPM sampler stage. No MNOC/engine registration or capability stubs. */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/pm_wakeup.h>
#include <linux/regulator/consumer.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/rpmsg.h>
#include <linux/rpmsg/mtk_rpmsg.h>
#include <linux/soc/mediatek/mtk_tinysys_ipi.h>
#include "sspm_define.h"
#include "sspm_helper.h"
#include "sspm_reservedmem.h"
#include "sspm_reservedmem_define.h"
#include "sspm_ipi_id.h"
#include "sspm_mbox_pin.h"
#include "mt6893_qos_transport_core.h"
#include "../mnoc/1.0/mt6893_qos_sequence.h"

static bool enable;
module_param(enable, bool, 0400);
MODULE_PARM_DESC(enable, "Opt in to sampler-only test with validated permanent highest DVFSRC floor");
static bool initialize_qos;
module_param(initialize_qos, bool, 0400);
MODULE_PARM_DESC(initialize_qos, "Send stock global QOS_ENABLE before bound sampling; reboot required after this experiment");
static DEFINE_MUTEX(transport_lock);
struct legacy_qos_transport {
    struct device *dev;
    struct platform_device *sspm;
    struct regulator *vcore;
    void __iomem *dvfsrc, *shared, *qos_sram;
    struct wakeup_source *wake;
    struct delayed_work poll;
    u32 *ack, *notify;
    unsigned int offset;
    unsigned int global_ack;
    bool tx_owned, rx_owned, attempted, active, poisoned, stopping;
    int error, stop_error;
    struct mt6893_qos_snapshot *first, *second, *latest;
    u64 captures, busy, failures;
    u64 changes;
    unsigned long captured_at, progress_at;
    struct mt6893_qos_cursor sequence;
    u64 sequence_epoch, sequence_steps, sequence_samples, sequence_empty;
    u64 sequence_overruns, sequence_stale, sequence_invalid, sequence_wraps;
    u64 sequence_max_gap_ns;
    unsigned int sequence_max_step;
    int sequence_error;
};
static struct legacy_qos_transport *sampler;

static int check_floor(struct legacy_qos_transport *q)
{
    int voltage, high;
    if (regulator_count_voltages(q->vcore) != 5)
        return -EINVAL;
    high = regulator_list_voltage(q->vcore, 4);
    voltage = regulator_get_voltage(q->vcore);
    if (high <= 0 || voltage != high)
        return voltage < 0 ? voltage : -ERANGE;
    /* Exact previously tested cold profile; never initialize or write it. */
    if (readl(q->dvfsrc + 0) != 0x66f8014b ||
        (readl(q->dvfsrc + 0xc) & 0x7070) != 0x7040 ||
        readl(q->dvfsrc + 0xd44) != 1 ||
        readl(q->dvfsrc + 0xd48) || readl(q->dvfsrc + 0xd6c))
        return -EAGAIN;
    return 0;
}
static int poison(struct legacy_qos_transport *q, int error)
{
    int ret;
    q->poisoned = true;
    q->error = error;
    q->failures++;
    if (q->tx_owned) {
        ret = mtk_ipi_quarantine_checked(&sspm_ipidev, IPIS_C_QOS, q->ack);
        if (ret)
            dev_err(q->dev, "channel quarantine failed=%d\n", ret);
    }
    return error;
}
static int request_bound(struct legacy_qos_transport *q, unsigned int on)
{
    struct mt6893_qos_wire request;
    unsigned int offset;
    int ret;
    if (q->poisoned)
        return q->error ? q->error : -EIO;
    ret = check_floor(q);
    if (ret)
        return poison(q, ret);
    ret = mt6893_qos_bound_message(on, &request);
    if (ret)
        return ret;
    *q->ack = 0;
    q->attempted = true;
    ret = mtk_ipi_send_compl_checked(&sspm_ipidev, IPIS_C_QOS,
        &request, MT6893_QOS_WIRE_SLOTS, 200, q->ack);
    if (ret)
        return poison(q, ret);
    offset = READ_ONCE(*q->ack);
    ret = mt6893_qos_validate_span(offset, SSPM_SHARE_REGION_BASE,
        SSPM_SHARE_REGION_SIZE);
    if (ret || (q->offset && q->offset != offset))
        return poison(q, ret ? ret : -EPROTO);
    q->offset = offset;
    q->active = on;
    return 0;
}
static int request_global_init(struct legacy_qos_transport *q)
{
    struct mt6893_qos_wire request;
    int ret = check_floor(q);
    if (ret)
        return poison(q, ret);
    ret = mt6893_qos_enable_message(&request);
    if (ret)
        return ret;
    *q->ack = 0;
    q->attempted = true;
    ret = mtk_ipi_send_compl_checked(&sspm_ipidev, IPIS_C_QOS,
        &request, MT6893_QOS_WIRE_SLOTS, 200, q->ack);
    if (ret)
        return poison(q, ret);
    q->global_ack = READ_ONCE(*q->ack);
    /* Stock helper accepts a nonzero ACK. Reject negative/zero replies,
     * without treating this ACK as proof the producer is progressing.
     */
    if (!q->global_ack || q->global_ack > INT_MAX)
        return poison(q, -EPROTO);
    dev_info(q->dev, "global QOS_ENABLE ack=%#x; reboot required to restore firmware state\n", q->global_ack);
    return 0;
}
static int shared_index(void *context)
{
    struct legacy_qos_transport *q = context;
    return readw(q->shared);
}
static int shared_copy(void *context, struct mt6893_qos_snapshot *out)
{
    struct legacy_qos_transport *q = context;
    memcpy_fromio(out, q->shared, sizeof(*out));
    return 0;
}
static void observe_sequence(struct legacy_qos_transport *q)
{
    struct mt6893_qos_window window;
    u64 now = ktime_get_ns(), gap;
    unsigned int previous_num = q->sequence.num;
    int ret;
    /* Diagnostic 50ms policy, NOT a verified firmware rate bound. No engine
     * accounting or command publication depends on these observations.
     */
    if (!q->sequence.valid) {
        ret = mt6893_qos_sequence_begin(q->latest, sizeof(*q->latest),
            q->sequence_epoch, now, &q->sequence);
        q->sequence_error = ret;
        if (ret)
            q->sequence_invalid++;
        return;
    }
    gap = now - q->sequence.observed_ns;
    if (gap > q->sequence_max_gap_ns)
        q->sequence_max_gap_ns = gap;
    ret = mt6893_qos_sequence_window(q->latest, sizeof(*q->latest),
        q->sequence_epoch, now, 50000000ULL, 0, &q->sequence, &window);
    q->sequence_error = ret;
    if (!ret) {
        if (!window.count) {
            q->sequence_empty++;
            return;
        }
        q->sequence_steps++;
        q->sequence_samples += window.count;
        if (window.count > q->sequence_max_step)
            q->sequence_max_step = window.count;
        if (q->sequence.num < previous_num)
            q->sequence_wraps++;
        return;
    }
    if (ret == -EOVERFLOW)
        q->sequence_overruns++;
    else if (ret == -ESTALE)
        q->sequence_stale++;
    else
        q->sequence_invalid++;
    /* Explicit diagnostic resync after a rejected interval. Never use this
     * to salvage a runtime command total or silently declare an epoch reset.
     */
    q->sequence.valid = 0;
    mt6893_qos_sequence_begin(q->latest, sizeof(*q->latest),
        q->sequence_epoch, now, &q->sequence);
}
static int capture(struct legacy_qos_transport *q)
{
    const struct mt6893_qos_capture_ops ops = {
        .context = q, .index = shared_index, .copy = shared_copy,
    };
    int ret;
    unsigned int previous_idx = q->latest->idx;
    unsigned int previous_num = q->latest->stats[previous_idx].num;
    if (!is_sspm_ready())
        return poison(q, -ENODEV);
    ret = check_floor(q);
    if (ret)
        return poison(q, ret);
    ret = mt6893_qos_capture_sequence(&ops, q->first, q->second, q->latest);
    if (ret == -EAGAIN) {
        q->busy++;
        return ret;
    }
    if (ret)
        return poison(q, ret);
    observe_sequence(q);
    if (q->captures && (q->latest->idx != previous_idx ||
        q->latest->stats[q->latest->idx].num != previous_num)) {
        q->changes++;
        q->progress_at = jiffies;
    }
    q->captures++;
    q->captured_at = jiffies;
    return 0;
}
static void poll(struct work_struct *work)
{
    struct legacy_qos_transport *q = container_of(to_delayed_work(work),
        struct legacy_qos_transport, poll);
    struct mtk_mbox_pin_recv *rx;
    int i;
    mutex_lock(&transport_lock);
    if (q->stopping || q->poisoned || !q->active)
        goto out;
    /* Notification payload is not used to certify coherence or progress.
     * Drain its completion with a bound; snapshot the actual shared SRAM.
     */
    rx = sspm_ipidev.table[IPIR_I_QOS].pin_recv;
    for (i = 0; i < 64 && try_wait_for_completion(&rx->notify); i++)
        ;
    capture(q);
    if (!q->poisoned)
        schedule_delayed_work(&q->poll, msecs_to_jiffies(16));
out:
    mutex_unlock(&transport_lock);
}
/* Stable-read telemetry only. Not used for command accounting until publish
 * ordering, full-ring loss and firmware sample cadence have been validated.
 */
int apu_legacy_qos_copy_snapshot(struct mt6893_qos_snapshot *output)
{
    struct legacy_qos_transport *q;
    int ret = 0;
    if (!output)
        return -EINVAL;
    mutex_lock(&transport_lock);
    q = sampler;
    if (!q || q->stopping || !q->active)
        ret = -ENODEV;
    else if (q->poisoned)
        ret = q->error;
    else if (!q->changes || time_after(jiffies, q->captured_at + msecs_to_jiffies(100)) ||
        time_after(jiffies, q->progress_at + msecs_to_jiffies(500)))
        ret = -ESTALE;
    else
        *output = *q->latest;
    mutex_unlock(&transport_lock);
    return ret;
}
EXPORT_SYMBOL_GPL(apu_legacy_qos_copy_snapshot);

static ssize_t status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct legacy_qos_transport *q = dev_get_drvdata(dev);
    unsigned int idx, i;
    ssize_t len;
    mutex_lock(&transport_lock);
    idx = q->latest->idx;
    len = sysfs_emit(buf, "active=%u poisoned=%u error=%d stop_error=%d offset=%#x captures=%llu busy=%llu failures=%llu changes=%llu idx=%u num=%u state=%u age_ms=%u\n",
        q->active, q->poisoned, q->error, q->stop_error, q->offset,
        q->captures, q->busy, q->failures, q->changes, idx,
        q->captures ? q->latest->stats[idx].num : 0,
        q->latest->state, q->captures ? jiffies_to_msecs(jiffies - q->captured_at) : 0);
    for (i = 0; i < MT6893_QOS_APU_ENGINES; i++)
        len += sysfs_emit_at(buf, len, "engine%u=%u%s", i,
            q->captures ? q->latest->stats[idx].smibw_mon[MT6893_QOS_APU_FIRST + i] : 0,
            i == MT6893_QOS_APU_ENGINES - 1 ? "\n" : " ");
    len += sysfs_emit_at(buf, len, "global_init=%u global_ack=%#x apu_clk=%#x bw_nord=%#x\n",
        initialize_qos, q->global_ack, readl(q->qos_sram + 0x48), readl(q->qos_sram + 0x4c));
    for (i = 0; i < MT6893_QOS_APU_FIRST; i++)
        len += sysfs_emit_at(buf, len, "smi%u=%u%s", i,
            q->captures ? q->latest->stats[idx].smibw_mon[i] : 0,
            i == MT6893_QOS_APU_FIRST - 1 ? "\n" : " ");
    for (i = 0; i < MT6893_QOS_EMI_MASTERS; i++)
        len += sysfs_emit_at(buf, len, "emi%u=%u%s", i,
            q->captures ? q->latest->stats[idx].emibw_mon[i] : 0,
            i == MT6893_QOS_EMI_MASTERS - 1 ? "\n" : " ");
    len += sysfs_emit_at(buf, len, "sequence_diagnostic=1 valid=%u last_error=%d steps=%llu samples=%llu empty=%llu overruns=%llu stale=%llu invalid=%llu wraps=%llu max_step=%u max_gap_us=%llu policy_gap_us=50000\n",
        q->sequence.valid, q->sequence_error, q->sequence_steps,
        q->sequence_samples, q->sequence_empty, q->sequence_overruns,
        q->sequence_stale, q->sequence_invalid, q->sequence_wraps,
        q->sequence_max_step, q->sequence_max_gap_ns / 1000);
    mutex_unlock(&transport_lock);
    return len;
}
static DEVICE_ATTR_RO(status);
static struct attribute *legacy_qos_attrs[] = { &dev_attr_status.attr, NULL };
ATTRIBUTE_GROUPS(legacy_qos);

static int channel_geometry(void)
{
    struct mtk_ipi_chan_table *tx, *rx;
    if (!is_sspm_ready() || !sspm_ipidev.ipi_inited || !sspm_ipidev.table ||
        !sspm_ipidev.mrpdev || sspm_ipidev.mrpdev->rpdev.src != SSPM_IPI_COUNT)
        return -ENODEV;
    tx = &sspm_ipidev.table[IPIS_C_QOS];
    rx = &sspm_ipidev.table[IPIR_I_QOS];
    if (!tx->pin_send || !tx->pin_recv || !rx->pin_recv ||
        tx->pin_send->msg_size != MT6893_QOS_WIRE_SLOTS ||
        tx->pin_recv->msg_size != 1 || tx->pin_recv->recv_opt != MBOX_RECV_ACK ||
        rx->pin_recv->msg_size != MT6893_QOS_WIRE_SLOTS ||
        rx->pin_recv->recv_opt != MBOX_RECV_MESSAGE)
        return -EPROTO;
    return 0;
}
static void release_channels(struct legacy_qos_transport *q)
{
    int ret;
    if (q->rx_owned) {
        ret = mtk_ipi_unregister_checked(&sspm_ipidev, IPIR_I_QOS, q->notify);
        if (ret)
            dev_err(q->dev, "channel detach failed=%d; retaining independent RX buffer until reboot\n", ret);
        if (!ret)
            q->rx_owned = false;
    }
    if (q->tx_owned) {
        ret = mtk_ipi_unregister_checked(&sspm_ipidev, IPIS_C_QOS, q->ack);
        if (ret)
            dev_err(q->dev, "channel detach failed=%d; retaining independent RX buffer until reboot\n", ret);
        if (!ret)
            q->tx_owned = false;
    }
    /* No client callback/private pointer is registered. If an unexpected
     * ownership failure prevents detaching, retaining its standalone buffer
     * avoids leaving firmware IRQs with freed devm/client memory.
     */
    if (!q->rx_owned) {
        kfree(q->notify);
        q->notify = NULL;
    }
    if (!q->tx_owned) {
        kfree(q->ack);
        q->ack = NULL;
    }
}
static int legacy_qos_probe(struct platform_device *pdev)
{
    struct legacy_qos_transport *q;
    struct device_node *sspm_node;
    struct resource sspm_res, *qos_res;
    int ret;
    if (!enable || !of_machine_is_compatible("mediatek,MT6893"))
        return -ENODEV;
    qos_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (!qos_res || qos_res->start != 0x11bb00 || resource_size(qos_res) != 0x100)
        return -EINVAL;
    ret = channel_geometry();
    if (ret)
        return ret;
    q = devm_kzalloc(&pdev->dev, sizeof(*q), GFP_KERNEL);
    if (!q)
        return -ENOMEM;
    q->dev = &pdev->dev;
    q->sequence_epoch = ktime_get_ns();
    q->qos_sram = devm_ioremap_resource(q->dev, qos_res);
    if (IS_ERR(q->qos_sram))
        return PTR_ERR(q->qos_sram);
    INIT_DELAYED_WORK(&q->poll, poll);
    q->first = devm_kzalloc(q->dev, sizeof(*q->first), GFP_KERNEL);
    q->second = devm_kzalloc(q->dev, sizeof(*q->second), GFP_KERNEL);
    q->latest = devm_kzalloc(q->dev, sizeof(*q->latest), GFP_KERNEL);
    if (!q->first || !q->second || !q->latest)
        return -ENOMEM;
    sspm_node = of_find_compatible_node(NULL, NULL, "mediatek,sspm");
    if (!sspm_node)
        return -ENODEV;
    ret = of_address_to_resource(sspm_node, 0, &sspm_res);
    if (!ret && (sspm_res.start != 0x10400000 || resource_size(&sspm_res) != 0x28000))
        ret = -EINVAL;
    q->sspm = !ret ? of_find_device_by_node(sspm_node) : NULL;
    of_node_put(sspm_node);
    if (ret)
        return ret;
    if (!q->sspm)
        return -ENODEV;
    q->vcore = regulator_get_optional(NULL, "dvfsrc-vcore");
    if (IS_ERR(q->vcore)) {
        ret = PTR_ERR(q->vcore);
        goto put_sspm;
    }
    q->dvfsrc = ioremap(0x10012000, 0x1000);
    if (!q->dvfsrc) {
        ret = -ENOMEM;
        goto put_regulator;
    }
    q->wake = wakeup_source_register(q->dev, "apusys-legacy-qos-experiment");
    if (!q->wake) {
        ret = -ENOMEM;
        goto unmap;
    }
    __pm_stay_awake(q->wake);
    platform_set_drvdata(pdev, q);
    mutex_lock(&transport_lock);
    if (sampler) {
        ret = -EBUSY;
        goto unlock;
    }
    ret = check_floor(q);
    if (ret)
        goto unlock;
    q->ack = kzalloc(sizeof(*q->ack), GFP_KERNEL);
    q->notify = kcalloc(MT6893_QOS_WIRE_SLOTS, sizeof(*q->notify), GFP_KERNEL);
    if (!q->ack || !q->notify) {
        ret = -ENOMEM;
        goto channels;
    }
    ret = mtk_ipi_register_checked(&sspm_ipidev, IPIS_C_QOS, NULL, NULL,
        q->ack, sizeof(*q->ack));
    if (ret)
        goto channels;
    q->tx_owned = true;
    ret = mtk_ipi_register_checked(&sspm_ipidev, IPIR_I_QOS, NULL, NULL,
        q->notify, MT6893_QOS_WIRE_SLOTS * sizeof(*q->notify));
    if (ret)
        goto channels;
    q->rx_owned = true;
    if (initialize_qos) {
        ret = request_global_init(q);
        if (ret)
            goto channels;
    }
    ret = request_bound(q, 1);
    if (ret)
        goto channels;
    q->shared = (void __iomem *)(uintptr_t)sspm_sbuf_get(q->offset);
    if (!q->shared) {
        ret = -ENODEV;
        goto stop;
    }
    ret = capture(q);
    /* Startup may not have a complete ring yet. Leave the consumer stale,
     * retain the real bound session, and let bounded background reads fill it.
     */
    if (ret && ret != -EAGAIN)
        goto stop;
    sampler = q;
    schedule_delayed_work(&q->poll, msecs_to_jiffies(16));
    mutex_unlock(&transport_lock);
    dev_info(q->dev, "sampler active offset=%#x idx=%u; no engine/QoS command accounting\n",
        q->offset, q->latest->idx);
    return 0;
stop:
    if (!q->poisoned)
        q->stop_error = request_bound(q, 0);
channels:
    release_channels(q);
    if (q->attempted)
        dev_err(q->dev, "sampler start failed=%d stop=%d; firmware restoration unverified, reboot before reuse\n", ret, q->stop_error);
unlock:
    mutex_unlock(&transport_lock);
    __pm_relax(q->wake);
    wakeup_source_unregister(q->wake);
unmap:
    iounmap(q->dvfsrc);
put_regulator:
    regulator_put(q->vcore);
put_sspm:
    put_device(&q->sspm->dev);
    return ret;
}
static int legacy_qos_remove(struct platform_device *pdev)
{
    struct legacy_qos_transport *q = platform_get_drvdata(pdev);
    mutex_lock(&transport_lock);
    q->stopping = true;
    sampler = NULL;
    mutex_unlock(&transport_lock);
    cancel_delayed_work_sync(&q->poll);
    mutex_lock(&transport_lock);
    q->stop_error = q->poisoned ? q->error : request_bound(q, 0);
    release_channels(q);
    mutex_unlock(&transport_lock);
    dev_info(q->dev, "sampler stopped status=%d; no firmware rollback certification\n", q->stop_error);
    if (initialize_qos)
        dev_info(q->dev, "global QoS state retained; reboot required before any other experiment\n");
    __pm_relax(q->wake);
    wakeup_source_unregister(q->wake);
    iounmap(q->dvfsrc);
    regulator_put(q->vcore);
    put_device(&q->sspm->dev);
    return 0;
}
static const struct of_device_id legacy_qos_match[] = {
    { .compatible = "mediatek,qos-2.0" }, { }
};
static struct platform_driver legacy_qos_driver = {
    .probe = legacy_qos_probe, .remove = legacy_qos_remove,
    .driver = { .name = "apusys-legacy-qos", .of_match_table = legacy_qos_match,
        .dev_groups = legacy_qos_groups, .suppress_bind_attrs = true },
};
static int __init legacy_qos_init(void)
{
    if (!enable)
        return -ENODEV;
    return platform_driver_register(&legacy_qos_driver);
}
static void __exit legacy_qos_exit(void)
{
    platform_driver_unregister(&legacy_qos_driver);
}
module_init(legacy_qos_init);
module_exit(legacy_qos_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT6893 checked SSPM sampler experiment, no engine capabilities");
