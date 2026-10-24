# evergo / MT6833 mainline port - handoff

State as of 2026-10-24, written so the next session can start cold.

## Where the project stands

### Working
* **Arch Linux ARM boots automatically** into systemd: POST screen → loop-mount
  userdata → `switch_root` → `root` autologin on ttyGS0.
  See `wifi-bt/patches/applied/arch-boot/ARCH-BOOT-MILESTONE.md`.
  Measured: `Linux evergo 7.3.0-rc4 aarch64`, 8 cores, 5750 MiB RAM,
  `/dev/loop0 6.0G 40% /` read-write, 995 binaries, no cpufreq.
* mainline 7.3.0-rc4 boots on the phone (boot_b), USB-gadget serial console on
  COM20 at 921600 8N1, display works.
* CONN power-on, ROM patches, EMI remap, entry registers, IRQ, DRIVER OWN.
* **STP link verified**: clk_en/clk_138/clk_26/clk_dis (0x08), WMT_RESET (0x07),
  set_stp (0x04) and set-chip-id (0x02) all answered, payloads byte-identical to
  the vendor's event definitions.
* inband ECO read returns HW[0x8a00] SW[0x8a00], same as the vendor.
* Boot self-test screen (`init_root`) prints kernel/model/cpu/memory/storage/fs
  to the display and the serial, then tries to switch_root into Arch.

### Not working yet
* `INIT_CMD_ID_WIFI_START` gets no answer (28525 empty polls); only firmware-side
  opcodes (0x14 ant-swap / RF-cal, and WIFI_START) are silent while ROM-side ones
  answer.  Every observable parameter matches the vendor - see the exclusion
  table in `SESSION-2.md`.
* `boot-net.img` (USB RNDIS networking) is built but has not been flashed.

## The one mistake to not repeat

`out/.config` was silently truncated from 4973 to 2720 options, which turned
`CONFIG_ARCH_MEDIATEK` off and removed the whole MediaTek branch - including the
CONN driver.  Every boot then hung at the first screen and it looked like a
kernel regression.  `build_safe.sh` now refuses to build a config with fewer
than 4000 options and restores `phone/author.config`.

## Key facts about the hardware (measured, not guessed)

| item | value |
|---|---|
| storage | **eMMC**, 122032 MiB, exposed as `/dev/sdc` (sda/sdb are 4 MiB UFS_BOOT) |
| eMMC partition table | GPT at LBA1 is zeroed, so the kernel creates no `sdcN` nodes |
| userdata | eMMC offset `0x304000000` (12352 MiB, 12 952 010 752 B), 10 GiB in the scatter |
| RAM | 5750 MiB usable |
| CPU | 6x A55 + 2x A76, no OPP table in the DT (no DVFS yet) |
| panel | K16A, DSI 3-trio C-PHY, backlight LM36273 on i2c6, touch NT36672C on spi1 |
| serial | USB gadget ACM, ttyGS0, 921600 8N1 |
| fastboot | vol-down + power; `boot-fastboot` in misc does not work from mainline |

## Next steps, in order

1. **Flash `boot-net.img`** and get USB RNDIS networking up, so the console stops
   being the bottleneck (see `kvm/README.md`, "Getting a network").
2. **Restore the eMMC GPT** if Android is ever wanted back: `pgpt`/`sgpt` from
   the stock scatter, or reflash the stock firmware.
3. **CPU DVFS**: the vendor table lives in
   `base/power/cpufreq_v1/src/mach/mt6853/mtk_cpufreq_opp_table.h`
   (A55 2.0 GHz → 500 MHz, A76 2.21 GHz → 725 MHz, 1.00 V → 0.65 V); mainline
   needs `operating-points-v2` plus an ARM mux in `clk-mt6833-topckgen.c`, which
   does not exist yet.  Confirmed at runtime: there is no `/sys/devices/system/
   cpu/cpu0/cpufreq`.
4. **WiFi**: the remaining unknown is why the MCU does not answer
   firmware-side opcodes.  The vendor's own 0x14 traffic happens at ~6.4 s, before
   any module can load; the way to see it is to rebuild the vendor kernel with
   `-D WMT_DBG_SUPPORT=1` and drive the power-on from a module of our own.

## Where the evidence lives

* `wifi-bt/patches/applied/wlan-port/STATE.md` - the long-form log: 14 fixed
  defects, register values, STP findings.
* `wifi-bt/patches/applied/wlan-port/SESSION-2.md` - the exclusion matrix and the
  vendor-comparison results.
* `wifi-bt/patches/applied/wlan-port/FINAL-STATE.md` - the earlier handoff.
* `wifi-bt/patches/applied/wlan-port/vendor-*.txt` - captured vendor traces.
* `kvm/README.md` - the toolchain index.
* `/root/backup-working/` in Ubuntu-24.04 - working Image, config and sources.

## Tooling

Everything now goes through one script, `kvm/go.sh` (run it inside WSL):

```powershell
wsl -d Ubuntu-24.04 -u root -- bash /mnt/c/Users/cuicanmx/Documents/deepseek-harness/default-workspace/kvm/go.sh <cmd>
```

| command | use |
|---|---|
| `doctor` | first thing in a new session: checks toolchain, config, initramfs, images, fastboot |
| `kernel` | rebuild kernel + `images/boot/boot-own2.img` (ccache accelerated) |
| `root` | rebuild the Arch-boot image |
| `flash` / `flash-root f2fs\|ext4` | flash boot only / boot + rootfs |
| `images` | list the image tree |

Images live under `kvm/images/` (`boot/`, `rootfs/`, `probe/`, `modules/`,
`history/`); kernel configs under `kvm/configs/`.

