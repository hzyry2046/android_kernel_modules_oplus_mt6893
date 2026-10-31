# SPDX-License-Identifier: GPL-2.0
# The native VPU engine (vpu/4.5) built standalone, on top of the legacy 2.0
# power provider, so the VPU firmware can be booted on 6.6 without the native
# apusys.ko (which owns the power 2.5 contract this board's DT does not have).
#
# Never selected by the boot build.  Build with:
#   scripts/build_apusys_legacy.sh vpu
#
# `vpu/` is a symlink to the native vpu/4.5 directory and `apu_bmap.c` to the
# native utility: kbuild cannot reference objects outside the module directory,
# and copying the sources would fork them.
#
# The 2.0 power headers MUST be searched before the native 2.5 ones:
# vpu_power.c's adu() is `VPU0 + id`, the native 2.5 enum puts VPU0 at 4 and
# 4.19 puts it at 0, so the wrong header would hand the provider domain 4/5/6.
obj-m += apusys_legacy_vpu.o

NATIVE_APU := $(DEVICE_MODULES_PATH)/drivers/misc/mediatek/apusys
LEGACY_APU := $(srctree)/$(src)/..

ccflags-y += -I$(LEGACY_APU)/power/2.0 -I$(LEGACY_APU)/power/2.0/mt6885
ccflags-y += -I$(LEGACY_APU)/midware/1.1
ccflags-y += -I$(NATIVE_APU)/include
ccflags-y += -I$(NATIVE_APU)/util
ccflags-y += -I$(NATIVE_APU)/debug
ccflags-y += -I$(NATIVE_APU)/debug/1.0/interface
ccflags-y += -I$(NATIVE_APU)/apusys_rv
ccflags-y += -I$(NATIVE_APU)/midware/2.0
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/mt-plat
ccflags-y += -I$(NATIVE_APU)/ipi
# vpu_events.h sets TRACE_INCLUDE_PATH to '.', so the directory holding it has
# to be on the include path for <trace/define_trace.h> to find it again.
ccflags-y += -I$(srctree)/$(src)/vpu
# struct apu_dev_power_data comes from the 2.5 header we deliberately do not use.
ccflags-y += -include $(srctree)/$(src)/vpu_compat.h

apusys_legacy_vpu-y := vpu_compat_stubs.o apu_bmap.o \
 vpu/vpu_main.o vpu/vpu_algo.o vpu/vpu_cmd.o vpu/vpu_hw.o vpu/vpu_mem.o \
 vpu/vpu_power.o vpu/vpu_cfg.o vpu/vpu_debug.o vpu/vpu_dump.o \
 vpu/vpu_tag.o vpu/vpu_met.o
