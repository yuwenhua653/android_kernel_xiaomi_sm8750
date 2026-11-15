# Annibale peripheral batch (R4)

This batch adds the HWID dependency, Xiaomi Wi-Fi SAR GPIO driver and an
experimental P11 fingerprint candidate to the LOS SM8750 kernel. These are
source and object-build results, not a working-device or flashable release.

| Component | Board fragment | Build output | Qualification |
| --- | --- | --- | --- |
| HWID | `CONFIG_MI_HARDWARE_ID=m` | `hwid.o` | Stock K90 interface/layout checked; loader parameter forwarding untested. |
| Wi-Fi SAR GPIO | `CONFIG_MI_WIFI_GPIO=m` | `xiaomi_wifi_gpio.o` | GPIO/netlink/debounce port; radio, firmware and regulatory behavior untested. |
| P11 fingerprint | **Disabled** | Four objects combined into `mi_fp.o` by the explicit test configuration | Supply, IPC, netlink and input glue compile; HAL/TEE, lifecycle stress and suspend validation pending. |

## Provenance and identity

HWID is the GPL community reconstruction by Iscle / xiaomi-dada at
[`b269d3a5d576fbf3e8899e1189ce8d2d62ba6528`](https://github.com/xiaomi-dada/android_kernel_xiaomi_sm8750/commit/b269d3a5d576fbf3e8899e1189ce8d2d62ba6528),
not newly released Xiaomi source. The Wi-Fi and fingerprint interfaces come from
MiCode `annibale-w-oss` at
[`f66f8c8bcbe38044fab61d44ea7f7c224f82b3ea`](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/commit/f66f8c8bcbe38044fab61d44ea7f7c224f82b3ea).
Original source hashes and integration hashes are recorded in
[annibale-peripherals-sources.json](annibale-peripherals-sources.json).

The K90 stock reference is `modules/vendor_boot/hwid.ko` and `mi_fp.ko` in
[xiaomi-annibale's prebuilt repository at df318c6b](https://github.com/xiaomi-annibale/android_device_xiaomi_annibale-kernel/tree/df318c6b1c81e17d4fdc0fc9333c00c4d90e928a).
Static ELF/disassembly checks confirmed all nine HWID exports, four uint
parameter types, the 11-entry sun product table and the accessor bit layout.
For SoC 618, project 11 maps to annibale. Stock fingerprint disassembly also
confirms project 11 / revision P01 chooses `fp_3v1_vreg`; other reviewed P11
revisions choose `fp_3v3_vreg`. Binary hashes and precise bit expressions are in
the source manifest. The proprietary binaries are **not** redistributed here.
This establishes source-selection evidence, not KMI CRC compatibility.

The original nine HWID accessors retain their export class and zero-default
parameter behavior. A new GPL readiness accessor requires both `project` and
`hwid_value` to have actually been supplied and the product mapping to be known.
A supplied zero identity is valid; an omitted identity is not treated as China.
There are no disabled-config fake HWID accessors. The new consumers depend on
the real module and reject an incomplete identity.

Integration must forward the device's actual bootloader parameters into the
module loader: `hwid.project`, `hwid.hwid_value`, `hwid.project_adc`, and
`hwid.build_adc` (module argument names omit the `hwid.` prefix). Do not hardcode
project 11 or guess a revision/region. Build these modules together and regenerate
module dependencies; mixing this Wi-Fi/fingerprint port with an older stock
`hwid.ko` will lack `xiaomi_hwid_is_ready`. This batch does not modify the ROM's
loader or boot ramdisk.

## Wi-Fi SAR GPIO

The MiCode ABI remains `wifi_sar`, version 1; command 1 (`WPAS_READY`) publishes
command 2 (`GPIO_STATUS`), attribute 1 (u32), to `sar_event`. The admin permission
and multicast response behavior remain. This reports the **raw GPIO level**;
it does not choose transmit power limits or implement a WLAN radio driver.

Only a supplied Chinese HWID may initialize it. Annibale's published DT already
provides GPIO 79 and 30 ms debounce, so no DT values were changed. Probe now
propagates GPIO direction, IRQ, wake-IRQ and deferred-probe errors. It checks
malformed debounce values and limits them to 1000 ms (default 50 ms). The global
ABI permits only one bound device. Netlink requests before probe or after remove
return `ENODEV` instead of reading GPIO zero. GPIO read failures are not emitted
as false SAR states. An empty multicast listener set is normal.

A mutex protects netlink publication and device lifetime. Removal stops the IRQ
producer before cancelling work. A generation/pending-work check keeps an older
worker from dropping the wake reference of a newer edge. Failure paths release
wakeup resources. This has host scheduling fault-injection coverage, not proof
under real interrupt load, suspend or the stock supplicant.

## Fingerprint candidate

Only the P11/TIC-side configuration published for Annibale is exposed by Kconfig.
It uses the existing `xiaomi,xiaomi-fp` node, validated HWID, GPIO 85, real regulator
supplies and the kernel's panel notifier. A fingerprint notifier client is
appended without renumbering existing client IDs. No display stub is supplied.

Integration corrections include:

- Real regulator acquisition and deferred-probe errors; one enable reference
  through the donor's P01 3.1-to-3.3 V transition, with errors and rollback.
- Three allocated character-device minors, error propagation, input-device
  unregister, readiness after successful setup, and ordered teardown.
- IRQ requests tracked only after success, cancellation after stopping IPC IRQs,
  and refusal to request the same P11 GPIO simultaneously as normal and IPC IRQ.
- Serialized ioctls/open/close; bounded vendor strings; zero-filled 16-byte
  netlink notifications; validated message headers and actual sender port IDs.
- Touch-slot bounds and initialization before input-handler registration.
  Successful IPC reads return the byte count; copy failure returns `EFAULT`.

The published DT does not provide AP-side intr2 or `reset-pin-enable`. This
candidate does not invent that wiring or silently drive GPIO zero. Unsupported
AP/reset configurations are rejected. Manual unbinding is disabled; dynamic DT
removal/reprobe with open descriptors has not been qualified. Remaining review
includes suspend/wakeup accounting across IPC and explicit HAL wakelocks,
notifier cancellation under load, concurrent teardown and stock HAL expectations
(including the corrected IPC read return). Enrollment/authentication still needs
the matching HAL, TA, firmware and hardware. None is implemented by this glue.

Keep fingerprint disabled in normal builds until these checks pass. The test
fragment and `fingerprint` build phase explicitly opt into compiling it; they are
not deployment configurations.

## Reproduce and interpret validation

Use the toolchain environment documented in [annibale-port.md](annibale-port.md).
From the kernel source:

```sh
# Configures and compiles the enabled board drivers, including HWID and Wi-Fi:
bash scripts/xiaomi/compile-annibale-board.sh all

# Dedicated test output recommended: adds P11 fingerprint and panel notifier:
bash scripts/xiaomi/compile-annibale-board.sh fingerprint

# Real production functions with host kernel/bus/PM mocks and UBSan:
python3 scripts/xiaomi/tests/test_annibale_peripherals.py
```

The fingerprint phase leaves its explicit test configuration in `OUT_DIR`.
Running `prepare` or `all` rebuilds the default configuration with fingerprint off.
Do not treat this smoke-check output directory as a ROM image build.

On 2026-11-15 all three new drivers and the panel notifier compiled with Android
clang r510928; the four fingerprint objects combined into an AArch64 `mi_fp.o`.
Four host suites passed: missing/invalid HWID and explicit zero, power fault
injection/reference balance, touch-slot bounds, and Wi-Fi read/send errors plus
wake/teardown ordering. Tests extract the actual C functions, not copied versions
of their logic. Kernel APIs and scheduling are mocked, so this is not a kernel
race test. Existing duplicate DT-target recipe warnings remain.

No full Image, final `.ko`/modpost, Module.symvers/KMI validation, module partition
assembly, radio SAR compliance, fingerprint authentication or physical test has
been performed. MCA charging, full display extensions and device firmware remain
separate unfinished integration work.
