#!/bin/bash
# Switch the kernel's built-in initramfs between the two builds.
#
#   ./use_initramfs.sh test       -> /root/initramfs-test      (never touches userdata)
#   ./use_initramfs.sh install    -> /root/initramfs-installer (the final installer)
#
# The install build is the only one that may write userdata; the test build is
# self-contained and read-only towards it.
set -eu
export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- CC="ccache aarch64-linux-gnu-gcc"
N=/root/linux-evergo-new
O=$N/out
MODE=${1:-test}

case "$MODE" in
  test)    SRC=/root/initramfs-test ;;
  install) SRC=/root/initramfs-installer ;;
  *) echo "usage: $0 test|install"; exit 1 ;;
esac

BEFORE=$(stat -c%s "$O/.config")
cd "$N"
./scripts/config --file "$O/.config" --set-str INITRAMFS_SOURCE "$SRC"
make -s O="$O" olddefconfig </dev/null >/dev/null 2>&1
AFTER=$(stat -c%s "$O/.config")
grep -m1 CONFIG_INITRAMFS_SOURCE "$O/.config"
echo "config size: $BEFORE -> $AFTER"
[ "$BEFORE" != "$AFTER" ] && echo "WARNING: config size changed - check it did not shrink"

# force the initramfs to be rebuilt
rm -f "$O/usr/initramfs_data.cpio" "$O/usr/initramfs_data.o" "$O/usr/.initramfs_data"*.cmd 2>/dev/null

T0=$(date +%s)
make -s O="$O" -j"$(nproc)" Image </dev/null > /root/logs/build_$MODE.log 2>&1
RC=$?
T1=$(date +%s)
echo "make rc=$RC elapsed=$((T1-T0))s errors=$(grep -c 'error:' /root/logs/build_$MODE.log || true)"
[ "$RC" -ne 0 ] && { grep -E "error:" /root/logs/build_$MODE.log | head -10; exit 1; }

ls -la "$O/arch/arm64/boot/Image"
cpio -itv < "$O/usr/initramfs_data.cpio" 2>/dev/null | head -6
