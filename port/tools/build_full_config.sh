#!/bin/bash
# Build from the full known-good config without running olddefconfig again.
#
# olddefconfig keeps cascading options away (ARCH_MEDIATEK off -> the whole
# MediaTek platform branch disappears, MTK_MT6833_CONN included).  author.config
# is complete, so just point CONFIG_INITRAMFS_SOURCE at the tree we want and
# build.
set -u
export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
N=/root/linux-evergo-new
O=$N/out
KVM=/mnt/c/Users/cuicanmx/Documents/deepseek-harness/default-workspace/kvm
SRC=${1:-/root/initramfs-test}

cp -f "$KVM/configs/author.config" "$O/.config"
sed -i "s|^CONFIG_INITRAMFS_SOURCE=.*|CONFIG_INITRAMFS_SOURCE=\"$SRC\"|" "$O/.config"

echo "options: $(grep -cE '^CONFIG_' "$O/.config")"
grep -m1 CONFIG_INITRAMFS_SOURCE "$O/.config"
grep -m1 '^CONFIG_ARCH_MEDIATEK=' "$O/.config"
grep -m1 '^CONFIG_MTK_MT6833_CONN=' "$O/.config"

cd "$N"
make -s O="$O" -j8 Image </dev/null > /root/logs/build_full.log 2>&1
echo "rc=$? errors=$(grep -c 'error:' /root/logs/build_full.log || true)"
grep -E 'error:' /root/logs/build_full.log | head -5
ls -l "$O/arch/arm64/boot/Image"

bash "$KVM/pack_now.sh"
