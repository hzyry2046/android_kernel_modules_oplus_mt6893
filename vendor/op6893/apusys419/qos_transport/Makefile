# SPDX-License-Identifier: GPL-2.0
# Separate, default-off sampler. Never part of the normal boot module recipe.
obj-m += apusys_legacy_qos_transport.o
apusys_legacy_qos_transport-y := mt6893_qos_transport.o
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/sspm/v2
