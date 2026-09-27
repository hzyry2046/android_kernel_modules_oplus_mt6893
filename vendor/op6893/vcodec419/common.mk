# Shared flags: 4.19 compat shims first, then 6.6 device-modules headers.
DM := $(DEVICE_MODULES_PATH)
ccflags-y += -include $(PORT_ROOT)/compat/compat419.h
ccflags-y += -I$(PORT_ROOT)/compat/include -I$(PORT_ROOT)/include419
ccflags-y += -I$(DM)/drivers/misc/mediatek/qos -I$(DM)/include -I$(DM)/drivers/misc/mediatek/include/mt-plat \
	-I$(DM)/drivers/misc/mediatek/iommu -I$(DM)/drivers/dma-buf/heaps \
	-I$(DM)/drivers/misc/mediatek/cmdq -I$(DM)/drivers/misc/mediatek/cmdq/mailbox -I$(DM)/include/soc/mediatek
ccflags-y += -DCONFIG_VIDEO_MEDIATEK_VCU=1 -DCONFIG_MACH_MT6893=1
ccflags-y += -Wno-error=format
ccflags-y += -I$(DM)/drivers/misc/mediatek/ion_compat
