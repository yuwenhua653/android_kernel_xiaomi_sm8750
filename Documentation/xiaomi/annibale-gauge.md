# Annibale R7: BQ27Z561 read-only provider

R7 connects an actual I2C read path to the R6 MCA fuel-gauge registry. It is
experimental and default-off. It does not register an Android `battery`
power_supply or enable a charging strategy. No hardware result is claimed.

## Source and device scope

This is a small new provider using the standard-register subset documented in
the community reconstruction
[`xiaomi-dada/android_kernel_xiaomi_sm8750`](https://github.com/xiaomi-dada/android_kernel_xiaomi_sm8750/tree/a7a6375b13e490d187c2b348d961cec39a414ad2/drivers/power/supply/mca/mca_hardware_ic/fuelgauge_ic/bq27z561).
That reference is **not a MiCode release**. The register table and selected unit
conversions were cross-checked against the Annibale stock `bq27z561.ko` at
`xiaomi-annibale/android_device_xiaomi_annibale-kernel` commit
`df318c6b1c81e17d4fdc0fc9333c00c4d90e928a`. See the accompanying source manifest.
No stock binary, gauge firmware or authentication material is included.

The existing Annibale DT (`c6dfe046b7cb97d7e895128ef05b701e1f9a50c9`,
`qcom/annibale-charger-common.dtsi`) already defines the master at `0x55` with
`ic_role = <0>` and compatible `ti,bq27z561_master`. No DT change is needed.
Probe rejects missing/wrong HWID, a different address, ten-bit addressing,
missing/wrong role or lack of SMBus read-word support. Voltage and SOC must be
read successfully before publication. A response is not silicon identity or
pack authentication: alternate pack/gauge variants remain unverified.

## Implemented reads

| MCA getter | Register | Result |
| --- | --- | --- |
| `get_volt` | `0x08` | mV |
| `get_curr` | `0x0c` | signed current, µA, discharge-positive |
| `get_temp` | `0x06` | 0.1 °C, raw minus 2730 |
| `get_original_temp` | `0x1e` | internal sensor, 0.1 °C |
| `get_rsoc` | `0x2c` | raw integer % |
| `get_rm` | `0x10` | remaining charge, µAh |
| `get_fcc` | `0x12` | learned full charge, µAh |
| `get_full_design` | `0x3c` | gauge design charge, µAh |
| `get_cyclecount` | `0x2a` | cycles |
| `get_soh` | `0x2e` | raw integer % |
| `get_chip_ok`, `probe_ok` | `0x0a` | successful live flags read only |

The first ten also have read-only `telemetry/` attributes on the I2C device,
with units in their names; see `Documentation/ABI/testing/sysfs-driver-annibale-fg`.
The current sign is deliberately the MCA convention; future power_supply
integration must convert it. Each attribute is a separate sample, not a
multi-register atomic reading. Chip/probe availability does not assert battery
health, authentication, firmware identity, or charger readiness.

Zero remaining charge/SOC/current is valid. Zero voltage/full/design capacity
and values outside the reviewed bounds return `-ERANGE`, not stale data or
defaults. The signed-current conversion also handles the `0x8000` boundary
before rejecting it as outside the supported range. Bus errno is preserved.
No values are written to caller outputs on failed reads.

## Lifetime and power

One mutex covers each transaction, suspend/resume and the stopping flag. Suspend
waits for a running read then blocks new reads with `-EHOSTDOWN`. Resume permits
fresh reads. Removal marks the provider stopped, releases the mutex, unregisters
from MCA (draining in-flight callbacks), removes sysfs (draining readers), then
allows devres to release memory. Sysfs creation failure unregisters MCA; a busy
registry leaves the existing provider alone. There are no polling timers,
background workers, wake locks or unsolicited bus traffic after probe.

No gauge writes, MAC transactions, unseal keys, calibration, firmware updates,
fake temperature/SOC, charging status heuristics, SOC smoothing or authentication
callbacks are provided. Unsupported MCA operations remain NULL and the R6
registry returns its existing errors. Stock policy modules must not be mixed
with this incomplete provider. Do not load the stock gauge driver alongside it.

## Build and validation

`CONFIG_XIAOMI_MCA_BQ27Z561` depends on MCA services and I2C. The normal board
fragment keeps it disabled. `annibale_mca_test.config` explicitly enables it
and the existing `compile-annibale-board.sh mca` phase checks its selection.
Use the toolchain/environment recipe in `annibale-port.md`.

Validated with AOSP clang-r510928 from prebuilts commit
`921f6da692b1ffc96a0daa7e741373f0089d40b0` and build-tools
`81f31e087778f0b89d7af788c739192dfc4cf500`:

- ARM64 compilation of the new provider, all eight prior MCA objects and the
  existing board objects; new provider confirmed as AArch64 ELF.
- `python3 -m unittest discover -s scripts/xiaomi/tests -p 'test_annibale*.py'`:
  14 suites passed. The new test compiles the production gauge read, probe, PM,
  removal and real MCA registry functions with UBSan and a mocked SMBus. It
  covers conversion bounds, all implemented dispatch mappings, errno/output
  preservation, invalid board/DT/capability, allocation and publication failure,
  duplicate registration, suspend/resume and removal during a blocked bus read.
- Driver checkpatch: zero warnings/errors. Default board build is separately
  checked with the experimental gauge/MCA options disabled.

These are object and host tests, not full Image/module modpost, signed module
loading, actual bus transactions or on-device validation. Still missing:
complete battery power_supply/UI reporting, pack/vendor identification and
authentication integration, charging IC providers and validated charging/thermal
policy. Leave the normal boot configuration unchanged until those paths are ready.
