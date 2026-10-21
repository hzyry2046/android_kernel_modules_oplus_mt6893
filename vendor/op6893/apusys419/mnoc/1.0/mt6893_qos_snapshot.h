/* SPDX-License-Identifier: GPL-2.0 */
/* MT6893 SSPM firmware ABI. This is NOT the native 6.6 qos_bound ABI.
 * Input must be an owned snapshot acquired by a validated SSPM transport.
 * These helpers neither find/map firmware memory nor certify a live snapshot.
 */
#ifndef MT6893_QOS_SNAPSHOT_H
#define MT6893_QOS_SNAPSHOT_H
#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/stddef.h>
typedef u16 mt6893_qos_u16;
typedef u64 mt6893_qos_u64;
#else
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
typedef uint16_t mt6893_qos_u16;
typedef uint64_t mt6893_qos_u64;
#endif
#define MT6893_QOS_SLOTS 64
#define MT6893_QOS_EMI_CHANNELS 4
#define MT6893_QOS_EMI_MASTERS 5
#define MT6893_QOS_SMI_MASTERS 14
#define MT6893_QOS_LAT_MASTERS 9
#define MT6893_QOS_APU_FIRST 6
#define MT6893_QOS_APU_ENGINES 8

struct mt6893_qos_stat {
    mt6893_qos_u16 num, event;
    mt6893_qos_u16 emibw_mon[MT6893_QOS_EMI_MASTERS];
    mt6893_qos_u16 emibw_req[MT6893_QOS_EMI_MASTERS];
    mt6893_qos_u16 smibw_mon[MT6893_QOS_SMI_MASTERS];
    mt6893_qos_u16 smibw_req[MT6893_QOS_SMI_MASTERS];
    mt6893_qos_u16 lat_mon[MT6893_QOS_LAT_MASTERS];
};
struct mt6893_qos_snapshot {
    mt6893_qos_u16 idx, state;
    struct mt6893_qos_stat stats[MT6893_QOS_SLOTS];
};
_Static_assert(sizeof(struct mt6893_qos_stat) == 98, "MT6893 stat ABI");
_Static_assert(offsetof(struct mt6893_qos_stat, smibw_mon) == 24, "SMI monitor ABI");
_Static_assert(offsetof(struct mt6893_qos_stat, lat_mon) == 80, "latency ABI");
_Static_assert(offsetof(struct mt6893_qos_snapshot, stats) == 4, "ring ABI");
_Static_assert(sizeof(struct mt6893_qos_snapshot) == 6276, "SSPM buffer ABI");

struct mt6893_qos_window {
    mt6893_qos_u64 total;
    unsigned int peak, count, next;
};
/* sspm_sbuf_get() validates only the first offset (and accepts the end).
 * Validate the complete firmware object before obtaining/dereferencing it.
 * The region geometry must come from the actual SSPM provider, not guessed
 * addresses or the separate DRAM QOS_MEM_ID allocation.
 */
static inline int mt6893_qos_validate_span(mt6893_qos_u64 offset,
    mt6893_qos_u64 region_base, mt6893_qos_u64 region_bytes)
{
    if ((offset & 3) || offset < region_base ||
        region_bytes < sizeof(struct mt6893_qos_snapshot))
        return -ERANGE;
    if (region_bytes > (mt6893_qos_u64)-1 - region_base)
        return -ERANGE;
    if (offset - region_base > region_bytes - sizeof(struct mt6893_qos_snapshot))
        return -ERANGE;
    return 0;
}
/* Consume [next, snapshot.idx] once, matching the old command counter.
 * Transport must detect overrun/staleness before calling: an index alone
 * cannot distinguish a full producer lap from zero new samples.
 * Never chase the live producer index in the loop.
 */
static inline int mt6893_qos_read_window(
    const struct mt6893_qos_snapshot *snapshot, size_t bytes,
    unsigned int next, unsigned int engine, struct mt6893_qos_window *result)
{
    struct mt6893_qos_window window = {0};
    unsigned int stop, i, value;

    if (!snapshot || !result || bytes != sizeof(*snapshot))
        return -EINVAL;
    if (snapshot->idx >= MT6893_QOS_SLOTS || next >= MT6893_QOS_SLOTS ||
        engine >= MT6893_QOS_APU_ENGINES)
        return -ERANGE;
    stop = (snapshot->idx + 1) % MT6893_QOS_SLOTS;
    for (i = next; i != stop; i = (i + 1) % MT6893_QOS_SLOTS) {
        value = snapshot->stats[i].smibw_mon[MT6893_QOS_APU_FIRST + engine];
        window.total += value;
        if (value > window.peak)
            window.peak = value;
        window.count++;
    }
    window.next = stop;
    *result = window;
    return 0;
}
#endif
