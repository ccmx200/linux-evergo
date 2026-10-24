#!/bin/bash
# Pack the current Image into boot.img.
# Mirrors pack_own2.sh but keeps the fallback-proof ordering in one place.
set -eu
export ARCH=arm64
N=/root/linux-evergo-new
O=$N/out
IMGDIR=/root/evergo-out
mkdir -p "$IMGDIR"
I=$O/arch/arm64/boot/Image
OUT_NAME=${OUT_NAME:-boot-own2.img}


gzip -9 -n -c "$I" > "$IMGDIR/own-image.gz"
cat "$IMGDIR/own-image.gz" /root/phone/author_dtb.bin > "$IMGDIR/own-kernel.bin"

CMDLINE='bootopt=64S3,32N2,64N2 androidboot.selinux=enforcing buildvariant=user'

python3 /root/mkboot2.py \
    --kernel "$IMGDIR/own-kernel.bin" \
    --ramdisk /root/phone/auth_ramdisk.bin \
    --dtb /root/phone/auth_dttable.bin \
    --pagesize 2048 --header-version 2 --os-version 0x1800017c \
    --cmdline "$CMDLINE" \
    --base 0x40000000 --kernel-offset 0x80000 \
    --ramdisk-offset 0x11100000 --tags-offset 0x7c80000 \
    --output "$IMGDIR/own-nofooter.img" >/dev/null

python3 - <<'PY'
import hashlib, struct
PART = 134217728
raw = open('/root/evergo-out/own-nofooter.img','rb').read()
orig = len(raw)
stock = open('/root/phone/boot_a.img','rb').read()
t = stock[-64:]
vo, vs = struct.unpack_from('>Q', t, 20)[0], struct.unpack_from('>Q', t, 28)[0]
vb = bytearray(stock[vo:vo+vs])
isz = 256 + 16
lens = isz + 8 + 32
pn, sn, dn, _ = struct.unpack_from('>4I', vb, lens)
body = lens + 16 + 60
salt = bytes(vb[body+pn:body+pn+sn]); dig = body + pn + sn
struct.pack_into('>Q', vb, isz, orig)
vb[dig:dig+dn] = hashlib.sha256(salt + raw).digest()
buf = bytearray(raw) + vb
v2 = len(raw)
buf += b'\x00' * (PART - 64 - len(buf))
f = bytearray(64); f[0:4] = b'AVBf'
struct.pack_into('>2I', f, 4, 1, 0)
struct.pack_into('>Q', f, 12, orig); struct.pack_into('>Q', f, 20, v2)
struct.pack_into('>Q', f, 28, len(vb))
buf += f
assert len(buf) == PART
open('/root/evergo-out/' + __import__('os').environ.get('OUT_NAME','boot-own2.img'),'wb').write(buf)
print(f"  boot.img 128 MB, image_size {orig}")
PY
cp -f "$IMGDIR/$OUT_NAME" "/mnt/c/Users/cuicanmx/Documents/deepseek-harness/default-workspace/kvm/images/boot/$OUT_NAME"
