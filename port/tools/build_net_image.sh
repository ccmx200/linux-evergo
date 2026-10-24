#!/bin/bash
# Build the "network" boot image: configfs gadget with ACM + RNDIS instead of
# the legacy g_serial gadget, so the phone exposes a USB network adapter.
#
# The safe image (boot-own2.img, g_serial) is left untouched: if this one does
# not come up, flashing the safe image restores the serial console.
set -u
export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
K=/mnt/c/Users/cuicanmx/Documents/deepseek-harness/default-workspace/kvm
N=/root/linux-evergo-new
O=$N/out
CFG=$O/.config
SRC=${1:-/root/initramfs-root}

mkdir -p "$O"
cp -f "$CFG" /root/backup-working/config.before-net 2>/dev/null

echo "=== switch to the configfs gadget ==="
cd "$N" || exit 1
./scripts/config --file "$CFG" -d USB_G_SERIAL
./scripts/config --file "$CFG" -e USB_CONFIGFS
./scripts/config --file "$CFG" -e USB_CONFIGFS_ACM
./scripts/config --file "$CFG" -e USB_CONFIGFS_RNDIS
make -s O="$O" olddefconfig </dev/null >/root/logs/net_od.log 2>&1
for k in USB_G_SERIAL USB_CONFIGFS USB_CONFIGFS_ACM USB_CONFIGFS_RNDIS; do
    printf '  %-22s %s\n' "$k" \
      "$(grep -m1 "^CONFIG_$k=" "$CFG" || grep -m1 "^# CONFIG_$k is not set" "$CFG" || echo MISSING)"
done

echo "=== build ($SRC) ==="
MAKE_CC="${MAKE_CC:-ccache ${CROSS_COMPILE}gcc}"
./scripts/config --file "$CFG" --set-str INITRAMFS_SOURCE "$SRC"
make -s O="$O" -j8 dtbs  CC="$MAKE_CC" </dev/null >/root/logs/net_build.log 2>&1
make -s O="$O" -j8 Image CC="$MAKE_CC" </dev/null >>/root/logs/net_build.log 2>&1
echo "errors=$(grep -c 'error:' /root/logs/net_build.log)"
grep -E 'error:' /root/logs/net_build.log | head -5
ls -l "$O/arch/arm64/boot/Image"

OUT_NAME=boot-net.img bash "$K/pack_now.sh"

echo "=== restore the safe config ==="
cp -f /root/backup-working/config.before-net "$CFG"
echo "restored"
