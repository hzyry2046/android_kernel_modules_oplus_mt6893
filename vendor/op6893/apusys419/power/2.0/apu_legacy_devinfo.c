// SPDX-License-Identifier: GPL-2.0
/* Stock bootloader devinfo words, as consumed by the existing 6.6 provider. */
#include <linux/of.h>
#include <linux/errno.h>
#include <asm/unaligned.h>
#include "apu_legacy_devinfo.h"

static u32 devinfo_words[400];
static u32 devinfo_count;

int apu_legacy_devinfo_init(void)
{
	struct device_node *chosen;
	const void *tag;
	u32 count, i;
	int len;

	chosen = of_find_node_by_path("/chosen");
	if (!chosen)
		chosen = of_find_node_by_path("/chosen@0");
	if (!chosen)
		return -ENODEV;
	tag = of_get_property(chosen, "atag,devinfo", &len);
	if (!tag || len < sizeof(u32)) {
		of_node_put(chosen);
		return -EINVAL;
	}
	count = get_unaligned_le32(tag);
	/* Segment word 30, binning word 72, raising word 134 are mandatory. */
	if (count <= 134 || count > ARRAY_SIZE(devinfo_words) ||
	    count > (len - sizeof(u32)) / sizeof(u32)) {
		of_node_put(chosen);
		return -EINVAL;
	}
	for (i = 0; i < count; i++)
		devinfo_words[i] = get_unaligned_le32((const u8 *)tag + sizeof(u32) * (i + 1));
	devinfo_count = count;
	of_node_put(chosen);
	return 0;
}

u32 apu_legacy_devinfo_read(u32 index)
{
	if (WARN_ON_ONCE(index >= devinfo_count))
		return 0;
	return devinfo_words[index];
}
