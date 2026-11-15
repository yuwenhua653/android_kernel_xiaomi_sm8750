# Annibale board driver import

This is an initial Redmi K90 (annibale) source port onto the LineageOS SM8750
kernel. It is not a complete device kernel or a flashable release. The checks
below establish ARM64 object compilation, not successful module linking,
loading, Android boot, or hardware operation.

## Sources

- Base: `LineageOS/android_kernel_qcom_sm8750`, `lineage-22.2`,
  [`aefcb6e642bd691d8da6f577cadc794594e63e5c`](https://github.com/LineageOS/android_kernel_qcom_sm8750/commit/aefcb6e642bd691d8da6f577cadc794594e63e5c),
  Linux 6.6.144, ACK `android15-6.6`, KMI generation 8.
- Donor: `MiCode/Xiaomi_Kernel_OpenSource`, `annibale-w-oss`,
  [`f66f8c8bcbe38044fab61d44ea7f7c224f82b3ea`](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/commit/f66f8c8bcbe38044fab61d44ea7f7c224f82b3ea),
  Linux 6.6.57. Existing copyright and license notices are retained.
- Board evidence: `MiCode/kernel_devicetree`, `annibale-w-oss`,
  [`32fd4ddcedd46baa579643b3894c37c0fccccb91`](https://github.com/MiCode/kernel_devicetree/commit/32fd4ddcedd46baa579643b3894c37c0fccccb91),
  especially `qcom/annibale-sm8750.dtsi`, `qcom/sm8750-ir-sensor-common.dtsi`
  and `qcom/pmk8550.dtsi`.

| Feature | Donor path | Destination / configuration | Scope |
| --- | --- | --- | --- |
| Mi T1 GPIO | `drivers/gpio/gpio-mi-t1.c` | Same path; `CONFIG_MI_T1_CHIP=m` | Real GPIO, procfs and Qualcomm SSR driver. Annibale publishes three GPIOs and no clock. |
| Xiaomi IR | `drivers/media/rc/ir-spi.c` | `drivers/media/rc/xiaomi/ir-spi.c`; `CONFIG_IR_SPI_XIAOMI=m` | Raw SPI `/dev/ir_spi` interface. Generic LOS rc-core `ir-spi.c` remains available under its original option. |
| PMIC bark | Xiaomi additions in `drivers/input/misc/pm8941-pwrkey.c` | Minimal delta at the same path; `CONFIG_INPUT_PM8941_PWRKEY_XIAOMI=y` | Two extra bark compatibles, IRQ binding and existing PMIC wakeup handling. |
| AW21024 LED | `drivers/leds/leds-aw21024.c`, `leds-aw21024.h`, `leds-aw21024-reg.h` | Same paths; optional `CONFIG_LEDS_AW21024` | Source retained with whitespace normalization. Disabled for annibale: the published matching node is in `qcom/miro-leds-common.dtsi`, not the annibale board. |

## Porting changes and boundaries

The T1 import fixes GPIO-array bounds, GPIO acquisition before direction changes,
error/defer propagation, character-device allocation cleanup and current procfs
private-data access. Character-device publication is the last probe step, so a
later probe failure cannot free the state of an already opened descriptor.
Legacy ioctl user-copy errors now return `EFAULT`. Clock control tracks and
balances only the driver's own enable reference. Probe failure and removal clean
up procfs, SSR registration, wakeup state and acquired clock references.

T1 retains the vendor's single-instance board design. Manual unbinding is disabled;
dynamic DT removal and multiple matching devices are not supported by this port.
The annibale path is the numbered three-GPIO interface, not the legacy ioctl or
clock-equipped T2 variants. The driver requires `QCOM_RPROC_COMMON`, procfs, OF
and the common clock framework. Device-node and procfs permissions retain the
vendor ABI; Android policy integration and hardware access still need validation.

The Xiaomi IR implementation preserves the donor's unusual userspace ABI:
`LIRC_SET_SEND_MODE` sets a **byte count**, and writes transmit raw SPI data at
1.92 MHz with 32 bits per word. It is not the generic rc-core pulse interface.
Its allocation, user copies, transfer length limits, per-device locking and
open-file lifetime handling were rewritten: disconnect prevents further I/O,
and existing descriptors retain their state until close. The module basename
remains `ir-spi.ko`; the SPI driver name is `ir-spi-xiaomi`. Kconfig prevents
selecting both implementations for the same `ir-spi` DT compatible.

The PMIC option is disabled by default and enabled only in the annibale fragment.
It adds `qcom,pmk8350-pwrkey-bark` and `qcom,pmk8350-pwrkey-resin-bark`. Both sample
the KPDPWR status bit as the donor does. Bark IRQs return without generating a
second `KEY_POWER` input event. The import deliberately omits the donor's global
console-loglevel changes, task dumping, raw notifier export and display callback
export: their consumers have not been ported. This does **not** implement Xiaomi
display recovery or mtdoops integration. A future display port needs an explicit,
synchronized callback registration and teardown design.

The R4 batch adds a stock-K90-checked community HWID implementation and the
MiCode Wi-Fi SAR GPIO interface. P11 fingerprint is an explicit, default-off
experimental candidate. See [annibale-peripherals.md](annibale-peripherals.md)
for provenance, configuration, loader requirements and outstanding qualification.
R5 adds four default-off MCA common/transport candidates; see
[annibale-mca.md](annibale-mca.md). R6 adds the
[charging and fuel-gauge services](annibale-mca-services.md), also default-off.
Charging still needs the remaining MCA
policy, power-supply and HAL integration. No fabricated hardware identities
or dummy hardware implementations are supplied.

## Configuration and reproducible compile check

Apply `arch/arm64/configs/vendor/annibale_board.config` after `gki_defconfig` and
`arch/arm64/configs/vendor/sun_perf.config`. The fragment selects the board modules/options above and the Annibale haptics
controls described in [annibale-haptics.md](annibale-haptics.md), plus HWID and Wi-Fi
SAR from R4. AW21024 and the fingerprint candidate stay disabled. A full Android kernel build must
explicitly include this fragment and the matching annibale DT/module packaging.
Adding this file alone does not change the default sun build.

The object check uses Android clang **r510928**, real AOSP host build tools,
Bison data and host GCC. Tested tool checkouts were:

- `platform/prebuilts/clang/host/linux-x86` at
  `921f6da692b1ffc96a0daa7e741373f0089d40b0`, directory `clang-r510928`.
- `platform/prebuilts/build-tools` at
  `81f31e087778f0b89d7af788c739192dfc4cf500`.

From this repository, with absolute local paths:

```sh
export OUT_DIR=/path/to/out/annibale-board
export CLANG_BIN=/path/to/clang-prebuilts/clang-r510928/bin
export HOST_TOOLS_BIN=/path/to/build-tools/linux-x86/bin
export BISON_PKGDATADIR=/path/to/build-tools/common/bison
export JOBS=3
scripts/xiaomi/compile-annibale-board.sh all
```

If `bc` is absent from PATH but AOSP `gavinhoward-bc` exists in `HOST_TOOLS_BIN`,
the script creates a `bc` link under the dedicated output directory. It does not
install host tools or alter kernel source to bypass generated headers. The
`prepare` phase creates the configuration and runs `modules_prepare`; `objects`
uses an already prepared directory. Do not run `prepare` concurrently with other
builds sharing that output directory.

The following checks passed with clang r510928 on 2026-11-15:

- `gki_defconfig` + both fragments + `olddefconfig modules_prepare`.
- ARM64 module objects `drivers/gpio/gpio-mi-t1.o`,
  `drivers/media/rc/xiaomi/ir-spi.o`, and `drivers/input/misc/pm8941-pwrkey.o`.
- Optional ARM64 `drivers/leds/leds-aw21024.o` compiled with the make-time override
  `CONFIG_LEDS_AW21024=m`. This does not enable it in the saved configuration or
  establish annibale hardware support.
- Shell syntax and final staged-patch `git diff --cached --check`; independent
  source review of T1, IR and PMIC resource ownership and user-copy handling.
  The earlier unstaged/trailing-whitespace-only check missed mixed indentation
  and EOF blank lines in the new T1 file; these were fixed before this staged
  check, and the final T1 source was compiled again.

Logs are written under `OUT_DIR`: `configure.log`, `merge-config.log`,
`prepare.log`, `board-objects.log`, and `optional-led-object.log`. The existing
base kernel emits duplicate DT-target recipe warnings during these direct Make
checks. No driver compile error was observed. Full `Image`, module modpost/link,
KMI/ABI checks, boot image packaging and on-device validation remain outstanding.
