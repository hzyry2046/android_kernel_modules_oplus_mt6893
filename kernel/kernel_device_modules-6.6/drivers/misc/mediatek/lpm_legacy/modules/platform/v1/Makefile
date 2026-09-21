#source files
LPM_MODULE_PLAT_PLATFORM_OBJS += lpm_plat.o
LPM_MODULE_PLAT_PLATFORM_OBJS += lpm_plat_apmcu.o
# op6893 6.6 bring-up: the MCUSYS-off idle model.  lpm_legacy shipped the
# platform half and the debug half but not this one -- see idles/lpm_mcusys.c.
LPM_MODULE_PLAT_PLATFORM_OBJS += idles/lpm_mcusys.o
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/lpm_legacy/modules/platform/v1/idles/
# op6893 6.6 bring-up: the APMCU->MCUPM mailbox is built unconditionally now.
# It used to be MT6781-only, which meant mt6893 never wrote MCUPM_MCUSYS_CTRL
# and MCUSYS therefore never powered down -- see lpm_plat_apmcu_mbox.c.  Only
# the SSPM back-end inside it needs the sspm/v2 include path.
LPM_MODULE_PLAT_PLATFORM_OBJS += lpm_plat_apmcu_mbox.o
ifeq ($(CONFIG_MTK_LPM_MT6781),m)
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/sspm/v2/
endif

LPM_MODULES_FOLDER=$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/lpm_legacy/modules/


ccflags-y += -I$(LPM_MODULES_FOLDER)/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mcupm/include/
ccflags-y += -I$(LPM_MODULES_FOLDER)/platform/v1/

MTK_LPM_MODULE_PLATFORM_PLAT_NAME=v1
MTK_LPM_SUSPEND_VERSION = suspend_v1


ccflags-y += -I$(LPM_MODULES_FOLDER)/platform/$(MTK_LPM_PLATFORM_VERSION)/
ccflags-y += -I$(LPM_MODULES_FOLDER)/platform/$(MTK_LPM_SUSPEND_VERSION)/
ccflags-y += -I$(LPM_MODULES_FOLDER)/include/

include $(LPM_MODULES_FOLDER)/platform/$(MTK_LPM_SUSPEND_VERSION)/Makefile


LPM_MODULE_PLAT_PLATFORM = $(LPM_MODULE_PLAT_PLATFORM_OBJS:%.o=%.o)
LPM_MODULE_PLAT_PLATFORM += $(LPM_MODULE_PLAT_PLATFORM_SUSPEND_OBJS:%.o=../$(MTK_LPM_SUSPEND_VERSION)/%.o)


#Generic build
BUILD_MTK_LPM_PLAT_MODUDLE_NAME=mtk-lpm-plat-${MTK_LPM_MODULE_PLATFORM_PLAT_NAME}-legacy
obj-$(CONFIG_MTK_LPM_LEGACY) += ${BUILD_MTK_LPM_PLAT_MODUDLE_NAME}.o

${BUILD_MTK_LPM_PLAT_MODUDLE_NAME}-objs += $(LPM_MODULE_PLAT_PLATFORM)
