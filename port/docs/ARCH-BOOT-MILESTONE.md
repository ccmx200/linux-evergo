# Milestone: Arch Linux ARM boots on the evergo

2026-10-24.  The phone now boots mainline Linux into a full Arch Linux ARM
userspace without any manual step:

```
   evergo   MT6833   mainline Linux   boot self-test
  kernel    7.3.0-rc4
  model     Xiaomi Redmi Note 11 5G
  cpu       8 cores
  memory    5750 MiB

-- usb -------------
[  ok  ] gadget: legacy serial owns the UDC, no network this boot
-- rootfs -------------
  /dev/sdc 122032 MiB, userdata at +12352 MiB
  loop0 -> /dev/sdc+12352 MiB
  mounting f2fs ...
[  ok  ] rootfs mounted (f2fs), 995 binaries, Arch Linux ARM
-- switch_root -------------
Arch Linux ARM 7.3.0-rc4 (ttyGS0)
evergo login: root (automatic login)
[root@evergo ~]#
```

Measured inside the running system:

| item | value |
|---|---|
| kernel | `Linux evergo 7.3.0-rc4 #3 SMP PREEMPT aarch64` |
| cpu | 8 cores, `CPU part 0xd05` (Cortex-A55 cluster), **no cpufreq** |
| memory | 5750 MiB total, 5356 MiB free at idle |
| root | `/dev/loop0  6.0G  2.4G  3.7G  40%  /` (read-write) |
| userspace | Arch Linux ARM, 995 binaries in `/usr/bin`, systemd up, root autologin on ttyGS0 |

## Why it works now

Three separate faults were fixed on the way here.

1. **The kernel config was truncated** (4973 → 2720 options), which turned
   `CONFIG_ARCH_MEDIATEK` off and removed the whole MediaTek platform branch,
   including the CONN driver.  Every boot then hung at the first screen and it
   looked like a hardware problem.  `build_safe.sh` now refuses a config with
   fewer than 4000 options.
2. **The init had no console**: it wrote to stdout, which was not the gadget
   serial, so nothing was visible.  It now resolves ttyGS0 on each write and
   mirrors everything to the display.
3. **`mount -o loop,offset=` does not work here.**  busybox mount accepts the
   option but the mount fails.  The fix is to attach the loop device by hand and
   mount it by name:

   ```
   losetup -o 12952010752 /dev/loop0 /dev/sdc
   mount -t f2fs -o rw /dev/loop0 /newroot
   ```

## Where userdata actually is

The stock scatter file gives it away:

```
partition_name:      userdata
physical_start_addr: 0x304000000     (12352 MiB, inside EMMC_USER)
partition_size:      0x280000000
```

`/dev/sdc` is the whole eMMC user area (122032 MiB).  Its GPT is damaged (LBA1
is zeroed), so the kernel creates no `sdcN` nodes and userdata has to be
addressed as an offset.  Verified by flashing a 1 MiB image containing
`EVERGOPROBE` and reading it back at that offset.

## Artifacts

| file | meaning |
|---|---|
| `kvm/images/boot/boot-arch-auto.img` | the image that produced the boot above |
| `kvm/images/boot/boot-own2.img` | same build, working copy |
| `kvm/images/boot/boot-net.img` | configfs ACM+RNDIS variant, untested on hardware |
| `kvm/images/rootfs/arch-f2fs.img` | the 6 GB F2FS rootfs, label `archroot` |
| `/root/backup-working/init_root.ok` | the init script that booted |
| `/root/backup-working/config.arch-ok` | the config that built it |

## What is still missing in the userspace

* **No cpufreq / OPP table** on MT6833, so the CPU stays at whatever LK set and
  there is neither frequency scaling nor thermal throttling.
* No MT6359 regulator or charger driver, no thermal zones, no suspend/resume,
  no Mali GPU driver, no modem/WiFi/BT.
* `boot-net.img` (USB RNDIS networking) is built but has not been flashed.
