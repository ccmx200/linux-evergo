# linux-evergo — mainline Linux on the Xiaomi Redmi Note 11T 5G (MT6833 / evergo)

Working tree of the mainline port for the MT6833 "evergo".  The phone boots
**Arch Linux ARM** from an F2FS image in the eMMC userdata area, with a USB
gadget serial console and a boot self-test screen.

```
   evergo   MT6833   mainline Linux   boot self-test
  kernel    7.3.0-rc4
  model     Xiaomi Redmi Note 11 5G
  cpu       8 cores      memory 5750 MiB
-- rootfs --
  loop0 -> /dev/sdc+12352 MiB
[  ok  ] rootfs mounted (f2fs), 995 binaries, Arch Linux ARM
-- switch_root --
Arch Linux ARM 7.3.0-rc4 (ttyGS0)
evergo login: root (automatic login)
```

## Layout

```
kernel/     the port's files, at their upstream paths
docs/       milestone notes, register findings, handoffs
tools/      build / flash / rootfs scripts, init script, serial command lists
config/     author.config - the known-good kernel config (4973 options)
```

`kernel/` is an overlay: copy it over a Linux 7.3-rc4 tree (`cp -a kernel/* linux/`)
and it replaces/extends the files the port touches.

## What the port adds

| path | what |
|---|---|
| `drivers/soc/mediatek/mt6833-conn.c` | CONN power-on, EMI remap, ROM patch download, STP/WMT handshake, `/proc` diagnostics |
| `drivers/misc/mediatek/conn-mt6833/` | the vendor connectivity stubs needed by the above (UPower, BTIF, chip id) |
| `drivers/net/wireless/mediatek/gen4m/` | the gen4m WiFi driver (SOC2_1X1), including the WMT shim |
| `arch/arm64/boot/dts/mediatek/mt6833.dtsi` | CPU OPP tables + ARM PLL clocks, and the rest of the SoC description |
| `arch/arm64/boot/dts/mediatek/mt6833-xiaomi-evergo.dts` | the board: 6 GB RAM, DSI K16A panel, NT36672C touch, eMMC, UFS, USB, PMIC, CONN/WiFi windows |

## Booting into Arch

userdata is **not** a device on this phone.  The stock scatter says:

```
partition_name:      userdata
physical_start_addr: 0x304000000     (12352 MiB inside EMMC_USER)
```

so the rootfs is attached with a loop device and mounted by name:

```sh
losetup -o 12952010752 /dev/loop0 /dev/sdc
mount -t f2fs -o rw /dev/loop0 /newroot
```

`busybox mount -o loop,offset=` silently fails here, which cost a day of work.

## Building

```sh
# inside WSL, as root
cp config/author.config  <linux>/out/.config
make -C <linux> O=out -j8 dtbs  CC="ccache aarch64-linux-gnu-gcc"
make -C <linux> O=out -j8 Image CC="ccache aarch64-linux-gnu-gcc"
tools/pack_now.sh                    # Image + initramfs + dtb -> 128 MB boot.img
fastboot flash boot_b images/boot/boot-own2.img
```

`tools/go.sh` wraps all of it (`doctor`, `kernel`, `root`, `net`, `flash`,
`images`).  The kernel Makefile assigns `CC := $(CROSS_COMPILE)gcc`, so ccache
only works when `CC=` is passed **on the make command line** — a full rebuild
drops from 830 s to 60 s with a warm cache.

## Status

Working: boot, display, USB serial console, eMMC/UFS enumeration, CONN power-on,
ROM patch download, STP link (clk/reset/set_stp/chip-id all answered), Arch
userspace with systemd.

Not working yet:
* `INIT_CMD_ID_WIFI_START` — firmware-side opcodes stay silent, so the MCU never
  answers `WIFI_START`.  Every measurable parameter matches the vendor; see
  `docs/SESSION-2.md` for the exclusion list.
* CPU DVFS is described in the DT (OPP tables + ARM PLL clocks) but has not been
  verified on hardware yet.
* No MT6359 regulator/charger, no thermal zones, no suspend, no GPU driver.

## Credits

Mainline kernel, MediaTek and MTK connectivity upstream work, and the gen4m
driver from the vendor BSP.  Everything in `docs/` was measured on the device.
