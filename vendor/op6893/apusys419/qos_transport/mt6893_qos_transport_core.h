/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT6893_QOS_TRANSPORT_CORE_H
#define MT6893_QOS_TRANSPORT_CORE_H
#include "../mnoc/1.0/mt6893_qos_snapshot.h"
#include "../mnoc/1.0/mt6893_qos_sequence.h"
#ifdef __KERNEL__
#include <linux/string.h>
typedef u32 mt6893_qos_u32;
#else
#include <string.h>
typedef uint32_t mt6893_qos_u32;
#endif
#define MT6893_QOS_WIRE_SLOTS 6
#define MT6893_QOS_ENABLE_COMMAND 0
#define MT6893_QOS_BOUND_COMMAND 11
struct mt6893_qos_wire {
    mt6893_qos_u32 command, words[MT6893_QOS_WIRE_SLOTS - 1];
};
_Static_assert(sizeof(struct mt6893_qos_wire) == 24, "SSPM QoS wire ABI");

/* Separate explicit opt-in for the stock global monitor initialization.
 * The reference has no inverse command: restore by reboot, never invent one.
 * No DVFSRC_ENABLE, stress, OPP, or clock commands are permitted.
 */
static inline int mt6893_qos_enable_message(struct mt6893_qos_wire *wire)
{
    struct mt6893_qos_wire request = {0};
    if (!wire)
        return -EINVAL;
    request.command = MT6893_QOS_ENABLE_COMMAND;
    *wire = request;
    return 0;
}
static inline int mt6893_qos_bound_message(unsigned int enable,
    struct mt6893_qos_wire *wire)
{
    struct mt6893_qos_wire request = {0};
    if (!wire || enable > 1)
        return -EINVAL;
    request.command = MT6893_QOS_BOUND_COMMAND;
    request.words[0] = enable;
    *wire = request;
    return 0;
}
struct mt6893_qos_capture_ops {
    void *context;
    int (*index)(void *context);
    int (*copy)(void *context, struct mt6893_qos_snapshot *snapshot);
};
/* Best-effort stable read of the real published firmware layout. Compare two
 * complete owned copies plus the publication index, with at most three tries.
 * This detects observed concurrent changes, not a firmware seqlock guarantee:
 * producer publish ordering and same-index/full-ring ABA remain unverified.
 * Never expose a partially copied result. Stability does not imply producer
 * progress; callers must check publication markers and their age separately.
 */
static inline int mt6893_qos_capture_impl(const struct mt6893_qos_capture_ops *ops,
    struct mt6893_qos_snapshot *first, struct mt6893_qos_snapshot *second,
    struct mt6893_qos_snapshot *output, unsigned int require_sequence)
{
    int before, after, ret, attempt;
    if (!ops || !ops->index || !ops->copy || !first || !second || !output ||
        first == second || output == first || output == second)
        return -EINVAL;
    for (attempt = 0; attempt < 3; attempt++) {
        before = ops->index(ops->context);
        if (before < 0)
            return before;
        if (before >= MT6893_QOS_SLOTS)
            return -ERANGE;
        ret = ops->copy(ops->context, first);
        if (ret)
            return ret;
        ret = ops->copy(ops->context, second);
        if (ret)
            return ret;
        after = ops->index(ops->context);
        if (after < 0)
            return after;
        if (after >= MT6893_QOS_SLOTS || first->idx >= MT6893_QOS_SLOTS ||
            second->idx >= MT6893_QOS_SLOTS)
            return -ERANGE;
        if (before == after && before == first->idx &&
            before == second->idx && !memcmp(first, second, sizeof(*first))) {
            /* Observed hardware can publish idx while that slot still has
             * the previous lap's num, even across two identical copies.
             * Reject this stable but incomplete ring with bounded retries.
             */
            if (require_sequence && mt6893_qos_validate_sequence(second, sizeof(*second)))
                continue;
            *output = *second;
            return 0;
        }
    }
    return -EAGAIN;
}
static inline int mt6893_qos_capture(const struct mt6893_qos_capture_ops *ops,
    struct mt6893_qos_snapshot *first, struct mt6893_qos_snapshot *second,
    struct mt6893_qos_snapshot *output)
{
    return mt6893_qos_capture_impl(ops, first, second, output, 0);
}
static inline int mt6893_qos_capture_sequence(const struct mt6893_qos_capture_ops *ops,
    struct mt6893_qos_snapshot *first, struct mt6893_qos_snapshot *second,
    struct mt6893_qos_snapshot *output)
{
    return mt6893_qos_capture_impl(ops, first, second, output, 1);
}
#endif
