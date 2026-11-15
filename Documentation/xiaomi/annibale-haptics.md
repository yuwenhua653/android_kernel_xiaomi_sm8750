# Annibale haptics HAL controls (R3)

This ports a subset of the Xiaomi haptics sysfs ABI onto the existing LOS
Qualcomm driver. It does not establish a working stock HAL or calibrated motor.

## Source and board selection

The reference is `drivers/input/misc/qcom-hv-haptics.c` in
[MiCode annibale-w-oss at f66f8c8bcbe38044fab61d44ea7f7c224f82b3ea](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/f66f8c8bcbe38044fab61d44ea7f7c224f82b3ea/drivers/input/misc/qcom-hv-haptics.c).
The donor file SHA256 is
`17537dd0ac26dd9bc4bc0957bed131bdfa4d8153b145cea62f0ef1ec9d39d6a6`.
The interface names, units and output formats follow this source. Error handling,
serialization and board gating are new integration code, not a verbatim import.

Enable `CONFIG_INPUT_QCOM_HV_HAPTICS_XIAOMI=y` alongside
`CONFIG_INPUT_QCOM_HV_HAPTICS=m`. The Annibale fragment does this. The matching DT
must also use `compatible = "xiaomi,annibale-haptics", "qcom,hv-haptics"` on
`pmih010x_haptics`. The fallback preserves ordinary Qualcomm binding when the
option is disabled. Other boards retain the generic sysfs group and formatting.
The binding schema and OF module aliases include the new compatible.

## Exposed ABI

All attributes are under `/sys/class/qcom-haptics/` (subject to probe success).
Permissions use standard `CLASS_ATTR_RW` / `CLASS_ATTR_RO`; Android ownership and
SELinux policy still require device integration.

| Attribute | Units / result | Behavior |
| --- | --- | --- |
| `mi_f0_ctrl` | Input frequency times 10; `1500` means 150 Hz | Programs open-loop period while idle; updates cached periods only after a successful register write. |
| `mi_vmax_ctrl` | mV; read `vmax: N` | Sets normal-effect voltage for the next upload. |
| `mi_custom_vmax_ctrl` | mV; read `custom vmax: N` | Sets custom FIFO voltage for the next upload. |
| `lra_calibration_check` | `0` / `1` | Reads the actual hardware `AUTO_RES_CAL_DONE_BIT`; not a board calibration pass/fail result. |

On this board only, `lra_frequency_hz` and `mi_f0_ctrl` reads preserve the donor's
quirk: the numeric value is frequency times 10, **despite the `Hz` suffix**.
Measured `lra_impedance` omits the generic `measured ` prefix, as in MiCode.

Zero frequency is rejected before division. Converted periods must fit the
5..20475 us hardware range, including after sleep-clock adjustment. These are
register limits, not a claim that all representable frequencies suit the motor.
Voltage must not exceed the detected hardware maximum; downstream driver voltage
clamps still apply. Zero voltage is accepted. No motor voltage/tuning limits have
been inferred from another phone.

Frequency writes hold the existing play mutex and a nonblocking atomic gate
shared with SPMI and SoundWire starts. Active playback, FIFO use, calibration or
SoundWire use returns `EBUSY`; no sleeping lock is added to FF playback callbacks.
PM acquisition and bus failures propagate to userspace. A failed write leaves
cached periods unchanged; this does not promise rollback of a partially completed
physical bus transaction. Strength changes are serialized with effect uploads.

Related fixes preserve `regmap_update_bits()` failures, reject zero calibration
divisors, validate DT period lower bounds and unwind registered class/input and
notifier resources on the affected probe error paths.

## Deliberately incomplete OEM behavior

The published Annibale board does not supply verified per-motor f0 min/max/default,
calibration retry counts or Xiaomi custom-voltage tuning. Consequently this port
keeps LOS calibration/timing and does not implement the donor's boot calibration
policy, `mi_f0_check`, vendor telemetry (`xm-haptic.h` / MIEV), period retuning or
its complete factory-calibration workflow. Do not treat the hardware completion
bit as evidence that those missing checks passed. No invented calibration result
or thresholds are supplied.

## Validation

On 2026-11-15:

- Android clang r510928 compiled `drivers/input/misc/qcom-hv-haptics.o` as ARM64
  with the Annibale option enabled. Existing duplicate DT target recipe warnings
  remain; there were no haptics compiler diagnostics.
- `python3 scripts/xiaomi/tests/test_annibale_haptics.py` passed 30 cases with
  undefined-behavior sanitization. The harness extracts actual driver functions
  and mocks bus/PM/kernel primitives: invalid input, hardware limits, PM/read/write
  failures, cache preservation, playback conflicts, gate ownership and voltage
  paths. Simulated start attempts during a register write are rejected. It is
  not a parallel locking stress test or hardware emulator. Probe teardown and
  the full calibration algorithm are not exercised by this host harness.
- All eight sun/sunp SoC + Annibale + camera + display DT chains merged. Against
  the prior validated trees the only changed property was the haptics compatible.
  Existing SoC overlay DTC warnings remain (99); no new warning was introduced.

Reproduce object checks using `scripts/xiaomi/compile-annibale-board.sh` and the
setup in [annibale-port.md](annibale-port.md). The script now includes haptics in
its required configuration and object targets. Host tests require a C compiler
with UBSan. Module linking, Module.symvers/KMI, stock HAL access, waveform quality,
suspend/resume and hardware operation remain untested. No flashable image is
produced by these checks.
