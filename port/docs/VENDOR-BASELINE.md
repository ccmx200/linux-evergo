# Working baseline captured from the vendor Android kernel

Captured on the handset with the **stock Android kernel** (4.14.356-Evergo) and
`adb shell su -c dmesg` (KernelSU root).  This is the reference this port has
been missing: it shows what a CONN subsystem that actually works looks like.

Reproduce with: flash a stock boot image, boot Android with WiFi enabled, then
`adb shell su -c dmesg`.

## EMI reservation

```
reserve_memory_consys_fn: name: consys-reserve-memory, base: 0xbe000000, size: 0x500000
OF: reserved mem: initialized node consys-reserve-memory,
                  compatible id mediatek,consys-reserve-memory
```

The AP-side base differs per boot; what matters is that the CONN-side view is
always `0xf0088000` for the coredump region, i.e. `0xf0000000 + 0x88000`.

## Probe (driven by userspace, NOT at boot)

```
wmt_detect_driver_init: driver(major 154) installed success          # device_initcall
wmt_loader: wmt_detect_unlocked_ioctl:cmd ...                        # /dev/wmtdetect
wmt_loader: set current consys chipid (0x6833)
wmt_loader: mtk_wmt_probe: Clearing Connsys EMI ... 5242880 bytes
wmt_loader: consys_emi_mpi_set_region_protection:setting MPU
wmt_loader: consys_emi_set_remapping_reg:CONSYS_EMI_MAPPING dump (0x0000be00)
wmt_loader: consys_emi_set_remapping_reg:PERISYS_MAPPING   dump (0x00001000)
wmt_loader: consys_emi_coredump_remapping: physical 0xbe088000
wmt_loader: mtk_wmt_probe: cannot find consys pinctrl
wmt_loader: mtk_wmt_probe: ant swap pin number:-2
```

Note: the vendor also has **no consys pinctrl and no antenna-swap GPIO** on this
board, so those are correctly absent in the mainline port too.

## A-die detection (a full power-up/power-down of its own)

```
mtk_wcn_consys_detect_adie_chipid: CONSYS A-DIE DETECT start
polling_consys_chipid: retry(9)consys version id(0x100a0000)
polling_consys_chipid: consys HW version id(0x8a00)
polling_consys_chipid: consys FW version id(0x8a00)
polling_consys_chipid: consys configuration id(0x1)
consys_afe_reg_setting: No need to do afe
consys_adie_chipid_detect: A-die chip id = 6631
mtk_wcn_consys_detect_adie_chipid: Set a-die chipid = 6631
mtk_wcn_consys_detect_adie_chipid: CONSYS A-DIE DETECT finish
```

**The A-die is 0x6631 (MT6631, primary)** - confirming what this port now reads
from hardware.  The version id `0x100a0000` matches `CONSYS_IP_VER_ID` exactly,
and `0x8a00` is what `CONN_ON + 0x000` reports.

## The real power-on, and the three patches

```
mtk_wcn_consys_hw_pwr_on: CONSYS-HW-PWR-ON, start
mtk_wcn_consys_hw_reg_ctrl: CONSYS-HW-REG-CTRL(0x00000001),start
mtk_wcn_soc_rom_patch_dwn: [Rom Patch] emiInfo: emi_ap_phy_addr=0xbe000000
        emi_size=5242880 emi_phy_addr=0xf0088000
mtk_wcn_soc_rom_patch_dwn: [Rom Patch]Name=soc2_2_ram_bt_1_1_hdr.bin,
        EmiOffset=0x170000, Size=0xa7a10
mtk_wcn_soc_rom_patch_dwn: [Rom Patch]Name=soc2_2_ram_wifi_1_1_hdr.bin,
        EmiOffset=0x2a0000, Size=0x3ad8c
mtk_wcn_soc_rom_patch_dwn: [Rom Patch]Name=soc2_2_ram_mcu_1_1_hdr.bin,
        EmiOffset=0x0,      Size=0x2d630
```

Three blobs, three offsets - MCU at 0, BT at 0x170000, WiFi at 0x2a0000.  The
port now copies all three (it used to copy only the MCU one).

## The idle handshake, step by step

```
consys_polling_goto_idle: 0x18002600(0x0)      0x1800216c(0x0)  0x18007104(0x0)
consys_polling_goto_idle: 0x18002600(0x0)      0x1800216c(0x0)  0x18007104(0xf0000030)
consys_polling_goto_idle: 0x18002600(0x2)      0x1800216c(0x0)  0x18007104(0xf0003cea)
consys_polling_goto_idle: 0x18002600(0x1d1e)   0x1800216c(0x0)  0x18007104(0x22052)
mtk_wcn_consys_hw_reg_ctrl: CONSYS-HW-REG-CTRL(0x00000001),finish
mtk_wcn_consys_hw_pwr_on: CONSYS-HW-PWR-ON, finish(0)
```

Read this carefully: `0x18007104` is **CPUPCR, the CONN CPU's program counter**.
It starts at 0 and then *moves* (`0xf0000030` -> `0xf0003cea` -> `0x22052`) as the
CPU executes out of ROM, and the idle signature `0x1d1e` appears about **85 ms**
after the patch copies.

In this mainline port `0x18007104` reads **0 forever**, i.e. the CPU never
executes a single instruction.  That is the single fact that separates the two
systems.

## What the port now matches, and what it does not

Matches: EMI clear + remap, all 28 reg_ctrl steps, MTCMOS sequence, A-die id
(0x6631) and the resulting firmware flavour, all three patch blobs at the right
offsets, chip-id value, no pinctrl, no antenna GPIO.

Does not match: **the CPU never runs** (`0x18007104` stays 0, `0x18007100` reads
the unmapped pattern `0x87654321`), so `0x18002600` never becomes `0x1d1e`,
`nicpmSetDriverOwn()` times out and `wlan0` is never created.
