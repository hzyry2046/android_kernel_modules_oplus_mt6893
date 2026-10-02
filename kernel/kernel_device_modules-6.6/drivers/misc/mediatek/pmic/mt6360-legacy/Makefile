# op6893 6.6 bring-up: legacy MT6360 PMU MFD (4.19 v1/pmu port).
# Charger cell intentionally NOT built here: the 6.6
# drivers/power/supply/mt6360_pmu_chg.c is its descendant and already binds
# mediatek,mt6360_pmu_chg.  Same for adc/fled/rgbled (6.6 has equivalents).
# Only the MFD core (i2c + irq domain + subdev + core cell) is ported.
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/pmic/mt6360-legacy/inc

obj-$(CONFIG_MFD_MT6360_PMU_LEGACY) += mt6360_pmu_legacy.o
mt6360_pmu_legacy-objs := pmu/mt6360_pmu_i2c.o pmu/mt6360_pmu_irq.o \
			   pmu/mt6360_pmu_regmap.o \
			   pmu/mt6360_pmu_subdev.o pmu/mt6360_pmu_core.o
