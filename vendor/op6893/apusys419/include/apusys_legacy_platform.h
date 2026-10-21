/* SPDX-License-Identifier: GPL-2.0 */
#ifndef APUSYS_LEGACY_PLATFORM_H
#define APUSYS_LEGACY_PLATFORM_H
/* MT6893 stock defconfig uses a 34-bit IOMMU; old Reviser programs bank 3.
 * DMA addresses remain 64-bit internally. The vendor/engine ABI passes their
 * low 32 bits, with the upper two bits provided by Reviser boundary registers.
 * Do not key this hardware ABI on the removed MTK_IOMMU_PGTABLE_EXT Kconfig.
 */
#define APUSYS_LEGACY_IOVA_BANK 3U
#define APUSYS_LEGACY_IOVA_BASE (3ULL << 32)
#define APUSYS_LEGACY_IOVA_LIMIT (4ULL << 32)
#endif
