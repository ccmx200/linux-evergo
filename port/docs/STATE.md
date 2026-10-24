# Where the WiFi port stands

Everything below is measured on the handset.

## Working

| thing | evidence |
|---|---|
| CONN power-on sequence | reaches the ROM idle loop: `0x18002600 = 0x00001d1e` |
| CONN CPU executing | `CPUPCR (0x18007104) = 0x00022052`, the same value the working vendor system shows |
| WiFi HIF window | `0x18007030 = 1`, `0x1800712c = 0x00430000`, `0x18007004/08` match the vendor |
| ROM patches in EMI | MCU 185904 B @ +0x0, BT 686608 B @ +0x170000, WiFi 241036 B @ +0x2a0000 |
| WiFi firmware body in EMI | 549904 B @ +0x2f0000, 168880 B @ +0x386000 |
| EMI window alignment | CONN now uses the same 6 MB reservation as the WLAN driver, `0xbe000000`, and programs `CONSYS_EMI_MAPPING = 0xbe00` (the vendor's value) |
| WLAM interrupt | `wifi@18000000` maps to virq 159 (MT_SYSIRQ 377); a probe handler fires on it |
| firmware identification | `WIFI_RAM_CODE_soc2_2_1_1.bin`, manifest `...MT6631_ASIC_MINIROM...`, ADIE `0x6631` |
| DRIVER_OWN | succeeds in ~2 ms when the probe is re-run right after an explicit off/on cycle |

## Most recent state

    DRIVER OWN Done[2022 us]                  <- ownership, reliable now
    FWDL-DBG: stage enter, 5 sections
    FWDL-DBG: sec[0..2] section config ACKed
    FWDL-DBG: sec[3] EMI copy 549904 -> 0xbf000000 + 0x2f0000
    FWDL-DBG: sec[4] EMI copy 168880 -> 0xbf000000 + 0x386000
    FWDL-DBG: ram_entry=0x73000 -> start cmd enable=1 pda=0, dl status=0
    halRxWaitResponse: CONN_HIF_ON_DBGCR01[0x22052]
    nicRxWaitResponseByWaitingInterval: halRxWaitResponse fail!status C0000001
    Fail reason: 5 (RAM_CODE_DOWNLOAD_FAIL)

so the whole download succeeds and `INIT_CMD_ID_WIFI_START` is sent, but the MCU
never answers.  State read back afterwards:

    0x180C1140 = 0x00000004    bit2 (N9_DONE) set, bit1 (INIT_DONE) clear
    0x18002600 = 0x00001d1e    MCU still in the ROM idle loop
    0x18007104 = 0x00022052    same CPUPCR as the working vendor system

A/B on the EMI window (`emi_use_wifi`):

| emi_use_wifi | CONN remap | result |
|---|---|---|
| 0 | `bf000000` | all 5 sections download, `dl status=0`, WIFI_START sent, then the response times out |
| 1 | `be000000` | dies at section 0 with `Chip No Ack` |

`bf000000` is therefore the window the register path needs, and the firmware body
now follows it (`FWDL-DBG: EMI base from CONN: 0xbf000000`); the RX/TX rings stay
at `be008000`, which is fine because those are DMA physical addresses.

## Fixed along the way (this round)

* `shim` forces a real off/on (`mt6833_conn_force_cycle`) before the first probe
  and before every retry - this is what turned DRIVER_OWN from a 2 s timeout into
  2 ms.
* a rejected inband ECO-version read no longer aborts `wlanAdapterStart`
  (the ROM answers "address incorrect" for both `0x80021000` and `0x81021000`).

## Breakthrough: the WMT/STP sequence wakes the MCU

`/proc/conn_stp` sends five commands copied byte for byte from the vendor's
`wmt_ic_soc.c`, framed for STP mandatory mode and written to BTIF (0x1100C000):

    WMT_SET_MCU_CLK_EN_CMD    01 08 10 00 01 01 00 01 34 03 00 80 00 00 01 00 ff ff ff ff
    WMT_SET_MCU_CLK_138_CMD   01 08 10 00 01 01 00 01 0c 01 00 80 59 4d 84 00 ff ff ff ff
    WMT_RESET_CMD             01 07 01 00 04
    WMT_SET_CHIP_ID_CMD       01 02 05 00 11 33 68 00 00
    WMT_QUERY_A_DIE_CMD       01 02 01 00 12

Measured result:

    stp: LSR=0x60                     (THRE|TEMT, transmitter ready)
    stp: clk_en sent (26)
    stp: clk_en resp 14 bytes         <- the MCU answers
    stp: clk_138 resp 14 bytes
    stp: reset resp 11 bytes
    stp: reset evt 80 40 05 00 02     <- STP response frame
    stp: done, CONN_ON+0x600=0x1d1e CPUPCR=0x000000cc ON_MISC=0x00000013

CPUPCR moves from 0x22052 (ROM idle) to 0x000000cc and ON_MISC from 0x4 to
0x13 - **INIT_DONE (bit1) sets**.  The MCU leaves the ROM idle loop and starts
executing; this is the first time this port has got the MCU to run anything.

## Where it stops now

With the sequence wired in (`stp_before_probe=1`, /proc/conn_cycle runs
power-on -> STP -> probe), the run looks like this:

    stp: clk_en   resp 14 bytes
    stp: clk_138  resp 14 bytes
    stp: reset    evt 80 40 05 00 02
    stp: set_stp  resp 12 bytes
    stp: rf_cal   resp 0 bytes      <- no answer, even with a 4 s wait
    stp: done, CONN_ON+0x600=0x1d1e CPUPCR=0x000000cc ON_MISC=0x00000013

    DRIVER OWN Done[2029 us]
    FWDL-DBG: ram_entry=0x73000 -> start cmd enable=1 pda=0, dl status=0
    wlanConfigWifiFunc: FW_START CMD send, waiting for RSP
    wlanConfigWifiFunc: FW_START EVT failed
    wlanAdapterStart: Fail reason: 5

State afterwards: ON_MISC back to 0x4, CPUPCR stays 0xcc.

So the MCU answers four of the five WMT commands, leaves the ROM idle loop and
sets INIT_DONE, but the RF-calibration command is not answered, and the driver's
`FW_START` still never receives `INIT_EVENT_ID_CMD_RESULT`.

The driver's readiness test wants `0x6` (INIT_DONE | N9_DONE); STP leaves `0x13`,
i.e. N9_DONE (bit2) is clear, so `wlanDownloadFW` decides the WiFi is not running
and downloads the firmware again over the code STP just brought up.

## What the vendor does that this port still skips

In order, after our five commands, `mtk_wcn_soc_sw_init` still performs:

1. the **ROM patch download over STP** (`mtk_wcn_soc_patch_dwn`, address command
   plus 1000-byte fragments) - this port loads the patches through EMI instead,
   which reaches the ROM idle loop but may not be what the patched code expects;
2. `set_mcuclk_table_2` (drop the MCU clock back to 26 MHz, then disable the
   override);
3. `wmt_init_wifi_config` - builds the command from `WMT.cfg`, which is parsed by
   userspace (`WMT_IOCTL_GET_WMT_CONF`); nothing in this port parses it, and the
   initramfs does have `WMT_SOC.cfg`;
4. the EPA/ELNA/coex/antenna-swap scripts;
5. `mtk_wcn_soc_calibration()`.

The WiFi config (step 3) is the most likely reason calibration gets no answer
and `FW_START` no result: the firmware that STP starts is the WMT/connsys image,
and the WiFi side is configured by those later commands.



| check | result |
|---|---|
| `0x18002508` (WiFi ROM-patch entry) | `0xf02a0000` - correct |
| `0x18002504` (BT ROM-patch entry) | `0xf0170000` - correct |
| FWDL ring 3 base | `0xbe006000`, count 512 - correct |
| FWDL ring 3 CIDX/DIDX at failure | 14 / 14 - the MCU consumed every entry |
| `ram_entry` | `0x73000`, which is literally region 0's `u4Addr` in the firmware tailer (ucFeatureSet 0x27 = encrypted + VALID_RAM_ENTRY) |
| `0x180C1140` | 0x4 = N9_DONE set, INIT_DONE clear |

So the command reaches the MCU and is consumed, the entry addresses are right,
and the start address matches what the firmware tailer asks for.  What is left is
the code the ROM should run from EMI offset 0x2a0000 (the WiFi ROM patch).

**Layout note worth acting on**: the WiFi ROM patch is 241036 bytes at EMI
offset 0x2a0000, i.e. it ends at 0x2fad8c, while the firmware body is copied to
0x2f0000 and runs to 0x376410.  The two overlap by 0xad8c (44 428 bytes), and the
body is copied last, so it overwrites the tail of the ROM patch.  The vendor's
numbers (Size=0x3ad8c at EmiOffset=0x2a0000) imply the same overlap, so either
the tail is dead padding or the vendor's copy order differs.

Next experiments, cheapest first:

1. capture EMI+0x2a0000 before and after the firmware body copy to see exactly
   what the overlap destroys;
2. try copying the ROM patches *after* the firmware body (a runtime flag, no
   flash needed if wired into /proc/conn_cycle);
3. dump the MCU SRAM at 0x73000 after the start command to see whether the
   encrypted region 0 landed and decrypted.

## Vendor comparison (measured on Android)

* every CONN register reads `deadfeed` from a foreign module **even when WiFi is
  running**, so `deadfeed` only means "access not granted to this master"; it is
  not a fault indicator.
* the working vendor dmesg shows `hif_thread` and `DRIVER OWN Done[3093 us]`,
  `wlan0` and `p2p0` up.



## The ten defects found and fixed so far

1. **ROM patch header is 48 bytes**, not 32 - the vendor log's Size fields
   (0x2d630 / 0xa7a10 / 0x3ad8c) match `file_size - 48` exactly.
2. **CONN bus/hif clock gate** - INFRA_AO+0x080 bits 0x88800000 were never set;
   without them `0x18007030` reads `0xdeadfeed`.
3. **Idle poll was 20-40 ms** instead of the vendor's 200 ms budget; the
   signature appears ~85 ms after reset release.
4. **Two MT6895-only WFSYS writes** (`0x18000000` key write, `0x18000010` CPU
   reset) are not used on MT6833; both are now off by default.
5. **`wlanDownloadEMISection()` was compiled out** (`CFG_MTK_ANDROID_EMI=0`), so
   the 718 KB firmware body was silently dropped while the function returned
   success.
6. **`gConEmiPhyBaseFinal` was zero** - `gl_init.c` sets it from
   `gConEmiPhyBase`, which nothing fills in this build; it is now resolved from
   the CONN driver at the point of use.
7. **Two different EMI reservations** - the CONN driver used `0xbf000000` while
   the WLAN driver allocates from `0xbe000000`, so the firmware was written to
   one place and fetched from another.
8. **The 6 MB EMI wipe destroyed the WLAN rings**; `no_clear` defaults to 1 now.
9. **The WLAN driver is a module** (`CONFIG_MTK_WLAN_GEN4M=m`) and the built-in
   CONN driver reaches its replay hook through a function pointer.
10. **The EMI copy needed the WLAN driver's own window**, not the CONN node's.

## Debug surface (all live, no rebuild)

* `/proc/conn_cycle` - re-run power-on (`replay_probe=1` also re-runs the WLAN probe)
* `/proc/conn_sweep` - a list of `steps` masks in one boot
* `/proc/conn_reg` - arbitrary 32-bit register read/write
* `/proc/conn_wmt` - BTIF bring-up + `WMT_RESET` frame
* module params: `steps`, `patch_hdr`, `patch_mode`, `no_clear`, `idle_poll_ms`,
  `infra_cg_extra`, `wf_pwr_ctl_write`, `wf_cpu_rst_write`, `power_cycle`,
  `replay_probe`

## Useful facts about the platform

* misc partition: LBA 64, byte offset 32768.  MTK LK accepts the AOSP bootloader
  message commands `boot-fastboot`, `boot-recovery`, `reboot-bootloader`,
  `reboot-recovery`, `reboot-fastboot` - **not** `bootonce-bootloader`.  It does
  not clear the command afterwards, so a pristine copy of misc must be restored
  (`kvm/phone/misc_clean.img`).
* `/proc/iomem` shows the two 5-6 MB reservations: `be000000-be5fffff` (WLAN) and
  `bf000000-bf4fffff`.
* The WLAN driver's ring dump lives in `halShowPdmaInfo()` output:
  `Rx Ring / Idx / Reg / Base / Cnt / CIDX / DIDX`.

## Hard blocker: the STP patch download hangs the machine

`/proc/conn_patchdl` sends the WiFi ROM patch (241036 bytes, 242 fragments of
1000) to the MCU over BTIF using the vendor's byte formats:

    address command  01 01 05 00 04 <addr32>      evt 02 01 01 00 00
    fragment         01 01 <len_lo> <len_hi> <flag> + data
                     flag 1 first / 2 middle / 3 last, len = 1 + fragment size

Both versions - the first with blocking 300 ms waits inside the /proc write
handler, the second with a workqueue and polled non-blocking receives - **hang
the whole phone** part way through: COM20 disappears, no fastboot, no adb, and
only a forced power cycle recovers.  It is the CONN/MCU side that locks up, not
the loop shape: at some fragment the MCU stops consuming and the AXI access to
the CONN wedges the AP.

So "send the ROM patch the way the vendor does" needs the rest of the WMT/STP
state machine (flow control, fragment acknowledgement, PSM/bit-rate handling),
not a raw byte pump.  Treat a raw pump as a dead end.

## Best known-good configuration

`no_clear=1`, `emi_use_wifi=0`, `force_cycle` and the five-command STP
sequence: the MCU answers four WMT commands, leaves the ROM idle loop
(CPUPCR 0xcc), sets INIT_DONE (ON_MISC 0x13), and `FW_START` still gets no
`CMD_RESULT`.  Do not run `/proc/conn_patchdl` on a device whose state you
care about.

## Latest results (after the stack-overflow fix)

Two hard hangs were my own bug: `btif_tx()` declared `u8 frame[64]` and then
copied a 1005-byte ROM patch fragment into it, smashing the kernel stack.  With
a kmalloc'd frame the device survives a full patch download attempt.

With the vendor's ordering (clock -> patch -> reset -> chipid -> adie -> stp ->
calibration) the log is:

    stp: clk_en   resp 14 bytes
    stp: clk_138  resp 14 bytes
    patchdl: 241036 bytes, 242 fragments -> 0xf02a0011
    patchdl: addr evt 0 bytes          <- no answer, and correctly so
    patchdl: sent 0 of 242 fragments
    stp: reset evt 80 40 05 00 02
    stp: set_stp resp 12 bytes
    stp: rf_cal  resp 0 bytes

**The patch download over STP is a confirmed dead end**: the vendor's
`consys_get_options()` returns `OPT_DISABLE_ROM_PATCH_DWN` for MT6833, so
`mtk_wcn_soc_patch_dwn()` never runs there either.  The MCU ignoring the patch
address command is the *correct* behaviour.

## The one remaining difference against Android

| | Android | this port |
|---|---|---|
| inband read of TOP_HVR (0x80021000) | returns 0x8a00 | "Event reports address incorrect" |
| firmware start handshake | 77 ms, success | 3 s timeout |

The MCU *parses* our command (it answers with an error event) but rejects the
address.  A direct read of the AP alias 0x18021000 returns 0, so that is not the
translation either.

Everything else is now excluded by measurement or by source: EMI window, EMI
copy, entry registers (0x18002508 = 0xF02a0000), ROM patch layout and 48-byte
header, ring configuration, interrupt (SPI 377 -> virq 159, fires), driver own
(2 ms), the two MT6895 WFSYS writes, `consys_wifi_ctrl_setting` /
`consys_wifi_ctrl_switch_conn_mode` (both no-ops for a primary MT6631 A-die),
and the STP patch download (disabled on this chip).

## Vendor boot reference

`vendor-boot-wifi.txt` holds the captured Android boot log: wmt_loader powers
CONN at 6.4 s, wmt_core_hw_check reports chip 0x6833, calibration runs at 6.8 s,
and wlanProbe at 16.7 s does DRIVER OWN (3 ms), reads Chip ID E1 HW/SW 0x8a00,
downloads the firmware and succeeds in 77 ms.

## Why the vendor power-on cannot be captured (this round)

Three routes were tried and all closed today:

1. **KernelSU post-fs-data** loads the tracers at ~7.7 s, but `wmt_loader`
   powers CONN on at ~6.05 s.  The hook is simply too late.
2. **Overlaying `/vendor/bin/wmt_loader`** with a delaying script does not
   work: KernelSU's overlay only covers `/system`; `/vendor/bin/wmt_loader`
   was still the original ELF after the reboot.
3. **Userspace power cycle** via `WMT_IOCTL_FUNC_ONOFF_CTRL`
   (`_IOW(0xa0,6,int)` = 0x4004A006, arg `0xDEAD000<<4 | 3` for off and
   `0xBEEF000<<4 | 3` for on) returns success but does nothing, because the
   vendor's `common/Kbuild` sets `-D WMT_DBG_SUPPORT=0` so the whole handler
   body is compiled out.

What is still needed to see the vendor's power-on: either rebuild the vendor
kernel with `WMT_DBG_SUPPORT=1` (same source tree, `O=out` already
configured), or put the tracer into `vendor_boot` so it loads during early
init.

## Vendor wlanProbe trace (captured, with return values)

`vendor-wlan-trace.txt`: the complete call order with kretprobe return values.
Everything succeeds:

    wlanAdapterStart 0, halSetDriverOwn 0/1, wlanSetChipEcoInfo 0,
    halRxWaitResponse 0 x4, wlanDownloadFW 0, wlanImageSectionConfig 0 x3,
    halRxWaitResponse 0 x3, wlanConfigWifiFunc 0, halRxWaitResponse 0,
    wlanConfigWifiFunc 0, wlanDownloadFW 0, wlanAdapterStart 0, wlanProbe 0

The port's identical calls return failure, so the difference is MCU state, not
driver logic.

## KSU module

`earlycap-kSU.zip` (also installed at /data/adb/modules/earlycap): loads
`consys_trc.ko` and `wlan_trc.ko` at post-fs-data and starts both traces.
`replay.sh` inside it retries the power path.  Note `setenforce 0` is needed
for the /proc nodes to be writable from an adb root shell.

## The complete vendor sequence (captured 2026-10-18, reproducible)

Using a module that calls the *exported* `mtk_wcn_wmt_func_off/on(WMTDRV_TYPE_WIFI)`
(the ioctl path is compiled out by `-D WMT_DBG_SUPPORT=0`), the vendor's full
power-on can be replayed on demand.  With the tracers loaded the captured order is:

    mtk_wcn_consys_hw_pwr_off
    mtk_wcn_consys_hw_reg_ctrl
    consys_hw_power_ctrl
    consys_clock_buffer_ctrl
    consys_set_if_pinmux
    mtk_wcn_consys_hw_pwr_on
        consys_set_if_pinmux
        consys_clock_buffer_ctrl
        consys_hw_reset_bit_set        (assert)
        consys_hw_power_ctrl
        polling_consys_chipid
        consys_acr_reg_setting         (no-op on MT6833)
        consys_afe_reg_setting         (no-op on MT6833)
        consys_set_xo_osc_ctrl
        consys_wifi_ctrl_setting       (no-op for a primary MT6631)
        consys_bus_timeout_config
        consys_hw_reset_bit_set        (release)
        consys_wifi_ctrl_switch_conn_mode (no-op for a primary MT6631)
        consys_hw_vcn_ctrl_after_idle  (PMIC LP mode, RC mode only)
    wmt_core_stp_init
        wmt_core_hw_check
        mtk_wcn_soc_sw_init
            wmt_init_wifi_config       (skipped: no wifi_config in WMT_SOC.cfg)
            wmt_stp_init_epa           (skipped: coex_wmt_ant_mode_ex != 1)
            wmt_stp_init_wifi_ant_swap
            mtk_wcn_soc_calibration
    hifAxiProbe
    wlanProbe

**wlanProbe is the last step**, not the first thing after power-on.

Register state measured at the end of `mtk_wcn_consys_hw_pwr_on`:

    0x18002600 = 0x00001d1e   ROM idle
    0x18007104 = 0x00022052   CPUPCR
    0x18007000 = 0x00030001
    0x18002504 = 0xf0170000 / 0x18002508 = 0xf02a0000
    0x180c1140 = 0x00000000   (still zero at this point)
    0x18000000 = 0x42424242 / 0x18000010 = 0x00000000
    0x180c1168 = 0x00000001

which is **identical to this port** after its own power-on, so the power-on
sequence itself is not the problem - the missing part is everything that runs
between it and the probe.

## STP command behaviour observed

Answered by the MCU: clk_en (opcode 0x08), clk_138 (0x08), WMT_RESET (0x07),
set_stp (0x04).
Not answered: antenna switch (0x14 sub 0x07) and RF calibration (0x14 sub 0x01),
and the ROM patch address command (0x01) - the last one correctly, because
MT6833 sets OPT_DISABLE_ROM_PATCH_DWN.

## Tooling built today (vendor side, all loadable modules)

* `connreg.ko`   - register window dumper
* `consys_trc.ko`- 27 kprobes over the CONN/WMT bring-up + register dump at pwr_on completion
* `wlan_trc.ko`  - 17 probes over the WiFi driver with kretprobe return values
* `btifcap.ko`   - BTIF/STP byte capture
* `connpwr.ko`   - calls mtk_wcn_wmt_func_off/on(WIFI) directly
* `connonoff`    - userspace ioctl attempt (dead: WMT_DBG_SUPPORT=0)
* `earlycap-kSU.zip` - loads the tracers at post-fs-data

## Late findings (18:00-19:00) - vendor parameter comparison

A module hooking the vendor driver's own functions produced the ground truth for
every value this port had been guessing at.  **All of them match the port
exactly**:

    wlanAccessRegister  addr=0x80021000 / 0x80021004   (ECO read)
    wlanImageSectionConfig 0x73000/3504, 0x2014400/192, 0x40100000/9824, mode 0x8000000f
    wlanDownloadEMISection dest=0xf02f0000 len=549904, dest=0xf0386000 len=168880
    wlanConfigWifiFunc     enable=1 addr=0x73000 pda=0

and the EMI offset is (dest & 0xFFFFFF), i.e. 0x2f0000 and 0x386000 - precisely
what the port writes.  The firmware body placement is therefore correct.

The port's ECO read now succeeds and returns HW[0x8a00] SW[0x8a00], the same as
the vendor, after the inband address was corrected to **0x80021000** (it had
been changed to 0x81021000, which the MCU rejects).

## EMI window

The vendor keeps everything in **0xbe000000**, 5 MB:

    reserve_memory_consys_fn: consys-reserve-memory base 0xbe000000 size 0x500000
    consys_emi_set_remapping_reg: CONSYS_EMI_MAPPING = 0x0000be00
    mtk_wcn_soc_rom_patch_dwn: emi_ap_phy_addr=0xbe000000 emi_size=5242880

The port's device tree allocated the region dynamically and landed on
0xbf000000, so emi_base_fixed was added to force 0xbe000000.

## Device tree gotchas hit while doing this

* `make Image` does **not** rebuild the dtb; `make dtbs` must be run too.
* `pack_now.sh` appends `/root/phone/author_dtb.bin`, not the freshly built
  dtb, so a dtb change only takes effect after copying it there.
* `axiGetRsvMemSizeRsvedByKernel()` reads a **`size` property**, not `reg`;
  pinning the WiFi node with `reg` makes `mtk_axi_probe` fail with -22 before
  the firmware is ever touched.  Keep `size` on the WiFi node and pin only the
  CONN node, then let `emi_base_fixed` line the two windows up.

## State at the end of the session

Working:

  * CONN power-on healthy: `CONN CPU idle check: CONN_ON+0x600 = 0x00001d1e`
  * ROM patches, EMI remap (0xbf000000), entry registers, IRQ all fine
  * the WiFi driver now completes its own initialisation and reaches FW_START
    (the -EINVAL chain is fixed: the reservation must be a "shared-dma-pool"
    with a 'size' property, shared by both drivers)
  * ECO read returned HW[0x8a00] SW[0x8a00] - matching the vendor - in the build
    where the WiFi driver did not yet share the CONN window

Not working:

  * with the WiFi driver sharing 0xbf000000, the inband ECO read returns 0 and
    FW_START fails within 27 ms (it used to take the full 3 s timeout), i.e. the
    MCU is not answering at all
  * the CONN power-on completes at ~21 s while the WiFi probe runs at ~19.6 s,
    so the probe may be racing the CONN bring-up

Next steps worth taking:

  1. serialise the two: make the WiFi probe wait until the CONN driver reports
     power-on complete (conn_power_on_done flag or a completion);
  2. re-measure the ECO read in that serialised configuration - if it returns
     0x8a00 again, FW_START is the only thing left;
  3. if FW_START still fails with ECO working, compare the RX ring contents
     after the command is written.

Device tree rules learned today (all cost a flash each):

  * `make Image` does not rebuild the dtb - run `make dtbs` as well;
  * `pack_now.sh` appends /root/phone/author_dtb.bin, not the built dtb;
  * the WiFi driver needs `compatible = "shared-dma-pool"` (for
    of_reserved_mem_device_init) *and* a `size` property (for
    axiGetRsvMemSizeRsvedByKernel);
  * two overlapping reserved-memory nodes break the CONN power-on;
  * the CONN driver needs `reg` for of_reserved_mem_lookup.

`kvm/deploy.sh` now performs the whole build in the right order and prints the
kernel build stamp so a boot can be verified against /proc/version.

## STP link VERIFIED (regression baseline - keep these)

Measured 2026-10-18 20:03 on kernel #140 (MCU patch at +0x0, two independent
dynamic EMI reservations, stp_before_probe=0 at boot).

    clk_dis [14]: 80 40 08 00 | 02 08 04 00 00 00 00 01 | 00 00
    set_stp [12]: 80 40 06 00 | 02 04 02 00 00 03      | 00 00

Both payloads are byte-identical to the vendor's own event definitions
(WMT_SET_MCU_CLK_EVT and WMT_SET_STP_EVT), which proves simultaneously:

  * the mand-mode frame we build is legal (prefix 0x80, type<<4|dlen_hi, dlen_lo,
    header byte 3 = 0, 2-byte zero trailer);
  * the BTIF write path reaches the MCU;
  * the MCU answers;
  * our receive path parses it correctly.

**Use these two commands as the link health check after every change.**  If they
stop answering, the change broke a working STP path - revert it.

Command status matrix at that point:

    opcode 0x08 (clk_en/clk_138/clk_26/clk_dis)  -> answered, 14 bytes
    opcode 0x07 (WMT_RESET)                      -> answered
    opcode 0x04 (set_stp)                        -> answered, 12 bytes
    opcode 0x14 (ant_swap, rf_cal)               -> no answer (may be fire-and-forget)
    HIF INIT_CMD ACCESS_REG                      -> answered, 0x8a00
    HIF INIT_CMD WIFI_START                      -> no answer

## What was fixed in this round

A regression introduced by pinning both drivers into one EMI window and moving
the MCU ROM patch to +0x88000.  The MCU then failed to boot (0x18002600 = 0).
Reverting to two independent dynamic reservations and the patch at +0x0 restored
it: idle 0x1d1e and ECO HW/SW = 0x8a00 again.

Two other additions from the same period were kept and are probably fine, but
neither was what broke it: the BTIF hardware init port (hal_btif_hw_init) and
running the STP sequence from mt6833_conn_force_cycle().
