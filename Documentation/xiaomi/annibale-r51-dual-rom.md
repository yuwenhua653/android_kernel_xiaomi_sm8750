# R51: shared kernel paths for AOSP-derived ROMs and HyperOS

Target: Redmi K90 / annibale / SM8750, Linux 6.6; retained stock reference is
HyperOS OS4.0.6.0.XPKCNXM. This is an offline development build, not a flash
package or certification of either ROM. The working R49 installation is intact.

R35–R40's userspace input fixes and R42–R44's native integration remain valid.
They are not replaced by the stock module-name comparison. There is no new
claim that missing names are missing hardware functions.

## Shared source additions

The four candidates come from official MiCode piano-w-oss at
45fb9bd6ae5ba2942fc1d53e4b6b46ef76992f71. Original per-file hashes are in
annibale-r51-origin.json; the following porting fixes intentionally differ.

- MIEV retains /dev/miev, its five public cdev_tevent exports, event structure,
  event text prefix, poll behavior and empty-read result. The bounded 64 KiB
  record FIFO and ten 4 KiB producer slots do not require a userspace reader.
  An AOSP ROM without MiSight does not block kernel producers or accumulate
  unlimited events. Full FIFOs discard complete oldest records.
- Short reads return EMSGSIZE without consuming the record; copy faults retain
  it. Kernel event publication always uses the ordered workqueue, including
  process context that may hold a spinlock. Slot ownership and pending work
  are synchronized, module references cover allocated events, and all workers
  finish before buffers/device storage are freed. Initialization publishes
  readiness only after all resources exist; failed allocations return errors.
- JSON strings and keys are escaped, null/oversize fields are rejected, and
  the closing brace/NUL have reserved space. Serialization failures do not
  become successful truncated events. Callers must serialize access to each
  allocated event and destroy it once; use after destroy is not supported.
- The full MCA candidate now links the real MIEV provider. Invalid input and
  field/allocation failures are handled, rate-state access is synchronized,
  and counters cannot overflow. This replaces the previously inactive MCA
  telemetry path; it does not certify the charger policy or firmware paths.
- kshrink_slabd retains a bounded single outstanding request and a memcg
  reference across deferred reclaim. Failed/busy admission leaves synchronous
  reclaim active. No MIUI launcher/process-name bypass is used. CPU policy
  references are balanced, unavailable policies are handled, hook registration
  failure stops the worker, and unload synchronizes hooks and stops/drains it.
- mi_rmap_efficiency retains its existing sysfs paths and mapcount policy.
  Parameters use coherent single reads/writes and reject unsigned thresholds
  exceeding INT_MAX. Disabled operation preserves another hook's decision.
  Registration/sysfs failures unwind, and unload synchronizes callbacks.
- NTSYNC calls the already exported schedule_hrtimeout_range_clock directly;
  the source's debug_symbol lookup and potentially NULL indirect call are
  removed. No kernel symbol or timer result is fabricated.

All four are opt-in modules in the existing oss/oss-power qualification
fragment, using strict modpost, normal MODVERSIONS, CFI and BTF. No runtime
load list is silently expanded. The kernel configuration is shared by the two
ROM routes; Android init, policy, firmware and image integration remain route
specific. Do not mix the new full MCA modules with retained OEM MCA modules.

## Verification

Run production-function fault tests:

    python3 -B -m unittest discover -s scripts/xiaomi/tests -p test_annibale_r51.py

Five suites cover serialization/full buffers, no-reader FIFO behavior,
short/fault reads, queued-slot ownership, concurrent reclaim admission and
memcg lifetime, stop paths, rmap thresholds, real timer call selection,
interrupt/early wake, and MCA validation/rate-state/failure cleanup.
GCC ASan and UBSan are enabled. LeakSanitizer cannot attach in this ptraced
executor and is disabled; reference/buffer ownership has explicit assertions.
These are host tests with kernel API fixtures, not kernel runtime tests.

Use the R50 pinned toolchains and scripts/xiaomi/compile-annibale-board.sh
oss-power, then rebuild the complete external audio profile with
--experimental-sla against those exact outputs. Reuse caches only with the
builder's source/input checks. Final release and file hashes are recorded in
the R51 checkpoint after successful full linking.

The native device reference is xiaomi-annibale/android_device_xiaomi_annibale
at 1ed1eccfa3f68e1ec440b7f5e87ac97dc89475fe. The existing continuation script
reconstructs the R40 inputs, native fstab, init owners and network changes.
The source already has /dev/miev labeling and init permissions for miev and
ntsync; no permissive policy, broad allow rule or extra private daemon is added.
Local Blueprint/file-input checks do not substitute for Android compilation.

## Remaining gates

The remaining OEM source replacements, hardware variants, full MCA policy and
provider lifetimes, display HBM producer, final DT/partition load integration,
complete AOSP ROM/SELinux/VINTF build, and HyperOS runtime remain unresolved.
No two-ROM boot or charging/standby result is claimed for R51. No default/shared
remote branch is changed and no source or artifact is published to GitHub.
