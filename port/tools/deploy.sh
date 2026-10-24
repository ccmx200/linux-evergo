#!/bin/bash
# Full, verified build + pack for the mainline port.
#
# Fixes the three traps that silently produced stale images:
#   1. "make Image" does not rebuild the device tree - "make dtbs" is required;
#   2. pack_now.sh appends /root/phone/author_dtb.bin, not the freshly built dtb;
#   3. the wlan driver is a module, so it must be rebuilt, stripped and copied
#      into the initramfs before the initramfs is regenerated.
#
# Prints the kernel build stamp so a boot can be verified against /proc/version.
set -e
export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
N=/root/linux-evergo-new
O=$N/out
LOG=/root/logs/deploy.log
mkdir -p /root/logs

echo "=== 1/6 Image ==="
make -s -C "$N" O="$O" -j8 Image </dev/null > "$LOG" 2>&1
grep -c "error:" "$LOG" || true

echo "=== 2/6 modules ==="
make -s -C "$N" O="$O" -j8 modules </dev/null >> "$LOG" 2>&1
grep -c "error:" "$LOG" || true
KO=$O/drivers/net/wireless/mediatek/gen4m/wlan_6833_axi.ko
ls -l "$KO" | awk '{print "  ko", $5, $6, $7, $8}'

echo "=== 3/6 strip + xz ==="
bash /mnt/c/Users/cuicanmx/Documents/deepseek-harness/default-workspace/kvm/prep_ko.sh "$KO" /root/ko
cp -f /root/ko/wlan_6833_axi.ko /root/initramfs-test/lib/modules/

echo "=== 4/6 dtbs ==="
make -s -C "$N" O="$O" dtbs </dev/null >> "$LOG" 2>&1
DTB=$O/arch/arm64/boot/dts/mediatek/mt6833-xiaomi-evergo.dtb
ls -l "$DTB" | awk '{print "  dtb", $5, $6, $7, $8}'
cp -f "$DTB" /root/phone/author_dtb.bin

echo "=== 5/6 relink Image with the new initramfs ==="
bash /mnt/c/Users/cuicanmx/Documents/deepseek-harness/default-workspace/kvm/use_initramfs.sh test | head -3

echo "=== 6/6 pack ==="
bash /mnt/c/Users/cuicanmx/Documents/deepseek-harness/default-workspace/kvm/pack_now.sh

echo "=== build stamp (compare with /proc/version after boot) ==="
strings "$O/arch/arm64/boot/Image" | grep -m1 "Linux version"
ls -l /mnt/c/Users/cuicanmx/Documents/deepseek-harness/default-workspace/kvm/images/boot/boot-own2.img \
  | awk '{print "  boot-own2.img", $5, $6, $7, $8}'
