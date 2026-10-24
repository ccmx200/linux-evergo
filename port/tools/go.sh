#!/bin/bash
# go.sh - single entry point for the evergo mainline port.
#
#   go.sh doctor        check the environment and every input this port needs
#   go.sh deps          install the build dependencies that are missing
#   go.sh kernel        build kernel + initramfs + boot.img  (ccache accelerated)
#   go.sh root          build the "boot into Arch" image
#   go.sh net           build the RNDIS (USB network) image
#   go.sh modules       build the loadable modules (.ko)
#   go.sh images        list the image tree
#   go.sh flash         flash images/boot/boot-own2.img to boot_b
#   go.sh flash-root [f2fs|ext4]
#                       flash a rootfs image to userdata
#   go.sh ccache        show cache statistics
#   go.sh clean         drop the build tree of the kernel (keeps the config)
#
# Run it from Windows with:
#   wsl -d Ubuntu-24.04 -u root -- bash /mnt/c/.../kvm/go.sh <command>
set -u

K=/mnt/c/Users/cuicanmx/Documents/deepseek-harness/default-workspace/kvm
N=/root/linux-evergo-new
O=$N/out
PT=/mnt/c/Users/cuicanmx/Documents/EEWork/Tools/Android/platform-tools
BOOT_IMG=$K/images/boot/boot-own2.img

export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
export CCACHE_DIR=/root/.ccache
export CC="ccache aarch64-linux-gnu-gcc"

c_ok()   { printf '\033[32m%s\033[0m\n' "$*"; }
c_warn() { printf '\033[33m%s\033[0m\n' "$*"; }
c_err()  { printf '\033[31m%s\033[0m\n' "$*"; }
hdr()    { printf '\n\033[1m== %s ==\033[0m\n' "$*"; }

need_root() {
    [ "$(id -u)" = 0 ] || { c_err "run me as root inside WSL"; exit 1; }
}

# ------------------------------------------------------------------ doctor --
cmd_doctor() {
    hdr "toolchain"
    for t in aarch64-linux-gnu-gcc aarch64-linux-gnu-ld make dtc ccache python3 openssl; do
        if p=$(command -v "$t" 2>/dev/null); then
            c_ok "  ok      $t  ($p)"
        else
            c_err "  MISSING $t"
        fi
    done

    hdr "sources"
    if [ -d "$N" ]; then c_ok "  kernel tree   $N"; else c_err "  MISSING $N"; fi
    if [ -f "$O/.config" ]; then
        n=$(grep -cE '^CONFIG_' "$O/.config")
        [ "$n" -ge 4000 ] && c_ok "  config        $n options" \
                          || c_err "  config        only $n options (truncated!)"
    else
        c_warn "  config        not configured yet"
    fi
    for d in /root/initramfs-test /root/initramfs-root; do
        [ -d "$d" ] && c_ok "  initramfs     $d" || c_warn "  missing       $d"
    done
    for f in /root/phone/author_dtb.bin /root/phone/auth_ramdisk.bin \
             /root/phone/auth_dttable.bin /root/phone/boot_a.img; do
        [ -f "$f" ] && c_ok "  boot piece    $f" || c_err "  MISSING       $f"
    done

    hdr "images"
    for f in "$K/images/boot/boot-own2.img" "$K/images/rootfs/arch-f2fs.img" \
             "$K/images/rootfs/arch-ext4.img"; do
        if [ -f "$f" ]; then
            c_ok "  $(du -h "$f" | cut -f1)  ${f#$K/}"
        else
            c_warn "  (absent)  ${f#$K/}"
        fi
    done

    hdr "host"
    [ -x "$PT/fastboot.exe" ] && c_ok "  fastboot found" || c_warn "  fastboot missing at $PT"
    c_ok "  ccache $(ccache --version 2>/dev/null | head -1)"
    ccache -s 2>/dev/null | sed 's/^/  /' | head -8
}

# -------------------------------------------------------------------- deps --
cmd_deps() {
    need_root
    hdr "installing build dependencies"
    apt-get update -qq
    apt-get install -y -qq ccache gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
        device-tree-compiler build-essential bc bison flex libssl-dev \
        libelf-dev python3 openssl rsync kmod xz-utils
    c_ok "done"
}

# ------------------------------------------------------------------ kernel --
cmd_kernel() {
    need_root
    hdr "kernel build (ccache)"
    bash "$K/build_safe.sh" /root/initramfs-test
    hdr "result"
    ls -l "$BOOT_IMG" 2>/dev/null | awk '{print "  ", $9, $5, $6, $7, $8}'
}

# -------------------------------------------------------------------- root --
cmd_root() {
    need_root
    hdr "Arch-root boot image"
    bash "$K/build_root_safe.sh"
    ls -l "$BOOT_IMG" 2>/dev/null | awk '{print "  ", $9, $5, $6, $7, $8}'
}

# ----------------------------------------------------------------- modules --
cmd_modules() {
    need_root
    hdr "loadable modules"
    bash "$K/build_mod_full.sh"
    mkdir -p "$K/images/modules"
    cp -f "$O/drivers/net/wireless/mediatek/gen4m/wlan_6833_axi.ko" \
          "$K/images/modules/" 2>/dev/null
    ls -lh "$K/images/modules" | tail -n +2 | awk '{print "  ", $9, $5}'
}

# -------------------------------------------------------------------- net --
# Build the configfs gadget image (ACM + RNDIS) instead of the legacy g_serial
# one.  The safe image keeps working: flashing boot-own2.img restores it.
cmd_net() {
    need_root
    hdr "RNDIS network image"
    bash "$K/build_net_image.sh"
    ls -l "$K/images/boot/boot-net.img" 2>/dev/null | awk '{print "  ", $9, $5, $6, $7, $8}'
}

# ------------------------------------------------------------------ images --
cmd_images() {
    hdr "image tree"
    for d in boot rootfs probe modules; do
        printf '  %s/\n' "$d"
        ls -lh "$K/images/$d" 2>/dev/null | tail -n +2 | \
            awk '{printf "      %-24s %s\n", $9, $5}'
    done
    hdr "configs"
    ls -l "$K/configs" | tail -n +2 | awk '{print "  ", $9, $5}'
}

# ------------------------------------------------------------------- flash --
fb() { "$PT/fastboot.exe" "$@"; }

cmd_flash() {
    hdr "flash boot_b"
    fb devices || true
    fb flash boot_b "$BOOT_IMG" || exit 1
    fb --set-active=b || exit 1
    fb reboot || exit 1
    c_ok "flashed $BOOT_IMG and rebooted"
}

cmd_flash_root() {
    which=${1:-f2fs}
    img=$K/images/rootfs/arch-$which.img
    [ -f "$img" ] || { c_err "no such image: $img"; exit 1; }
    hdr "flash userdata with $which"
    c_warn "this erases the Android data partition"
    fb devices || true
    fb flash boot_b "$BOOT_IMG" || exit 1
    fb flash userdata "$img" || exit 1
    fb --set-active=b || exit 1
    fb reboot || exit 1
    c_ok "done"
}

# ------------------------------------------------------------------ ccache --
cmd_ccache() {
    ccache -s
}
cmd_clean_cache() {
    ccache -C && c_ok "ccache cleared"
}

cmd_clean() {
    need_root
    hdr "removing $O (config is kept)"
    cp -f "$O/.config" /root/backup-working/config.before-clean 2>/dev/null
    rm -rf "$O"
    c_ok "done - next build will be a full one"
}

case "${1:-}" in
    doctor)      cmd_doctor ;;
    deps)        cmd_deps ;;
    kernel)      cmd_kernel ;;
    root)        cmd_root ;;
    net)         cmd_net ;;
    modules)     cmd_modules ;;
    images)      cmd_images ;;
    flash)       cmd_flash ;;
    flash-root)  shift; cmd_flash_root "${1:-f2fs}" ;;
    ccache)      cmd_ccache ;;
    clean-cache) cmd_clean_cache ;;
    clean)       cmd_clean ;;
    *)
        sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
        ;;
esac
