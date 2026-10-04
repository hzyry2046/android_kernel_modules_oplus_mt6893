# SPDX-License-Identifier: GPL-2.0
ifeq ($(CONFIG_OPLUS_PD_EXT_SUPPORT),)
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/typec/tcpc/inc
else
ccflags-y += -I$(srctree)/drivers/power/oplus/pd_ext/inc
endif

# op6893 6.6 OTG bring-up: the VBUS path falls back to the charger device
# (charger_dev_enable_otg) when there is no vbus regulator, so the charger
# class header has to be reachable from here.
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/power/supply/

obj-$(CONFIG_EXTCON_MTK_USB) += extcon-mtk-usb.o
