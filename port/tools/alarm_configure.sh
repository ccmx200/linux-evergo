#!/bin/bash
# Configure the Arch Linux ARM rootfs for the evergo phone.
# No chroot is needed: we edit files and drop systemd symlinks directly.
set -e
R=/root/alarm/rootfs

echo "=== hostname ==="
echo evergo > "$R/etc/hostname"
cat > "$R/etc/hosts" <<'EOF'
127.0.0.1   localhost
::1         localhost
127.0.1.1   evergo.localdomain evergo
EOF

echo "=== fstab (root is f2fs on the userdata partition) ==="
cat > "$R/etc/fstab" <<'EOF'
# <file system>   <dir>   <type>   <options>            <dump> <pass>
/dev/root         /       f2fs     rw,relatime          0      1
proc              /proc   proc     defaults             0      0
sysfs             /sys    sysfs    defaults             0      0
devtmpfs          /dev    devtmpfs mode=0755,nosuid     0      0
tmpfs             /tmp    tmpfs    mode=1777,nosuid,nodev 0    0
EOF

echo "=== pacman: Arch Linux ARM mirror (TUNA) ==="
cat > "$R/etc/pacman.d/mirrorlist" <<'EOF'
Server = https://mirrors.tuna.tsinghua.edu.cn/archlinuxarm/$arch/$repo
EOF

echo "=== root password: root ==="
HASH=$(openssl passwd -6 root)
awk -v h="$HASH" 'BEGIN{FS=OFS=":"} $1=="root"{$2=h} {print}' \
    "$R/etc/shadow" > "$R/etc/shadow.new"
mv "$R/etc/shadow.new" "$R/etc/shadow"
chmod 600 "$R/etc/shadow"
grep -c '^root:' "$R/etc/shadow"

echo "=== serial console on the USB gadget (ttyGS0) and on the framebuffer ==="
mkdir -p "$R/etc/systemd/system/getty.target.wants"
ln -sf /usr/lib/systemd/system/serial-getty@.service \
       "$R/etc/systemd/system/getty.target.wants/serial-getty@ttyGS0.service"
ln -sf /usr/lib/systemd/system/getty@.service \
       "$R/etc/systemd/system/getty.target.wants/getty@tty1.service"

echo "=== autologin root on ttyGS0 ==="
mkdir -p "$R/etc/systemd/system/serial-getty@ttyGS0.service.d"
cat > "$R/etc/systemd/system/serial-getty@ttyGS0.service.d/autologin.conf" <<'EOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin root --noclear %I 115200 vt100
EOF

echo "=== serial console in the kernel cmdline is separate; ensure a getty too ==="
mkdir -p "$R/etc/systemd/system/multi-user.target.wants"
ln -sf /usr/lib/systemd/system/sshd.service \
       "$R/etc/systemd/system/multi-user.target.wants/sshd.service" 2>/dev/null || true

echo "=== done ==="
ls -l "$R/etc/systemd/system/getty.target.wants/"
