#!/bin/bash
# Safe build: leave the config alone, only point the initramfs and make sure the
# port's own symbols are on.  author.config keeps ARCH_MEDIATEK, so the whole
# MediaTek branch stays enabled.
set -eu
export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
N=/root/linux-evergo-new
O=$N/out
KVM=/mnt/c/Users/cuicanmx/Documents/deepseek-harness/default-workspace/kvm
SRC=${1:-/root/initramfs-test}

mkdir -p "$O"

MAKE_CC="${MAKE_CC:-ccache ${CROSS_COMPILE}gcc}"

CFG=$O/.config

# Start from the complete config only if the current one is the shrunken one.
n=$(grep -cE '^CONFIG_' "$CFG" 2>/dev/null || echo 0)
if [ "$n" -lt 4000 ]; then
    echo "config looks truncated ($n options) - restoring author.config"
    cp -f "$KVM/configs/author.config" "$CFG"
fi

cd "$N"
./scripts/config --file "$CFG" --set-str INITRAMFS_SOURCE "$SRC"
./scripts/config --file "$CFG" -e ARCH_MEDIATEK
./scripts/config --file "$CFG" -e MTK_MT6833_CONN
./scripts/config --file "$CFG" -e MTK_WLAN_GEN4M
./scripts/config --file "$CFG" -e MTK_WLAN_GEN4M_CHIP_SOC2_1X1
./scripts/config --file "$CFG" -e MTK_WLAN_GEN4M_WMT
./scripts/config --file "$CFG" -d MTK_WLAN_GEN4M_EMI
./scripts/config --file "$CFG" -e RFKILL
./scripts/config --file "$CFG" -e CFG80211
./scripts/config --file "$CFG" -e CFG80211_WEXT

make -s O="$O" olddefconfig </dev/null >/root/logs/safe_od.log 2>&1
echo "options: $(grep -cE '^CONFIG_' "$CFG")"
for k in ARCH_MEDIATEK MTK_MT6833_CONN MTK_WLAN_GEN4M INITRAMFS_SOURCE; do
    printf '  %-22s %s\n' "$k" "$(grep -m1 "^CONFIG_$k=" "$CFG" || echo MISSING)"
done

echo "=== dtbs (needed by CONFIG_EXTRA_FIRMWARE) ==="
make -s O="$O" -j8 dtbs CC="$MAKE_CC" </dev/null >>/root/logs/safe_build.log 2>&1 || true
DTB=$O/arch/arm64/boot/dts/mediatek/mt6833-xiaomi-evergo.dtb
ls -l "$DTB" 2>/dev/null || echo "DTB missing"

echo "=== Image ==="
make -s O="$O" -j8 Image CC="$MAKE_CC" </dev/null >/root/logs/safe_build.log 2>&1 || true
echo "errors=$(grep -c 'error:' /root/logs/safe_build.log || true)"
grep -E 'error:' /root/logs/safe_build.log | head -5
ls -l "$O/arch/arm64/boot/Image"

bash "$KVM/pack_now.sh"
