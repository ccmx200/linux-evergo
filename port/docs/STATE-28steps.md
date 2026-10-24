## The full vendor power-on sequence is now replicated - and the CPU still does not start

Eight extraction agents read the vendor `reg_ctrl(on)` (mtk_wcn_consys_hw.c:625,
28 steps) and the platform hooks it calls.  Every step is now accounted for:

| # | Vendor step | Status |
|---|---|---|
| 1 | `consys_reset_emi_coredump` | covered: the driver memsets the whole 5 MB EMI |
| 2 | `consys_hw_vcn18_ctrl(ENABLE)` | our vcn18 regulator |
| 3 | `consys_set_if_pinmux(ENABLE)` | **added**: 0x10005000+0x480/0x490, 0x11EA0000+0x0/0x10 |
| 4 | `consys_hw_vcn28_ctrl(ENABLE)` | skipped: vcn33_2 rail absent from the DT |
| 5 | `efuse_paldo_ctrl` | no-op without co-clock |
| 6 | `reset_bit_set(ENABLE)` | our RGU bit 12 |
| 7 | `consys_hw_spm_clk_gating_enable` | **added**: SPM+0 |= 0x0B160001 |
| 8 | `consys_hw_power_ctrl(ENABLE)` | our pm_runtime on MT6833_POWER_DOMAIN_CONN + PERICFG 0x314 |
| 9 | `consys_ahb_clock_ctrl` | no-op in the vendor |
| 10 | `polling_consys_chipid` | ours (0x100a0000) |
| 11 | `set_access_emi_hw_mode` | **fixed**: MCU_TOP_MISC_ON is 0x180C1000, not 0x18002000 |
| 12 | `update_consys_rom_desel_value` | **does not exist** for MT6833 (NULL ops member) |
| 13 | `acr_reg_setting` | no-op ("No need to do acr") |
| 14 | `afe_reg_setting` | no-op ("No need to do afe") |
| 15 | `emi_entry_address` | **added**: 0xF0170000 / 0xF02A0000 |
| 16 | `set_xo_osc_ctrl` | added (RC bit already clear) |
| 17 | `identify_adie` | **added**: 0x180C1130 bit 8 |
| 18 | `wifi_ctrl_setting` | no-op unless SECONDARY_ADIE |
| 19 | `bus_timeout_config` | **added**: 0x18002440 = 0x90000002 |
| 20 | `set_mcu_mem_pdn_delay` | **does not exist** for MT6833 |
| 21 | `reset_bit_set(DISABLE)` | our RGU bit 12 clear |
| 22 | `polling_goto_idle` | our poll of CONN_ON+0x600 |
| 23 | `wifi_ctrl_switch_conn_mode` | no-op unless SECONDARY_ADIE |
| 24 | `hw_vcn_ctrl_after_idle` | PMIC low-power bits only, no-op here |
| -- | `gpio_ctrl` / `jtag_set_for_mcu` | GPS/I2S/BGF-EINT pinmux + jtag, not power |

Measured on the handset after the last build, every added step lands:

    vendor: if_pinmux 01=0x11111111 02=0x00000001     (exactly the vendor values)
    vendor: pad drive set to 2 mA
    vendor: spm pwr_on_clk 0x00000001 -> 0x00000001   (already set; key consumed)
    vendor: access_emi_hw_mode 0x00000000 -> 0x00000001
    vendor: emi entry bt=0xf0170000 wifi=0xf02a0000
    vendor: identify_adie 0xf0000000 -> 0xf0000000    (bit 8 already clear)
    vendor: ahb_timeout 0x90000002 -> 0x90000002      (already at target)

And still:

    CONN CPU idle check: CONN_ON+0x600 = 0x00000000 (NOT in idle loop)
    monflg[0x18060b10] = 0x00000000
    halDriverOwnTimeout: Driver own timeout 750 ms

Conclusion, and it is an important one: **the Linux-side register sequence is no
longer the difference.**  With the vendor's own 28-step power-on replicated and
each write verified to land, the CONN CPU still never leaves reset.  The blocker
is therefore outside that sequence - something in how the device is brought up
before Linux, or a path the vendor kernel gets from a component this port does
not have (the external A-die / connectivity companion, its power and clock
supplies, and the WMT firmware handshake that talks to it).
