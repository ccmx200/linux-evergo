#!/bin/bash
# Restore the kernel config that a cascading olddefconfig clobbered.
#
# Symptom: out/.config shrank from 326487 to 241602 bytes, CONFIG_ARCH_MEDIATEK
# became "# CONFIG_ARCH_MEDIATEK is not set", and with it the whole MediaTek
# platform branch - including CONFIG_MTK_MT6833_CONN, whose Kconfig is
# `depends on ARCH_MEDIATEK || COMPILE_TEST`.  That is why the built-in link
# lost mt6833_conn_power_on: the driver was no longer being built at all.
#
# This re-enables the branch and then re-asserts every symbol this port needs,
# with olddefconfig run once at the end so the cascade resolves in order.
set -u
export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
N=/root/linux-evergo-new
O=$N/out
cd "$N" || exit 1

cp -f "$O/.config" /root/logs/config.broken
echo "before: $(wc -c < "$O/.config") bytes, ARCH_MEDIATEK: $(grep -m1 'ARCH_MEDIATEK' "$O/.config" || echo absent)"

CFG="$O/.config"
./scripts/config --file "$CFG" -e ARCH_MEDIATEK
./scripts/config --file "$CFG" -e MTK_MT6833_CONN
./scripts/config --file "$CFG" -e PINCTRL
./scripts/config --file "$CFG" -e MTK_TIMER
./scripts/config --file "$CFG" -e RFKILL
./scripts/config --file "$CFG" -e CFG80211
./scripts/config --file "$CFG" -e CFG80211_WEXT
./scripts/config --file "$CFG" -e MTK_WLAN_GEN4M
./scripts/config --file "$CFG" -e MTK_WLAN_GEN4M_CHIP_SOC2_1X1
./scripts/config --file "$CFG" -e MTK_WLAN_GEN4M_WMT
./scripts/config --file "$CFG" -d MTK_WLAN_GEN4M_EMI

make -s O="$O" olddefconfig </dev/null >/root/logs/restore_olddef.log 2>&1
echo " rc=$?"
echo "after:  $(wc -c < "$O/.config") bytes"
for k in ARCH_MEDIATEK MTK_MT6833_CONN RFKILL CFG80211 CFG80211_WEXT \
         MTK_WLAN_GEN4M MTK_WLAN_GEN4M_WMT MTK_WLAN_GEN4M_EMI PM OF_RESERVED_MEM; do
    printf '  %-30s %s\n' "$k" \
      "$(grep -m1 "^CONFIG_$k=" "$O/.config" || grep -m1 "^# CONFIG_$k is not set" "$O/.config" || echo MISSING)"
done
echo
echo "=== sanity: core arm64 options still on? ==="
for k in ARM64 SMP ARM_GIC MULTIARCH PCI; do
    printf '  %-12s %s\n' "$k" \
      "$(grep -m1 "^CONFIG_$k=" "$O/.config" || echo MISSING)"
done
