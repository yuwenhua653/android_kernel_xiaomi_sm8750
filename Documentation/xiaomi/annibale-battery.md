# R9: standard battery measurement bridge

`mca_battery.ko` connects the R7 BQ27Z561 provider through the R6 MCA registry
to a standard read-only `battery` power supply. It binds the existing Annibale
`mca,business_battery` node. This is a limited new implementation, not a port of
all Xiaomi charging business logic and not a verified Android battery stack.

## Properties and error handling

| Property | Source | Standard ABI |
| --- | --- | --- |
| `capacity` | Master gauge raw SOC | Percent, 0–100; no UI smoothing |
| `voltage_now` | Gauge voltage | mV converted to µV with overflow checks |
| `current_now` | Gauge current | µA; negative discharge, positive charge |
| `temp` | Gauge temperature | Tenths of a degree Celsius |
| `charge_counter` | Gauge remaining capacity | µAh |
| `charge_full` | Gauge learned full capacity | µAh |
| `charge_full_design` | Gauge design capacity | µAh |
| `cycle_count` | Gauge cycle counter | Count |
| `status` | Charging state not integrated | `Unknown` |
| `health` | Pack/thermal policy not integrated | `Unknown` |

Read failures do not substitute plausible default values or a previously cached
measurement. `get_property` reports unavailable measurements to the power-supply
core, which skips unavailable fields when composing an event. One failing field
does not prevent other measurements from being read. Battery presence, pack
identity/authentication and writable charge controls are deliberately absent.
The normal power-supply core can expose its read-only temperature thermal zone;
this adds no thermal limits or charging policy.

No USB or wireless supply is required to read the gauge. Annibale's published
charger DT explicitly sets `support-wls = <0>`. The reference implementation's
requirement for both supplies would prevent this board from reporting readings.
The bridge does not infer charger presence or charge completion from current/SOC.

## Notifications and lifetime

A deferrable delayed work item on `system_freezable_wq` compares measurement
values and individual errors every 30 seconds. It sends `power_supply_changed`
on the initial sample, changes, errors/recovery, external-supply notifications
and resume. The comparison snapshot is used only to decide notification delivery;
property reads always reach the real provider. A deferrable timer does not wake
an otherwise idle CPU just to poll. The driver holds no persistent wake lock.

A mutex serializes property reads, polling and PM/removal transitions. Suspend
blocks new reads and synchronously cancels polling. Resume schedules an immediate
fresh update. Remove closes the read/requeue gate, cancels work, then unregisters
the supply. The registry's existing read/write lock protects each provider call
against provider removal. Missing gauge registration/readiness defers probe;
provider transport errors propagate. An existing `battery` supply prevents
registration. A module soft dependency requests the gauge before the bridge.

## Build and validation

The normal board fragment explicitly leaves `CONFIG_XIAOMI_MCA_BATTERY` unset.
The opt-in `annibale_mca_test.config` selects it as a module; Kconfig requires
BQ27Z561, power-supply support and `QTI_BATTERY_CHARGER=n`. Use the documented
`compile-annibale-board.sh qualify` flow, then rebuild the external modules
against the same output. Do not mix these files with stock module binaries.

R9 build hashes and exact scope are recorded in `annibale-r9-validation.json`.
The host suite executes production battery functions and the real MCA registry
with fake device/workqueue/provider boundaries, GCC warnings-as-errors and UBSan:

```sh
python3 -B -m unittest discover -s scripts/xiaomi/tests -v
```

The new fixture exercises conversions/polarity, SOC endpoints and overflow,
unchanged output on errors, probe deferral and rollback, missing individual
measurements, notification suppression/recovery, live reads, provider removal,
and concurrent reads versus suspend/removal. It checks cancellation before
unregister and no reads/requeue while suspended or stopped. These are host logic
tests, not emulation of Linux workqueue scheduling or proof of device PM behavior.

## Provenance and remaining integration

Reviewed reference: `xiaomi-dada/android_kernel_xiaomi_sm8750` commit
`a7a6375b13e490d187c2b348d961cec39a414ad2`, file
`drivers/power/supply/mca/mca_business/business_battery/mca_battery_psy.c`, Git blob
`5d7466dd4c023a6c333998e9c947d7a344e5ea29`. This is community reconstruction,
not newly released MiCode MCA source. The new bridge uses the existing tested
MCA callback contract; it does not import the reference's fabricated fallbacks,
wireless dependency, or writable callbacks without implemented controls.

Board evidence is the DT repository at
`c6dfe046b7cb97d7e895128ef05b701e1f9a50c9`:
`qcom/mca.dtsi` declares the node and `qcom/annibale-charger-common.dtsi` enables
it. Current polarity follows `Documentation/ABI/testing/sysfs-class-power` and
the R7 gauge contract; the earlier stock register/units evidence is preserved in
`annibale-gauge-sources.json`.

Still missing: complete wired charger and USB source reporting, charge-pump/
buck-boost drivers matched to Annibale's actual chips, fast-charge negotiation,
thermal/current limits, pack authentication, UI SOC/low-battery policy, and
Android Health/HAL integration. The reference SC8581 must not be substituted
for Annibale's SC8585 simply because their names are similar. Module packaging,
boot images, loading/probe, PM, battery readings and charging need hardware
validation before this experimental profile can replace the shipping stack.
