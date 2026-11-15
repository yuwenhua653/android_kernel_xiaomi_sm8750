#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# ARM64 object compilation smoke check, not a boot image or module-link build.
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
export M4="$HOST_TOOLS_BIN/m4"
export BISON_PKGDATADIR
OUT_DIR=$(realpath -m "$OUT_DIR")
# AOSP publishes bc under its implementation name; Kbuild invokes "bc".
if ! command -v bc >/dev/null && [[ -x "$HOST_TOOLS_BIN/gavinhoward-bc" ]]; then
	mkdir -p "$OUT_DIR/host-bin"
	ln -sf "$HOST_TOOLS_BIN/gavinhoward-bc" "$OUT_DIR/host-bin/bc"
	export PATH="$OUT_DIR/host-bin:$PATH"
fi
JOBS=${JOBS:-3}
PHASE=${1:-all}
if [[ "$PHASE" != all && "$PHASE" != prepare && "$PHASE" != objects && "$PHASE" != fingerprint ]]; then
	printf 'Usage: %s [all|prepare|objects|fingerprint]\n' "$0" >&2
	exit 2
fi
for tool in clang ld.lld llvm-ar bc gcc bison "$M4"; do
	command -v "$tool" >/dev/null || { printf 'Missing host tool: %s\n' "$tool" >&2; exit 1; }
done
mkdir -p "$OUT_DIR"
cd "$KERNEL_SRC"
KMAKE=(make O="$OUT_DIR" ARCH=arm64 LLVM=1 HOSTCC=gcc "-j$JOBS")
if [[ "$PHASE" != objects ]]; then
	"${KMAKE[@]}" gki_defconfig 2>&1 | tee "$OUT_DIR/configure.log"
	KCONFIG_CONFIG="$OUT_DIR/.config" scripts/kconfig/merge_config.sh -m \
		"$OUT_DIR/.config" arch/arm64/configs/vendor/sun_perf.config \
		arch/arm64/configs/vendor/annibale_board.config \
		2>&1 | tee "$OUT_DIR/merge-config.log"
	if [[ "$PHASE" == fingerprint ]]; then
		KCONFIG_CONFIG="$OUT_DIR/.config" scripts/kconfig/merge_config.sh -m \
			"$OUT_DIR/.config" arch/arm64/configs/vendor/annibale_fingerprint_test.config
	fi
	"${KMAKE[@]}" olddefconfig modules_prepare 2>&1 | tee "$OUT_DIR/prepare.log"
fi
for option in CONFIG_MI_T1_CHIP=m CONFIG_IR_SPI_XIAOMI=m \
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
	if [[ "$PHASE" == fingerprint ]]; then
		grep -qx CONFIG_FINGERPRINT_MI_FP=m "$OUT_DIR/.config"
		grep -qx CONFIG_ULTRA_FINGERPRINT_P11=y "$OUT_DIR/.config"
		"${KMAKE[@]}" drivers/input/fingerprint/mi_fp/ \
			drivers/soc/qcom/panel_event_notifier.o \
			2>&1 | tee "$OUT_DIR/fingerprint-objects.log"
	fi
	# Optional source check only: leave the saved annibale .config unchanged.
	"${KMAKE[@]}" CONFIG_LEDS_AW21024=m drivers/leds/leds-aw21024.o \
		2>&1 | tee "$OUT_DIR/optional-led-object.log"
fi
