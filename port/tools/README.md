# kvm/ — toolchain for the evergo (Redmi Note 11T 5G / MT6833) mainline port

One entry point: **`go.sh`**.  Everything else in this directory is a helper it
calls, or history.

## Quick start

```powershell
W = wsl -d Ubuntu-24.04 -u root -- bash /mnt/c/Users/cuicanmx/Documents/deepseek-harness/default-workspace/kvm/go.sh

wsl -d Ubuntu-24.04 -u root -- bash …/kvm/go.sh doctor    # check the environment
wsl -d Ubuntu-24.04 -u root -- bash …/kvm/go.sh kernel    # build kernel + boot.img
wsl -d Ubuntu-24.04 -u root -- bash …/kvm/go.sh root      # build the Arch-boot image
wsl -d Ubuntu-24.04 -u root -- bash …/kvm/go.sh images    # list every image
wsl -d Ubuntu-24.04 -u root -- bash …/kvm/go.sh flash     # flash boot_b and reboot
```

## go.sh commands

| command | what it does |
|---|---|
| `doctor` | verifies toolchain, sources, config size, initramfs dirs, boot pieces, images, fastboot, ccache |
| `deps` | installs the build dependencies (ccache, cross gcc, dtc, …) |
| `kernel` | `build_safe.sh /root/initramfs-test` → `images/boot/boot-own2.img` |
| `root` | assembles `initramfs-root` (with `init_root`) and packs the Arch-boot image |
| `modules` | builds the loadable `.ko` files into `images/modules/` |
| `images` | prints the image tree with sizes |
| `flash` | fastboot `boot_b` ← `images/boot/boot-own2.img`, set slot b, reboot |
| `flash-root f2fs\|ext4` | also flashes the rootfs image to userdata (**erases Android data**) |
| `ccache` / `clean-cache` | cache statistics / clear it |
| `clean` | removes `out/` for a full rebuild (keeps the config) |

## Two boot images

| image | gadget | serial | USB network | use |
|---|---|---|---|---|
| `images/boot/boot-own2.img` | legacy `g_serial` | yes | no | **safe**: always recoverable, what works today |
| `images/boot/boot-net.img` | configfs ACM + RNDIS | yes | **yes** | try this first once it is verified |

`boot-net.img` disables `CONFIG_USB_G_SERIAL` and enables
`CONFIG_USB_CONFIGFS_{ACM,RNDIS}` so the init can build one gadget carrying both
a serial console and a `usb0` network adapter.  If it does not come up, flash
`boot-own2.img` again — that path is untouched.

## init_root: six stages

```
1  mounts + console       proc/sys/dev/configfs, output helper picks ttyGS0 or the display
2  POST screen            kernel, model, cpu, memory, storage, filesystem support
3  usb gadget             configfs ACM+RNDIS; falls back cleanly when g_serial owns the UDC
4  network                usb0 192.168.42.1/24 + `nc -l -p 2222 -e /bin/sh`
5  rootfs                 /dev/sdc at USERDATA_OFF via `loop,offset=`
6  switch_root            into Arch
```

Every stage prints `[  ok  ]` / `[ fail ]` and no stage can make a later one
unreachable: the rescue shell always ends up on ttyGS0.

## Getting a network instead of the slow serial

With `boot-net.img` the phone appears on the PC as a USB network adapter:

| side | address |
|---|---|
| phone (`usb0`) | `192.168.42.1/24` |
| PC | `192.168.42.2/24` (assign manually) |

```powershell
# Windows: find the RNDIS adapter and give it an address
Get-NetAdapter | Where-Object InterfaceDescription -match 'RNDIS|Remote NDIS'
netsh interface ip set address name="<adapter>" static 192.168.42.2 255.255.255.0
# then, from the initramfs (unencrypted shell)
Test-NetConnection 192.168.42.1 -Port 2222
```

Once Arch boots, its own `sshd`/`dropbear` gives a real encrypted session on the
same link — that is the point of the network: `gdbserver`, `scp`, package
installs, all faster than 921600 baud serial.

## Build speed


The build goes through **ccache** (`CCACHE_DIR=/root/.ccache`, 5 GB limit).

Measured on this machine with `out/` wiped before each run:

| build | time |
|---|---|
| cold (empty cache) | **830 s** (13.8 min) |
| warm (cache populated) | **60 s** (1 min) |

Important: the kernel top-level Makefile does `CC := $(CROSS_COMPILE)gcc`,
which **overrides the environment**, so `build_safe.sh` passes
`CC="$MAKE_CC"` on the make command line.  Setting `CC` in the environment
silently disables ccache — that mistake cost an 827 s build that should have
been 60 s.  To build without ccache: `MAKE_CC=aarch64-linux-gnu-gcc go.sh kernel`.

## Layout

```
kvm/
├── go.sh                  ← the entry point
├── README.md
├── configs/
│   ├── author.config      the known-good kernel config (4973 options)
│   └── known_good.config
├── images/
│   ├── boot/boot-own2.img 128 MB boot image for boot_b (AVB footer included)
│   ├── rootfs/arch-f2fs.img  6 GB F2FS Arch ARM rootfs, label archroot
│   ├── rootfs/arch-ext4.img  6 GB ext4 Arch ARM rootfs, label archroot
│   ├── probe/probe.img    1 MB marker image used to find userdata's offset
│   ├── modules/*.ko       vendor-side probes (connreg, consys_trc, wlan_trc …)
│   └── history/           every earlier boot image and dump (1.7 GB)
├── init_root              POST screen + switch_root into Arch
├── build_safe.sh          the actual build (refuses a truncated config)
├── build_root_safe.sh     Arch-boot image
├── build_full_config.sh   fallback build straight from author.config
├── build_mod_full.sh      module build
├── pack_now.sh            Image + initramfs + DTB → boot-own2.img
├── prep_ko.sh             strip/xz a .ko for the initramfs
├── use_initramfs.sh       point CONFIG_INITRAMFS_SOURCE and build
├── copy_dtb.sh            put the DTB where CONFIG_EXTRA_FIRMWARE looks
├── restore_config.sh      re-enable the MediaTek branch in a torn config
├── deploy.sh              full pipeline in one go
├── alarm_configure.sh     configure the unpacked Arch rootfs
├── send_cmds.ps1          drive the serial console with a serial-cmds/*.txt
├── serial-cmds/           202 recorded command lists
├── docs/                  reports and notes
├── ksu_mod/               KernelSU module that loads the early tracers
├── archive/               1053 historical scripts, grouped
└── agent/  vend/  zlogs/  scratch
```

## WSL environments

| distro | what lives there |
|---|---|
| **Ubuntu-24.04** | `/root/linux-evergo-new` (mainline 7.3.0-rc4), `out/`, `/root/initramfs-test`, `/root/initramfs-root`, `/root/phone` (DTB/ramdisk/boot_a pieces the packer needs), `/root/backup-working` (known-good Image, config, sources) |
| **archlinux** | `/root/kernel-lxc_xiaomi_mtk810_mt6833-resukisu` (vendor kernel, reference only), `/root/alarm` (Arch rootfs + image build), the vendor-side probe module sources |

Cross toolchain: `aarch64-linux-gnu-` lives in Ubuntu-24.04.

## Hard-won rules

* `make Image` does **not** rebuild the DTB — `build_safe.sh` runs `make dtbs`
  and then `copy_dtb.sh`, otherwise `CONFIG_EXTRA_FIRMWARE` fails with
  "empty firmware".
* `olddefconfig` is safe on `author.config`; it is *not* safe once the config
  has been truncated, so `build_safe.sh` checks the option count first
  (< 4000 → restore `configs/author.config`).
* `pack_now.sh` appends `/root/phone/author_dtb.bin`, not the freshly built DTB.
* userdata is **not** a device: it sits at eMMC offset `0x304000000`
  (= 12352 MiB = 12 952 010 752 bytes) inside `/dev/sdc`, mounted as
  `mount -t f2fs -o rw,loop,offset=12952010752 /dev/sdc /newroot`.
  `offset=` only works together with `loop`.  `init_root` carries it as
  `USERDATA_OFF`.
* `fastboot erase userdata` does not work on this LK ("Erase error").
* Never run the old STP ROM-patch pump (`/proc/conn_patchdl`): it wedges the
  CONN block and hangs the handset.  The code has been removed.
