# Annibale MCA services (R6)

This batch adds four source components required between the Annibale fuel-gauge /
charging hardware and its higher-level policy: `mca_platform_fg_ic_ops`,
`mca_charge_interface`, `mca_event`, and `mca_sysfs`. They are real callback,
notification and sysfs implementations, but **do not yet read the BQ27Z561 chip,
provide Android battery percentage or implement charging policy**.

The board's published `annibale-charger-common.dtsi` names
`ti,bq27z561_master` at I2C address 0x55, with the master role. Its
`mca,charge_interface` node is already present and enabled. No board values or
wiring were changed. The similarly named MiCode `hl7603-regulator.c` matches
`halo,hl7603-msc06a`, not the Annibale charger node's `hl7603`; it was not selected
merely because the names resemble each other.

## Provenance and static stock comparison

The GPL community source reference is
[xiaomi-dada/android_kernel_xiaomi_sm8750 at a7a6375b](https://github.com/xiaomi-dada/android_kernel_xiaomi_sm8750/tree/a7a6375b13e490d187c2b348d961cec39a414ad2/drivers/power/supply/mca).
It describes reconstruction from Dada stock modules, **not MCA source released
by MiCode**. The fuel-gauge dispatch wrappers and interface headers derive from
that source. The sysfs, event and charge-control implementations are rewritten
with error propagation and removal paths. Original notices remain in imported
headers and dispatch code.

Static comparisons used K90 binaries in
[xiaomi-annibale's fixed stock snapshot](https://github.com/xiaomi-annibale/android_device_xiaomi_annibale-kernel/tree/df318c6b1c81e17d4fdc0fc9333c00c4d90e928a/modules/vendor_boot).
[annibale-mca-services-sources.json](annibale-mca-services-sources.json) records
source/binary hashes and the 66 checked fuel-gauge callback offsets. The 67-member
ARM64 callback table matches every reviewed stock dispatch function: all 66
function-pointer offsets agree, including the reserved capacity-level slot.
All 67 stock fuel-gauge exports are present; unregister is an additional export.

Stock charging registration recognizes 19 names, including `all`. The new source
retains those request names but refuses to register `all` as an actual provider.
Stock charging suspend writes notify chain 2 with event 17; the port retains that
pair. Stock event registration accepts chains 0..9. Other event enum names come
from the reconstruction and are not all individually validated against K90
consumers. Matching offsets and names do not prove calling conventions, CFI/KMI
compatibility or successful use with prebuilt modules. No proprietary binaries
are included in the commit.

## Components and contracts

| Component | Behavior |
| --- | --- |
| Fuel-gauge registry | Master/slave slots, copied callback tables, duplicate rejection, negative/out-of-range role and NULL argument checks, and unregister that waits for active dispatch. Absent integer operations return `EOPNOTSUPP`; legacy void getters leave caller data unchanged. |
| Charging interface | Named wired/wireless paths, strict command parsing, provider availability checks before writes, real callback errors, bounded reads, and explicit synchronous unregister. No missing operation is reported as successful hardware control. |
| Events | Native blocking notifier chains and bounded uevents through `xm_power/mca_event`. Invalid indices and strings fail; trigger writes propagate delivery errors. |
| Sysfs namespace | Five fixed `xm_power` devices plus owned dynamic groups, copied device names, held device references, link/file rollback, paired removal and full init-failure/module-exit cleanup. |

All are under `CONFIG_XIAOMI_MCA_SERVICES`, which requires the experimental MCA
core and sysfs. It is **off** in `annibale_board.config`, and enabled only by the
explicit `annibale_mca_test.config`. Source consumers must be rebuilt together
and use the new teardown functions. Do not mix these services with stock modules
or a second owner of `xm_power`.

Providers own their callback code and private data until unregister returns.
Stop their producers before unregistering. The fuel-gauge and charging registries
hold a read semaphore through each callback and a write semaphore while changing
registration. Callbacks may sleep but must not register/unregister themselves,
recursively invoke the same registry, or wait for another task that needs the
write lock. Legacy returned string pointers are borrowed from their provider;
clients need an independent provider lifetime guarantee while using them after
the dispatch returns. The void/unsigned-long legacy APIs retain their source
signatures; their limitations are not disguised as valid battery readings.

Sysfs group creation/removal APIs are for serialized probe/remove context, never
from the attributes being managed. Remove links/groups/files before freeing the
attribute tables or unloading their owner. A link-creation failure removes only
the newly created target group. File creation unwinds earlier successful files
and does not delete the failing file, which may belong to another client.
The supported class is `xm_power`; arbitrary additional classes are rejected.
The inherited `create_group` pointer API returns NULL on failure, while integer
APIs preserve negative errno.

Native notifier chains allow simultaneous notifications. Hardware consumers must
serialize their own state. A callback may forward to another chain, but must not
re-enter its own chain or change that chain's registrations. The previous global
notification mutex was not carried over because cross-chain forwarding could
self-deadlock. The event trigger retains mode 0220 and accepts a single trailing
newline. Entries are limited to 127 bytes, reject embedded NUL/CR/LF, and accept
`KEY=value` or the legacy bare alphanumeric/underscore token. The void reporting
API logs delivery errors; sysfs writes return them to the writer.

The historical `mca_debugfs_create_group` and stock-only
`mca_event_report_multiple_uevent` are **not provided**. There are no successful
stubs for them. A future consumer needing either must bring in a reviewed
implementation and its removal/buffer-lifetime contract first.

## Charging command behavior

The existing attribute names are preserved under the `charge_interface` link:
`input_suspend`, `charge_enable`, `iin_limit`, `ichg_limit`, `power_limit`,
`suspend_status`, and `shipmode_count_reset`. The last is the historical filename
for the ship-mode callback; it does not gain a new meaning here.

Ordinary writes use exactly `user path value`; ship mode uses a bare decimal
boolean and forwards the original `micharger`/`buck` identity. Commands must fit
255 bytes, with user/path/value tokens bounded to 63/31/127 characters. Numeric
conversion failures, overflow, embedded NUL and extra tokens fail before any
callback. Boolean charge-enable/ship values must be 0 or 1. Current-limit and
input-suspend strings must parse as signed integers; the actual provider owns
hardware range/policy checks. The dispatcher does not invent current or voltage
limits. `suspend_status` is now read-only (0440) rather than silently accepting a
write that did nothing.

`all` preflights every registered path for the requested operation and fails if
none exists or one lacks the method. Callback failures stop further dispatch and
are returned. An earlier successful operation cannot be rolled back generically;
read back state after an `all` error. A partially successful input-suspend write
still emits the battery-change notification. Positive nonzero callback statuses
are converted to `EIO` so they cannot look like a short successful sysfs write.

Reads start from an explicit byte count, check NUL termination, and propagate
provider errors. Missing ship-mode providers/methods return `ENODEV`/
`EOPNOTSUPP`, not a fabricated false status. No registered getters return
`ENODATA`; suspend aggregation validates `eff_client:<name> <0|1>` instead of
assuming any trailing character means a real suspended state.

## Validation and remaining work

Use the toolchain environment from [annibale-port.md](annibale-port.md):

```sh
bash scripts/xiaomi/compile-annibale-board.sh mca
python3 scripts/xiaomi/tests/test_annibale_mca_services.py
python3 scripts/xiaomi/tests/test_annibale_mca.py
# Restore default configuration afterwards:
bash scripts/xiaomi/compile-annibale-board.sh all
```

The MCA build phase now includes all four services and checks their configuration.
Four new host suites exercise actual production functions with UBSan: charging
parse/error/partial-all behavior, sysfs reference balancing and rollback,
uevent/chain validation, and gauge registration/lifetime. The last uses pthread
read/write locks and an intentionally blocked callback to verify that unregister
waits until the callback finishes. This tests the source locking contract with
host primitives, not Linux SMP/lockdep or real device removal.

On 2026-11-15, all four new services, the four R5 MCA components and the
PMIC Glink provider compiled as AArch64 ELF objects with Android clang r510928.
All four new service suites and the eight existing MCA/peripheral suites passed.
The default board object build also passed after restoring MCA services off and
QTI_BATTERY_CHARGER=m; the conflicting-client Kconfig check passed.

Full Image, module
modpost/link, KMI/CFI compatibility, live sysfs/uevent delivery, Android HAL,
real battery measurements, charging and suspend still need validation. The
existing duplicate DT-target Make warnings remain.

Next hardware work still includes the BQ27Z561 I2C implementation, battery
strategy/power-supply integration, voters, charger proxy and charge-pump policy.
The reference gauge driver also includes firmware updates, unsealing and
model-specific defaults; those cannot be enabled by this interface-only batch.
