#!/bin/bash
# Assemble the root-boot initramfs and build it with the safe (full) config.
set -eu
KVM=/mnt/c/Users/cuicanmx/Documents/deepseek-harness/default-workspace/kvm
SRC=/root/initramfs-root

rm -rf "$SRC"
cp -a /root/initramfs-test "$SRC"
rm -f "$SRC"/lib/modules/wlan_6833_axi.ko
cp -f "$KVM/init_root" "$SRC/init"
chmod 755 "$SRC/init"
echo "init head:"; head -3 "$SRC/init"
grep -n "mount -t ext4\|mount -t f2fs" "$SRC/init" | head -4

bash "$KVM/build_safe.sh" "$SRC"
