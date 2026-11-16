#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# ARM64 board checks; full/qualify also link Image and all in-tree modules.
set -euo pipefail

KERNEL_SRC=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
: "${OUT_DIR:?Set OUT_DIR to a dedicated kernel build directory}"
: "${CLANG_BIN:?Set CLANG_BIN to clang-r510928/bin}"
: "${HOST_TOOLS_BIN:?Set HOST_TOOLS_BIN to the AOSP build-tools/linux-x86/bin directory}"
: "${BISON_PKGDATADIR:?Set BISON_PKGDATADIR to AOSP build-tools/common/bison}"
CLANG_BIN=$(realpath "$CLANG_BIN")
HOST_TOOLS_BIN=$(realpath "$HOST_TOOLS_BIN")
BISON_PKGDATADIR=$(realpath "$BISON_PKGDATADIR")
export PATH="$CLANG_BIN:$HOST_TOOLS_BIN:$PATH"
# Without pahole, olddefconfig silently drops BTF and changes module_layout.
if [[ -n "${KERNEL_TOOLS_BIN:-}" ]]; then
	KERNEL_TOOLS_BIN=$(realpath "$KERNEL_TOOLS_BIN")
	export PATH="$KERNEL_TOOLS_BIN:$PATH"
fi
export M4="$HOST_TOOLS_BIN/m4"
export BISON_PKGDATADIR
OUT_DIR=$(realpath -m "$OUT_DIR")
mkdir -p "$OUT_DIR/host-bin"
export PATH="$OUT_DIR/host-bin:$PATH"
# AOSP publishes bc under its implementation name; Kbuild invokes "bc".
if ! command -v bc >/dev/null && [[ -x "$HOST_TOOLS_BIN/gavinhoward-bc" ]]; then
	mkdir -p "$OUT_DIR/host-bin"
	ln -sf "$HOST_TOOLS_BIN/gavinhoward-bc" "$OUT_DIR/host-bin/bc"
fi
# AOSP toybox supplies cpio for CONFIG_IKHEADERS in a complete kernel build.
if ! command -v cpio >/dev/null && [[ -x "$HOST_TOOLS_BIN/toybox" ]]; then
	ln -sf "$HOST_TOOLS_BIN/toybox" "$OUT_DIR/host-bin/cpio"
fi
JOBS=${JOBS:-3}
PHASE=${1:-all}
if [[ "$PHASE" != all && "$PHASE" != prepare && "$PHASE" != objects && "$PHASE" != fingerprint && "$PHASE" != mca && "$PHASE" != full && "$PHASE" != qualify ]]; then
	printf 'Usage: %s [all|prepare|objects|fingerprint|mca|full|qualify]\n' "$0" >&2
	exit 2
fi
if [[ "$PHASE" == full || "$PHASE" == qualify ]]; then
	for option in KBUILD_MODPOST_WARN KBUILD_MODPOST_NOFINAL KBUILD_EXTRA_SYMBOLS KBUILD_MIXED_TREE; do
		[[ -z "${!option:-}" ]] || { printf 'Unset %s for a complete build\n' "$option" >&2; exit 1; }
	done
	command -v cpio >/dev/null || { printf 'Missing host tool: cpio\n' >&2; exit 1; }
fi
for tool in clang ld.lld llvm-ar bc gcc bison pahole "$M4"; do
	command -v "$tool" >/dev/null || { printf 'Missing host tool: %s\n' "$tool" >&2; exit 1; }
done
mkdir -p "$OUT_DIR"
cd "$KERNEL_SRC"
KMAKE=(make O="$OUT_DIR" ARCH=arm64 LLVM=1 HOSTCC=gcc "-j$JOBS")
if [[ -n "${KERNEL_TOOLS_BIN:-}" ]]; then
	KERNEL_TOOLS_ROOT=$(dirname "$KERNEL_TOOLS_BIN")
	[[ -f "$KERNEL_TOOLS_ROOT/include/libelf.h" && -f "$KERNEL_TOOLS_ROOT/lib64/libelf.so" ]] || {
		printf 'Missing libelf headers/library beside KERNEL_TOOLS_BIN\n' >&2
		exit 1
	}
	KMAKE+=("HOSTCFLAGS=${HOSTCFLAGS:-} -I$KERNEL_TOOLS_ROOT/include"
		"HOSTLDFLAGS=${HOSTLDFLAGS:-} -L$KERNEL_TOOLS_ROOT/lib64 -Wl,-rpath,$KERNEL_TOOLS_ROOT/lib64")
fi
if [[ "$PHASE" != objects ]]; then
	"${KMAKE[@]}" gki_defconfig 2>&1 | tee "$OUT_DIR/configure.log"
	KCONFIG_CONFIG="$OUT_DIR/.config" scripts/kconfig/merge_config.sh -m \
		"$OUT_DIR/.config" arch/arm64/configs/vendor/sun_perf.config \
		arch/arm64/configs/vendor/annibale_board.config \
		2>&1 | tee "$OUT_DIR/merge-config.log"
	if [[ "$PHASE" == fingerprint || "$PHASE" == qualify ]]; then
		KCONFIG_CONFIG="$OUT_DIR/.config" scripts/kconfig/merge_config.sh -m \
			"$OUT_DIR/.config" arch/arm64/configs/vendor/annibale_fingerprint_test.config
	fi
	if [[ "$PHASE" == mca || "$PHASE" == qualify ]]; then
		KCONFIG_CONFIG="$OUT_DIR/.config" scripts/kconfig/merge_config.sh -m \
			"$OUT_DIR/.config" arch/arm64/configs/vendor/annibale_mca_test.config
	fi
	"${KMAKE[@]}" olddefconfig modules_prepare 2>&1 | tee "$OUT_DIR/prepare.log"
fi
for option in CONFIG_DEBUG_INFO_BTF=y CONFIG_DEBUG_INFO_BTF_MODULES=y \
	CONFIG_MI_T1_CHIP=m CONFIG_IR_SPI_XIAOMI=m \
	CONFIG_INPUT_PM8941_PWRKEY=m CONFIG_INPUT_PM8941_PWRKEY_XIAOMI=y \
	CONFIG_INPUT_QCOM_HV_HAPTICS=m CONFIG_INPUT_QCOM_HV_HAPTICS_XIAOMI=y \
	CONFIG_MI_HARDWARE_ID=m CONFIG_MI_WIFI_GPIO=m; do
	grep -qx "$option" "$OUT_DIR/.config" || { printf 'Missing config: %s\n' "$option" >&2; exit 1; }
done
if [[ "$PHASE" != prepare ]]; then
	"${KMAKE[@]}" drivers/gpio/gpio-mi-t1.o \
		drivers/media/rc/xiaomi/ir-spi.o \
		drivers/input/misc/pm8941-pwrkey.o \
		drivers/input/misc/qcom-hv-haptics.o \
		drivers/misc/hwid/hwid.o \
		drivers/misc/wifi_gpio/xiaomi_wifi_gpio.o 2>&1 | tee "$OUT_DIR/board-objects.log"
	if [[ "$PHASE" == fingerprint || "$PHASE" == qualify ]]; then
		grep -qx CONFIG_FINGERPRINT_MI_FP=m "$OUT_DIR/.config"
		grep -qx CONFIG_ULTRA_FINGERPRINT_P11=y "$OUT_DIR/.config"
		"${KMAKE[@]}" drivers/input/fingerprint/mi_fp/ \
			drivers/soc/qcom/panel_event_notifier.o \
			2>&1 | tee "$OUT_DIR/fingerprint-objects.log"
	fi
	if [[ "$PHASE" == mca || "$PHASE" == qualify ]]; then
		grep -qx CONFIG_XIAOMI_MCA_CORE=m "$OUT_DIR/.config"
		grep -qx CONFIG_XIAOMI_MCA_SERVICES=m "$OUT_DIR/.config"
		grep -qx CONFIG_XIAOMI_MCA_BQ27Z561=m "$OUT_DIR/.config"
		grep -qx CONFIG_XIAOMI_MCA_BATTERY=m "$OUT_DIR/.config"
		grep -qx CONFIG_XIAOMI_MCA_GLINK=m "$OUT_DIR/.config"
		grep -qx '# CONFIG_QTI_BATTERY_CHARGER is not set' "$OUT_DIR/.config"
		"${KMAKE[@]}" drivers/power/supply/mca/ \
			drivers/soc/qcom/qti_pmic_glink.o \
			2>&1 | tee "$OUT_DIR/mca-objects.log"
	fi
	# Optional source check only: leave the saved annibale .config unchanged.
	"${KMAKE[@]}" CONFIG_LEDS_AW21024=m drivers/leds/leds-aw21024.o \
		2>&1 | tee "$OUT_DIR/optional-led-object.log"
fi
if [[ "$PHASE" == full || "$PHASE" == qualify ]]; then
	"${KMAKE[@]}" Image modules 2>&1 | tee "$OUT_DIR/full-build.log"
	for artifact in arch/arm64/boot/Image vmlinux Module.symvers modules.order; do
		[[ -s "$OUT_DIR/$artifact" ]] || { printf 'Missing artifact: %s\n' "$artifact" >&2; exit 1; }
	done
	printf 'Linked Image and in-tree modules. Not a flashable boot image.\n'
fi
