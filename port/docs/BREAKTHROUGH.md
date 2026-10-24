# WiFi bring-up: the chain, end to end

Everything below is hardware-measured on the handset (mainline 7.3.0-rc4, test
initramfs on boot_b).

## What now works

    CONN CPU executes              CPUPCR leaves 0x74
    CONN CPU reaches ROM idle      CONN_ON+0x600 = 0x00001d1e
    CONN CPU settles in firmware   CPUPCR = 0x00022052  (identical to the
                                   working vendor system's idle value)
    WiFi HIF window accessible     0x18007030 = 1, 0x1800712c = 0x00430000
    bus ownership                  DRIVER OWN Done[2020 us]
    firmware identified            WIFI_RAM_CODE_soc2_2_1_1.bin,
                                   t-neptune-sp-soc2_2229-tc8sp1-mt6833-
                                   PALMER_SOC2_2_E1_MT6631_ASIC_MINIROM
    firmware register sections     3 of 5 downloaded and ACKed
    firmware body in EMI           549904 B @ 0xbf000000+0x2f0000
                                   168880 B @ 0xbf000000+0x386000

## The six defects found and fixed, in the order they were hiding

### 1. ROM patch header is 48 bytes, not 32
The vendor log's `Size` fields (0x2d630 / 0xa7a10 / 0x3ad8c) match
`file_size - 48` exactly for all three blobs.  With 32 the whole image was
shifted by 16 bytes.  CPUPCR went from a dead `0x74` to executing in the ROM
region the moment this was corrected.

### 2. CONN bus/hif clock gate never enabled
    vendor INFRA_AO+0x080 = 0x8bbfb900
    ours                  = 0x033fb900   (diff 0x88800000, bits 31/27/23)
Mainline has no `clk_scp_conn_main`.  OR-ing the missing bits at power-on is the
stand-in; without it `0x18007030` answers `0xdeadfeed`.

### 3. The idle poll was 20-40 ms instead of 400
The vendor budget is 10 x 20 ms and the signature appears ~85 ms after reset
release.  `idle_poll_ms` now defaults to 400.

### 4. The two MT6895 WFSYS writes are not used on MT6833
`0x18000000` stays `0x42424242` and `0x18000010` stays 0 on the working vendor
system.  Both writes are gated off by default.

### 5. The EMI firmware copy was compiled out
`wlanDownloadEMISection()` is wrapped in `#if CFG_MTK_ANDROID_EMI`, which is 0 in
this build, so both firmware body sections were silently dropped (the function
still returns success).  A plain `ioremap` + `memcpy_toio` now does the copy -
LK already programmed the EMI MPU for the region.

### 6. `gConEmiPhyBaseFinal` was zero
    wlanDownloadEMISection: Consys emi memory address gConEmiPhyBaseFinal invalid
`gl_init.c` does `gConEmiPhyBaseFinal = gConEmiPhyBase`, and `gConEmiPhyBase` is
only assigned in the X86 unit-test build; the vendor fills it through
`conninfra_get_phy_addr()`, which is compiled out.  `initWlan()` is not on this
port's probe path either, so the fallback lives in `wlanDownloadEMISection()`
itself and takes the value from the CONN driver.

## Where it stops now

The last 3.08 s is `wlanConfigWifiFuncStatus()` waiting for
`INIT_EVENT_ID_CMD_RESULT`:

    FWDL-DBG: ram_entry=0x73000 -> start cmd enable=1 pda=0, dl status=0
    (3.08 s later)
    wlanAdapterStart: Fail reason: 5      (WAIT_FIRMWARE_READY_FAIL)

`ram_entry` is valid and the start command is sent with enable=1, so the host
side does everything right.  The MCU never answers.

Relevant state:

    0x180C1140 = 0x00000004     bit2 (N9_DONE) set, bit1 (INIT_DONE) not
    0x18007104 = 0x00022052     same PC as the working vendor system
    0x18002600 = 0x00001d1e     ROM idle signature

so the CONN firmware is running and idle exactly as it does on Android - the
missing piece is the host command/response channel itself.

### Next suspect: the WLAN interrupt

    glBusSetIrq: request_irq num(159)

and `/proc/interrupts` contains no line for it (the table jumps from
panfrost-job 158 straight to the IPIs), while the `wifi@18000000` device tree
node carries `interrupts = <0 0x179 4>` i.e. **SPI 379**.  If the RX interrupt
never fires, every `nicRxWaitResponse()` times out, which is what all three
3-second failures look like.

Note the SoC also has `CONN2AP_SW_IRQ` at `mcu_base + 0x150 = 0x18002150`
(bits 27:24, clear at 0x1800214C) and the consys node lists SPI 378 / 79 / 379.

## Tools available in the test image

`iw`, `wpa_supplicant`, `wpa_cli`, `tcpdump`, `strace`, `ethtool`, `lspci`,
`lsusb`, `curl`, `jq`, `ip`, `lsblk`, `blkid`, `e2fsck`, `xxd`, `bash`,
`coreutils`, `procps`, whole thing 69 MB, boot.img payload 41.7 MB.

Live debugging without reflashing:

* `/proc/conn_cycle` - re-run power-on
* `/proc/conn_sweep` - a table of step masks in one boot
* `/proc/conn_reg` - arbitrary 32-bit register read/write
* `replay_probe` - re-run the WLAN probe in the same boot
* runtime knobs: `steps`, `patch_hdr`, `patch_mode`, `no_clear`, `idle_poll_ms`,
  `infra_cg_extra`, `wf_pwr_ctl_write`, `wf_cpu_rst_write`
* `kvm/push_file.ps1` - push a firmware blob over the serial console
