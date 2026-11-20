# R52: full MCA sysfs ownership and charge-log teardown

Target: Redmi K90 / annibale / SM8750, Linux 6.6, shared source for AOSP-derived
ROMs and HyperOS OS4.0.6.0.XPKCNXM. Parent kernel is
`c63d00fe36281b941389a7a17004fe4f8a930b77` (R51).

This is a source checkpoint. **No R52 ARM64 kernel/module build or device test
has been performed.** R51's 474 + 94 module result remains historical evidence
for that exact R51 commit only. No new source-name coverage is claimed.

## Actual defects and changes

The full `mca-oss` candidate still carried an older implementation than the
separate reduced `mca` path. This change ports the reviewed link/file rollback
logic into the full candidate and fixes its class/device ownership directly.
It preserves the full candidate's named classes and existing xm_power paths.

- Recognize class/device ERR_PTR failures; propagate initialization errors.
  Roll back every default-device allocation or registration failure.
- Copy class/device names, pass the owned class name to class_create, and keep
  attribute-group arrays alive through device_unregister.
- Publish attributes with device_create_with_groups. Only unregister devices
  owned by the matching class/device/group entry, once. Do not put_device
  before device_unregister, and never dereference a missing class.
- Hold the registry mutex for list access and take a device reference for
  lookup consumers. Reserve names during device creation and publish the
  device only after registration succeeds. Create and unregister outside the
  mutex, including failure paths that can wait for sysfs callbacks. Final
  cleanup removes default devices, dynamic devices and classes.
- Roll back only files created by the current operation, preserving an
  existing file when creation returns EEXIST. Undo a newly created target group
  when link creation fails. Remove target attributes even if the parent has
  already disappeared. Reject negative attribute-array sizes.
- Charge-log registration validates negative/out-of-range IDs and NULL inputs.
  Duplicate owners cannot overwrite a live provider. A sleepable read/write
  semaphore covers callback access; owner-matched unregister waits for active
  callbacks. Callbacks must not register/unregister recursively.
- All six current logging provider source files now unregister on their normal
  teardown path, before releasing the logging provider's state. These are
  battery, business charger, thermal, connector monitor, BQ gauge and SC8581.
  This is a logging-lifetime fix, **not certification of all their other
  callbacks, workers, voters, interrupts or debugfs lifetimes**.
- Use FG_MASTER explicitly for gauge log identity; assign the slave charge
  pump its own log slot. Drain gauge sysfs access before destroying its mutexes.
- Select the correct optional head/context callback. Bound callback return
  lengths, tolerate errors, and reserve newline/NUL space in the sysfs result.
  Callbacks must still obey the buffer-size argument when writing.
- Parse numeric sysfs input with error handling. Capture the log-buffer index
  once per read; reject invalid indices before indexing. Bound non-NUL log
  buffer reads and terminate the returned buffer explicitly.

The existing public registration function signatures remain unchanged. Added
exports are `mca_log_charge_log_unregister` and `mca_sysfs_remove_files`.
No binary ABI/CRC result is claimed without rebuilding. No DT, module load
list, charging policy, protection threshold, SELinux rule or ROM-specific
daemon requirement was changed.

## Verification actually run

Four suites compile production C functions against host kernel-API fixtures:

1. Sysfs initialization failure at every allocation/default-device position,
   error pointers, copied names, duplicate/wrong-owner/repeated removal, held
   references and complete registry cleanup.
2. The existing link/file fault fixture against the full candidate: rollback,
   EEXIST ownership, absent parents and balanced device references.
3. Missing callbacks, invalid IDs, duplicate owners, negative/oversized return
   lengths, buffer sentinels and 32 concurrent callback/unregister cycles.
4. Malformed/out-of-range buffer indices, full non-NUL buffers, a simulated
   writer change after index capture, bounded reads and cache freeing.

Run from the kernel root:

    python -B -m unittest discover -s scripts/xiaomi/tests -p test_annibale_r52.py -v

This session used Windows MSVC 14.44 with `/std:c11 /W3 /WX /RTC1` and assertions,
with `MCA_TEST_SANITIZE=0` explicitly set. Four suites passed. MSVC AddressSanitizer
was first attempted, but its interceptor initialization failed before test
execution (`interception_win.cpp:193`). That log is retained. No ASan/UBSan pass
is claimed for R52; no interception-failure bypass was enabled. Linux defaults
to GCC/Clang ASan+UBSan but that route has not been run for R52.

The prior Linux environment is unavailable, and local WSL enumeration returned
`Wsl/E_ACCESSDENIED`. Required follow-up: rebuild oss-power and the full external
audio/SLA profile, run strict combined module verification and the host tests
on Linux, then continue integration. Do not flash this source checkpoint.

## Remaining work

The full fuel-gauge and other platform-operation registries still need their
own lifetime barriers. Borrowed gauge-name pointers, provider probe error
paths, debugfs lifetime, strategy/workqueue teardown, MCA hardware policies,
remaining OEM replacements, HBM production, final DT/partition integration,
full AOSP build/SELinux/VINTF and HyperOS runtime remain unqualified. In
particular, fixing the logging registry does not make BQ/SC8581 removal safe
through every other interface. Complete source replacement and integration
still precede the next requested device-test stage.
