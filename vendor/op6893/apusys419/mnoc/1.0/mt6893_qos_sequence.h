/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT6893_QOS_SEQUENCE_H
#define MT6893_QOS_SEQUENCE_H
#include "mt6893_qos_snapshot.h"
#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

/* Diagnostic sequence observations, not firmware coherence certification.
 * num behaved as a 16-bit sequence on hardware; the firmware has no epoch,
 * seqlock or certified rate bound. The caller supplies an experiment epoch
 * and gap policy. A hidden reset or 65536-sample ABA can still alias these
 * markers; do not use this helper for runtime command QoS until that protocol
 * and command boundary/attribution have been established.
 */
struct mt6893_qos_cursor {
    mt6893_qos_u64 epoch, observed_ns;
    unsigned int idx, num, valid;
    struct mt6893_qos_stat anchor;
};

static inline int mt6893_qos_validate_sequence(
    const struct mt6893_qos_snapshot *snapshot, size_t bytes)
{
    unsigned int newest, distance, index;
    if (!snapshot || bytes != sizeof(*snapshot))
        return -EINVAL;
    if (snapshot->idx >= MT6893_QOS_SLOTS)
        return -ERANGE;
    newest = snapshot->stats[snapshot->idx].num;
    /* Requiring the full ring deliberately rejects startup/partial fill.
     * This checks sequence geometry; it does not certify payload publication.
     */
    for (distance = 1; distance < MT6893_QOS_SLOTS; distance++) {
        index = (snapshot->idx + MT6893_QOS_SLOTS - distance) % MT6893_QOS_SLOTS;
        if (snapshot->stats[index].num != ((newest - distance) & 0xffff))
            return -EPROTO;
    }
    return 0;
}

static inline int mt6893_qos_sequence_begin(
    const struct mt6893_qos_snapshot *snapshot, size_t bytes,
    mt6893_qos_u64 epoch, mt6893_qos_u64 now_ns,
    struct mt6893_qos_cursor *cursor)
{
    struct mt6893_qos_cursor candidate = {0};
    int ret;
    if (!cursor || !epoch)
        return -EINVAL;
    ret = mt6893_qos_validate_sequence(snapshot, bytes);
    if (ret)
        return ret;
    candidate.epoch = epoch;
    candidate.observed_ns = now_ns;
    candidate.idx = snapshot->idx;
    candidate.num = snapshot->stats[snapshot->idx].num;
    candidate.valid = 1;
    candidate.anchor = snapshot->stats[snapshot->idx];
    *cursor = candidate;
    return 0;
}

/* Consume samples strictly AFTER the saved marker. Unlike the legacy index
 * helper, an advance of exactly 64 is a full window, not an empty window.
 * Errors leave BOTH cursor and result unchanged; recovery is an explicit new
 * baseline, and cannot silently preserve an incomplete command total.
 * A duplicate snapshot does not refresh the last-progress timestamp.
 */
static inline int mt6893_qos_sequence_window(
    const struct mt6893_qos_snapshot *snapshot, size_t bytes,
    mt6893_qos_u64 epoch, mt6893_qos_u64 now_ns,
    mt6893_qos_u64 max_gap_ns, unsigned int engine,
    struct mt6893_qos_cursor *cursor, struct mt6893_qos_window *result)
{
    struct mt6893_qos_window window = {0};
    struct mt6893_qos_cursor candidate;
    unsigned int advance, num, i, index, value;
    int ret;
    if (!cursor || !result || !epoch || !max_gap_ns || cursor->valid != 1)
        return -EINVAL;
    if (engine >= MT6893_QOS_APU_ENGINES || cursor->idx >= MT6893_QOS_SLOTS ||
        cursor->num > 0xffff)
        return -ERANGE;
    if (cursor->epoch != epoch)
        return -ESTALE;
    if (now_ns < cursor->observed_ns)
        return -ERANGE;
    if (now_ns - cursor->observed_ns > max_gap_ns)
        return -ESTALE;
    ret = mt6893_qos_validate_sequence(snapshot, bytes);
    if (ret)
        return ret;
    num = snapshot->stats[snapshot->idx].num;
    advance = (num - cursor->num) & 0xffff;
    if (advance > MT6893_QOS_SLOTS)
        return -EOVERFLOW;
    if ((cursor->idx + advance) % MT6893_QOS_SLOTS != snapshot->idx)
        return -EPROTO;
    /* Until an entire lap replaces the anchor, its original marker must
     * remain in the ring. This check also documents the retention contract.
     */
    if (advance < MT6893_QOS_SLOTS && snapshot->stats[cursor->idx].num != cursor->num)
        return -EPROTO;
    /* Conservatively reject mutation of a retained published record. Its
     * firmware immutability contract has not been established; rejection is
     * diagnostic evidence, not a claim of a firmware protocol violation.
     */
    if (advance < MT6893_QOS_SLOTS &&
        memcmp(&snapshot->stats[cursor->idx], &cursor->anchor, sizeof(cursor->anchor)))
        return -EPROTO;
    for (i = 1; i <= advance; i++) {
        index = (cursor->idx + i) % MT6893_QOS_SLOTS;
        value = snapshot->stats[index].smibw_mon[MT6893_QOS_APU_FIRST + engine];
        window.total += value;
        if (value > window.peak)
            window.peak = value;
        window.count++;
    }
    window.next = (snapshot->idx + 1) % MT6893_QOS_SLOTS;
    candidate = *cursor;
    if (advance) {
        candidate.idx = snapshot->idx;
        candidate.num = num;
        candidate.observed_ns = now_ns;
        candidate.anchor = snapshot->stats[snapshot->idx];
    }
    *result = window;
    *cursor = candidate;
    return 0;
}
#endif
