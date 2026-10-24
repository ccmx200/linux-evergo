## The A-die was misidentified - fixed

Reading the vendor Android kernel (`/root/kernel-lxc_xiaomi_mtk810_mt6833-resukisu`)
turned up two real defects in this port, both now fixed and verified on hardware.

The radio baseband (CONNSYS MCU/MAC/PDMA/EMI) is inside the MT6833 SoC, but the
**RF/analog companion ("A-die") is a separate chip**, either MT6631 (0x6631,
primary) or MT6635 (0x6635, secondary).  It is read over the SoC's CONN RF SPI
(`mt6833.c:1807-1900`):

    MCU_TOP_MISC_ON + 0x500 |= 1
    CONN_RF_SPI(0x180c6000) + 0x50 = 0x0000b024 (primary) / 0x0000b02C (secondary)
    CONN_RF_SPI + 0x54 = 0
    chipid = CONN_RF_SPI + 0x58 >> 16

This port used to hardcode 0x6635 everywhere.  Reading it from hardware says:

    adie: primary read 0x6631
    adie: detected A-die id 0x6631 (MT6631 primary)

and evergo's defconfig agrees: `CONFIG_MTK_FM_CHIP="MT6631_FM"`.

Consequences, both fixed:

* `identify_adie` (0x180C1130 bit 8) is SET for a primary A-die and CLEAR for a
  secondary one.  The driver had it backwards; it now follows the detected id:
  `vendor: identify_adie 0xf0000000 -> 0xf0000100 (adie 0x6631)`.
* the firmware names carry the flavour letter only for the secondary A-die.  The
  ROM patch was loading `soc2_2_ram_mcu_1a_1_hdr.bin` - the 0x6635 variant.  It
  now picks by id and loads the unflavoured blob:

      ROM patch: A-die 0x6631 -> soc2_2_ram_mcu_1_1_hdr.bin
      ROM patch: soc2_2_ram_mcu_1_1_hdr.bin copied, 185920 bytes to EMI 0xbf000000+0x0

  The same id now drives the firmware flavour the WLAN driver asks for, so it
  requests `WIFI_RAM_CODE_soc2_2_1_1.bin` instead of the `1a_1` variant.  Both
  blobs are staged in the initramfs.

## Two structural facts confirmed from the vendor kernel

* **Mainline has no MT6833 power-domain driver.**  `drivers/soc/mediatek/` has
  no `mediatek,mt6833-power-controller` implementation, so this driver's
  `pm_runtime_resume_and_get()` is a no-op and the CONN domain is only turned on
  by the raw SPM writes.  The vendor instead enables the "conn" clock
  (`<&scpsys SCP_SYS_CONN>`), which runs `spm_mtcmos_ctrl_conn_pwr()`:
  PWR_ON=1, PWR_ON_2ND=1, wait for PWR_STATUS and PWR_STATUS_2ND bit 1, then
  PWR_CLK_DIS=0, PWR_ISO=0, PWR_RST_B=1.  Measured here: SPM 0x304 = 0x0d and
  both status registers report bit 1 set, i.e. the end state already matches.
* **Nothing outside the consys driver is needed to reach the ROM idle loop.**
  The vendor polls 0x18002600 for 0x1D1E *inside* reg_ctrl(1), before any
  STP/firmware exchange.  Its WMT core and consys probe are userspace-triggered
  (`/dev/wmtdetect` ioctl, then writing 1 to `/dev/wmtWifi`), and the EMI base
  comes from the `mediatek,consys-reserve-memory` reserved-memory node parsed at
  boot.  No nvdata, no sysfs, no bootloader handshake.

## Where it stands

Every vendor power-on step is replicated and verified to land, the CONN domain
reports powered, the A-die is now correctly identified as MT6631 - and the CONN
CPU still does not reach its idle loop:

    CONN CPU idle check: CONN_ON+0x600 = 0x00000000 (NOT in idle loop)
    monflg[0x18060b10] = 0x00000000
    halDriverOwnTimeout: LP cannot be own back, Timeout[1]
    wlanAdapterStart: Fail reason: 1

`wlan0` does not exist.  The remaining blocker is not a register this port can
derive from the vendor source; it needs either a register baseline captured
under the working vendor kernel, or the vendor WMT core itself compiled and run
against this hardware.
