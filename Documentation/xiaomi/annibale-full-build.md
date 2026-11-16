# R8: complete kernel and module link qualification

These are historical R8 results. See [R9 battery reporting](annibale-battery.md)
for the subsequent bridge and its separate build record.

R8 adds a reproducible complete Make build, followed by a joint external-module
modpost. This closes the earlier object-only validation gap. It does not provide
a boot image, working Android battery stack, or a verified hardware release.

## Kernel profiles

Use the pinned clang-r510928 and AOSP build-tools environment described in
`annibale-port.md`, with a dedicated absolute `OUT_DIR`:

```sh
# Normal Annibale fragment: experimental fingerprint and MCA remain disabled.
scripts/xiaomi/compile-annibale-board.sh full

# Separate development output recommended. Enables both experimental fragments.
# This disables QTI_BATTERY_CHARGER because its Glink owner conflicts with MCA.
scripts/xiaomi/compile-annibale-board.sh qualify
```

Both phases build `Image`, `vmlinux`, the real `Module.symvers`, and all selected
in-tree `.ko` files with normal modpost error handling. `all`, `fingerprint` and
`mca` keep their earlier object-check behavior. Qualification is deliberately
explicit; `annibale_board.config` remains unchanged. The same output directory
must not be reconfigured while external modules are building against it.

A full build exposed a missing host `cpio` needed by `CONFIG_IKHEADERS`. The
helper now exposes the real AOSP toybox `cpio` applet in its private host-bin,
alongside the existing AOSP `bc` alias. The kernel headers feature remains on.

## External dependency group

After the complete kernel phase finishes, run from the modules repository:

```sh
python3 -B tools/build_annibale_modules.py \
  --kernel /path/to/kernel/xiaomi/sm8750 \
  --kernel-out /path/to/out/annibale-qualify \
  --clang-bin /path/to/clang-r510928/bin \
  --output /path/to/out/annibale-external --jobs 6
```

The helper copies selected sources into a fresh staging directory and builds
touch, camera, display, audio, DSP and NFC together with their multimedia and
security providers. Joint modpost resolves the real mutual SYNX/hardware-fence
references without fake symbol files or turning undefined symbols into warnings.
The source checkouts are not used as module build-output directories.

The modules repository records the external build fixes and validation details
in `Documentation/xiaomi/annibale-full-build.md`. Its post-link verifier checks
ordered modules for ARM64 ELF, matching release/vermagic, every recorded imported
symbol CRC against these builds' actual exports, and presence of declared module
dependencies. Old `.ko` files not present in `modules.order` are excluded.

## Verified scope and remaining work

The normal profile produced 401 in-tree modules. The explicit qualification
profile produced 410, including P11 `mi_fp.ko`, all nine MCA components including
`mca_bq27z561.ko`, HWID, Wi-Fi SAR, T1, IR, haptics and the panel notifier.
Both profiles generated a complete ARM64 Image with clang-r510928. Build hashes,
external module counts and checks are recorded in `annibale-r8-validation.json`.

Qualification uses the LOS display/audio/camera baseline plus the already ported
P11 PHY and candidate touch code. Link success does not implement the still
missing Xiaomi `mi_disp` extensions, board amplifier integration, camera OIS/
actuator differences, full charging policy, or fingerprint HAL/TEE support.
The NFC driver compatible matches Annibale's `qcom,sn-nci` node at I2C 0x28;
that alone does not validate its userspace/secure-element behavior.

Module signature trust, Android KMI allowlists, compatibility with stock modules,
CFI execution, module loading, firmware/HAL integration, full DTBO/bootloader
selection, partition assembly and hardware tests remain outstanding. In
particular, experimental MCA still has no complete battery/charging policy and
must not replace the running battery stack just because it now links.

These scripts qualify selected source builds. They do not produce `boot.img`,
`vendor_boot.img`, `vendor_dlkm.img`, `system_dlkm.img`, an AK3 package, or a ROM.
