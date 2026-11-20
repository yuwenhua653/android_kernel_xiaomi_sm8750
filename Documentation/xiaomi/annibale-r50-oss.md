# R50: source coverage and MCA porting candidate

Target: Redmi K90 / annibale / SM8750, HyperOS OS4.0.6.0.XPKCNXM.
This is an **offline development profile**, not a complete OSS flash package.
The working R49 hybrid installation is not replaced by these build outputs.

## Sources and changes

- `drivers/xiaomi` and `include/mi_ubt`: exact initial import from
  MiCode/Xiaomi_Kernel_OpenSource `annibale-w-oss`,
  `f66f8c8bcbe38044fab61d44ea7f7c224f82b3ea`.
- The diagnostic integration repairs unbounded proc input, task/mm references,
  VMA lifetime, tracepoint teardown, failed initialization and boot-monitor
  worker ownership. `put_task_stack` has a real GPL export so the diagnostic
  module can release its reference through the kernel's existing implementation.
- `annibale_oss_coverage.config` also selects in-tree MTD and TLS modules.
  The test-only `mi_ubt_test` is compiled for coverage, not added to a boot list.
- `mca-oss` is a separate candidate imported from
  xiaomi-onyx-dev/android_kernel_xiaomi_sm8735,
  `09a5a2714470988d6a84f4b3677754af1479b037`. `ORIGIN.json` records original
  file hashes. Its common source derives from MiCode `dijun-v-oss`; its Qualcomm
  parts and later policy changes are community work. It is not an official
  Annibale charging release. The Annibale official MCA path is only a symlink
  into an unpublished vendor directory.
- The candidate uses the existing Annibale transport with checked framing,
  sequence matching, failure handling and ordered callbacks. Its callback
  types are adapted without pointer casts. Hardware identity is initialized
  from the real Annibale parameters, without another model's region override.
- HBM event identifiers and FSA liquid-detection event/register handling come
  from the official Annibale source. FSA I2C errors propagate to the caller,
  and referenced devices/nodes are released.
- Unused dummy one-wire functions are removed. No authentication success,
  firmware response, CRC override or missing kernel symbol is fabricated.

## Build

Use the pinned clang-r510928, AOSP build-tools, bison data and kernel-tools
listed in the modules repository's `configs/annibale-ci-pins.json`.
Set `OUT_DIR`, `CLANG_BIN`, `HOST_TOOLS_BIN`, `BISON_PKGDATADIR`,
`KERNEL_TOOLS_BIN`, and `JOBS`, then run:

```sh
bash scripts/xiaomi/compile-annibale-board.sh oss
# Separate output directory for the full charging candidate:
bash scripts/xiaomi/compile-annibale-board.sh oss-power
```

`oss` retains the earlier partial MCA profile. `oss-power` selects `mca-oss`
and disables both the partial MCA and standard QTI battery charger clients.
Both use normal strict modpost, MODVERSIONS and BTF. The selected MCA options
are checked after olddefconfig so dependencies cannot silently drop drivers.
Rebuild all external modules against the resulting kernel. Old R47/R49 modules
are not qualified by a successful new kernel build.

Host fault-injection checks exercise production transport and FSA functions:

```sh
python3 -B -m unittest discover -s scripts/xiaomi/tests -p test_annibale_mca.py
python3 -B -m unittest discover -s scripts/xiaomi/tests -p test_annibale_fsa.py
```

## Conditions still blocking a complete replacement

Compilation, internal CRC agreement and matching module names do not prove
compatibility with stock userspace or electrical behavior. In particular:

- Review the full MCA policy, ADC/voltage/current units, event enum values,
  calibration and gauge dataflash/firmware-update paths against Annibale.
  The public source includes write-capable gauge code; it has not been run.
- The 59 compared constant Qualcomm property IDs are only a subset of the
  transport contract. Payload layouts, notifications and error semantics
  still require review. These checks do not validate charging operation.
- Several upstream candidate provider registrations/removal paths need lifetime
  review; the sub-PMIC remove callback is incomplete. No unload/hotplug safety
  is certified for this candidate.
- `CONFIG_MIEV` is not implemented here: the upstream candidate's optional
  telemetry functions are inactive. This is an explicit functional gap.
- The source display stack does not yet provide end-to-end HBM notifications.
  Adding the official event definitions and a consumer does not provide them.
- Wireless IC drivers, SC8541, load-switch and MCA unit-test variants are not
  selected by this Annibale candidate. Existing DT declares no wireless charging.
  Do not silently erase their OEM inventory entries from coverage accounting.
- `sc8581` matches `sc8585_master`, but is not automatically equivalent to the
  complete OEM `sc8581_sy6541` family. Variant ownership must be reviewed.
- Remaining non-MCA OEM modules, DTB/DTBO integration, boot load lists, HAL and
  SELinux interfaces, AVB/images, and hardware stability remain separate gates.

No boot image packaging or new device test is authorized by a coverage count.
Complete source ownership and integration must be resolved before the next
flash package, as requested by the user.
