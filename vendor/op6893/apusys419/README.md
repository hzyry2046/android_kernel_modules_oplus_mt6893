# MT6893 legacy APUSYS forward port (offline experiment)

Reference: `android_kernel_oplus_mt6893`, HEAD
`e1db7235b89ae6128aebf4571d64e368330e97d8`.
`source-sha256.json` records the original bytes of each imported source/header.
Only middleware 1.1, its interface headers, Reviser 1.0/mt6885, and power 2.0/mt6885 sources are
currently included in the build. The old `mdw_mem_ion.c` is reference-only;
`mdw_mem_dmabuf.c` is the active 6.6 implementation.

This directory is outside the normal device-modules build. It is not in
`early-modules.list` or the boot/vendor build recipes. Native APUSYS must remain
disabled. The middleware, Reviser, and power modules each require explicit `enable=1`
for a future controlled test; the current dependency closure is incomplete.
The power module has only been loaded for a probe-denial test: its probe remains
blocked before hardware/resource initialization. Real power, Reviser and engine
initialization have not been validated.

From `mt6893-gki66`:

```sh
scripts/build_apusys_legacy.sh objects
scripts/audit_apusys_legacy_symbols.py --output logs/apusys-symbols.json
scripts/build_apusys_legacy.sh modules
scripts/build_apusys_legacy.sh providers
scripts/build_apusys_legacy.sh clocks
scripts/build_apusys_legacy.sh iommu
scripts/build_apusys_legacy.sh dvfsrc-modules
scripts/build_apusys_legacy.sh dvfsrc-regulators
```

`objects` builds the three ARM64 relocatable objects with the existing prepared
kernel configuration. `modules` runs strict modpost and currently fails on
missing MNOC QoS implementations. `providers` builds just power and Reviser
with strict modpost; both .ko files now build successfully. `clocks`/`iommu`
compile the related native objects for offline verification. The symbol audit also returns
nonzero for this incomplete closure. Never use warning-only modpost to treat
these objects as loadable modules.

The ioctl/task headers retain the old layout. `abi_layout.c` checks the ARM64
layout in the kernel build. `tools/apusys-probe` prints the userspace layout and
can later query handshake/capabilities, without submitting commands.

DMA-BUF mapping requires the stock `pseudo_m4u-vpu-data` client and its real APU
IOMMU domain. There is no MM-larb or physical-address fallback. Full DMA IOVAs
must be contiguous and in bank 3 (12–16 GiB); the legacy user/engine ABI carries
the low 32 bits. Reviser preserves boundary 3 from the stock 34-bit configuration.
Its VLM backing must map at exactly `0x304000000`; a different address fails,
with allocation/mapping resources released. This does not yet implement the APU
IOMMU power association, fixed-window allocation policy, or engine firmware map.

Before loading: validate the ported APU power owner on hardware, register the
clock providers/PG_VPU, adapt APU IOMMU2/3 power links and legacy domain encoding,
close MNOC symbol dependencies, and review the old driver's probe/remove,
thread and callback lifetime paths. Then validate DMA/cache/async lifetimes and
firmware/engine execution. Cache ownership transitions in the new backend need
validation with the actual DMA-BUF exporters and engine submission/completion
paths; compilation is not proof of coherency or network execution.

The 4.19 low-latency cgroupfs write hook has been removed as an optional tuning
path. Threads retain ordinary scheduling. Old protocol/task scheduling remains.
The known camera fallback is runtime property `oplus.camera.vd.sodenable=0`.

Second-batch details are in `mt6893-gki66/docs/apusys-power-port-progress-20261011.md`.
System VCORE votes require the DVFSRC virtual regulator; the stock APU
vcore-supply is the physical VGPU11 output, which is the actual system VCORE
rail on MT6893. Read-back uses VGPU11; direct writes still bypass shared voting.
The APU regulator voltage-control branch is used in place
of direct PCU writes. PG_VPU registration is guarded by the native clock
provider's `enable_apu` parameter, default false. Native IOMMU translation now
converts the three stock APU ports to their real domains, but APU IOMMU2/3 probe
remains disabled until power/PM integration is implemented and validated.

The native DVFSRC core now has an opt-in stock `mediatek,dvfsrc` handoff, guarded
by `enable_legacy_mt6893=0` by default. It only accepts known initialized legacy
firmware/controller state, uses the old read-only SMC status/type queries and
calibrated efuse/RSRV4 voltage table, and verifies actual VCORE voltage after
requests. It preserves the handoff vote as a floor. Cold initialization,
PWRAP programming, suspend/resume and real request validation are still pending.
Core/regulator modules build with strict modpost. A separately gated cold-start
mode now pins highest VCORE/DDR AP requests and disables DDR DFS. Its corrected
boot test completed initialization, 10/10 read-only state checks, two consumer
vote/release tests and five SOD-disabled camera reopen rounds. Dynamic DVFS,
DRAM DFS, failure recovery and suspend/resume remain unvalidated. An initial
test boot failed on a retained DPMAIF consumer CRC; rebuild that consumer and
check every actual packaged module, not just the replacement's imports.
See `mt6893-gki66/docs/apusys-dvfsrc-port-progress-20261011.md`.

Initial read-only device preflight validated firmware status 2/type 0 and
system VCORE 750000 uV (VGPU11 register and regulator readings agree). DVFSRC
is still disabled/uninitialized: BASIC_CONTROL=0x8, CURRENT_LEVEL=0. Handoff
was not attempted; the diagnostic module was unloaded after each run.
See `mt6893-gki66/docs/apusys-preflight-20261011.md`. Build the standalone
read-only diagnostic with `scripts/build_apusys_legacy.sh preflight`.

Fourth-batch implementation, cold1 failure/cold2 success and experiment limits:
`mt6893-gki66/docs/apusys-cold-port-progress-20261011.md`. The experimental
highest-floor boot is restored to the original after validation; normal build
recipes still do not opt in to legacy or cold-start parameters.

Fifth-batch QoS ABI and scheduler prerequisite checks:
`mt6893-gki66/docs/apusys-qos-port-progress-20261011.md`.
`mnoc/1.0/mt6893_qos_snapshot.h` is a checked firmware layout/window helper,
not a runtime provider. Native QoS has a different shared-buffer layout and
DT binding; enabling it also clears SRAM and sends initialization IPIs.
Host tests: `tools/apusys-probe/test_legacy_qos.sh`. The scheduler now rejects
Reviser/QoS prerequisite failures before engine submission, completes device
references, excludes negative bandwidth values, and preserves multicore errors.
Full strict modpost still requires real `apu_cmd_qos_start/end` implementations.

Sixth-batch checked SSPM sampler: `qos_transport/` builds separately with
`build_apusys_legacy.sh ipi-modules` then `qos-transport`; default `enable=0`.
It requires the verified cold highest-floor profile and only sends bound sampling
commands. On-device handshake returned SRAM offset 0x20850; two start/stop/unload
cycles completed, but the producer index/number did not advance. The real
consumer correctly rejected these snapshots with ESTALE. This is not a command
QoS provider and does not close `apu_cmd_qos_start/end`. The checked IPI/mbox
runtime was tested in a temporary boot with full actual-image CRC validation;
the original boot was restored afterward. See
`mt6893-gki66/docs/apusys-qos-transport-progress-20261012.md`.

Seventh-batch producer diagnosis: `initialize_qos=1` is a separate default-off
opt-in to stock command 0 before bound command 11. It never writes QoS SRAM,
APU_CLK, DVFS or OPP commands, and requires reboot afterward (the reference
protocol supplies no global disable). Fixes progress detection to compare the
new snapshot after capture. On-device A/B kept idx/num at zero without global
initialization; with it, ACK=1 and live EMI data appeared while APU_CLK stayed 0.
Ten actual fresh consumer calls and ten sequential 64-slot num dumps passed;
command accounting, firmware publish ordering/overrun and APU engine execution
remain unvalidated. `test_legacy_qos_transport.sh` now also runs actual capture,
freshness and global-init failure paths. See
`mt6893-gki66/docs/apusys-qos-producer-progress-20261012.md`.

Eighth-batch sequence diagnostics (`mnoc/1.0/mt6893_qos_sequence.h`) reject
observed overruns/expired or malformed intervals and correctly consume 64-slot
advances. They use explicit experiment epoch/time policy, retain old anchors,
and leave cursor/output unchanged on error. A 65536-sample ABA or firmware reset
can still alias these markers; this is not a command QoS provider. The transport
reports diagnostic counters only. Host tests exhaust 266240 windows plus actual
observer/sysfs paths; `test_legacy_qos.sh` runs both. Runtime sequence probe never
submits engine commands. See
`mt6893-gki66/docs/apusys-qos-sequence-progress-20261014.md`.

Ninth-batch power failure paths now stop on the first returned electrical error,
retain uncertain clock/rail references, and commit counters/logical state only
on success. RPC timeouts cannot submit/retry commands, clock batches/PLL phases
stop at the first error, invalid inputs are refused, and unfinished PM is
explicitly unsupported. The actual power probe is blocked before all resource
initialization even with enable=1 until a checked owner/IOMMU/PM lifecycle exists.
839 host fault/boundary cases and real blocked-probe consumer denial passed;
these are NOT actual APU power/execution validation. Run
`tools/apusys-probe/test_legacy_power.sh`; build `providers` then
`power-gate-probe`. See
`mt6893-gki66/docs/apusys-power-transitions-progress-20261014.md`.

Tenth-batch dynamic DVFS now preserves the first error, stops later stages,
and commits only successful voltage/frequency stages. IPUIF uses the checked
HAL path; request snapshots prevent consuming a newer request during an older
round. Worker/debug/status/write paths propagate failure without continuing
electrical work or printing failed telemetry as current state. 11002 unique
host cases pass Clang/GCC with sanitizers, including 1250 successful sequences
against the 4.19 reference on both real b0/b1 tables. Strict providers/consumer
and offline actual-ELF CRC checks pass. No modules were loaded or boot changed
in this batch; the user-requested sequence2 test boot is retained with SOD off.
The hard probe gate remains until checked owner/callback/client lifetime,
native PG deadlines and IOMMU access/save/restore are implemented. These are
software fault-path tests, not physical APU DVFS or engine validation. See
`mt6893-gki66/docs/apusys-power-dvfs-progress-20261014.md` and
`mt6893-gki66/docs/apusys-owner-lifecycle-audit-20261014.md`.

Eleventh-batch checked software ownership is implemented in
`power/2.0/apu_checked_power.{c,h}` with a production driver adapter. Opaque
clients/leases, module/device references, checked per-client callbacks,
side-effect-free veto, fault quarantine and stop/drain protect software
admission. Old user-only/void callback and raw debug paths are refused once
checked mode is selected; mode never falls back on detach. Readiness uses
release/acquire publication, and public metadata guards include core callback
faults as well as HAL faults. Reviser count/error paths now retain failed
references and block retries; its probe is also hard-gated before resources.
11441 unique host scenarios plus separately counted pthread workloads pass;
strict providers/consumer and 263 actual-ELF CRC checks pass. A real consumer
tested only unready refusal on the retained sequence2 boot and was unloaded.
No owner or APU resources were created on the device. OFF is a software
no-lease state, not verified physical rail/clock-off. Bootstrap resource
ledgers, PG/PM/IOMMU and Reviser/MNOC/engine lifetime migration remain required
before real power tests. See
`mt6893-gki66/docs/apusys-checked-owner-progress-20261014.md`.
