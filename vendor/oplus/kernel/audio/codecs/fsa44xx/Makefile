# FSA4480 object
ifeq ($(DEVICE_MODULES_PATH),)
    DEVICE_MODULES_PATH = $(srctree)
endif
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/typec/mux
ifdef CONFIG_OPLUS_PD_EXT_SUPPORT
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/power/oplus/pd_ext/inc
else
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/typec/tcpc/inc
endif
obj-$(CONFIG_SND_SOC_FSA) += fsa4480-i2c.o

# IS_ENABLED() in fsa4480-i2c.h must see the symbol or it emits the
# static-inline stubs which then collide with the real definitions here.
ccflags-y += -DCONFIG_SND_SOC_FSA_MODULE
