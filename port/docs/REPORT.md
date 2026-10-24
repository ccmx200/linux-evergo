# MT6895 "xaga" mainline CONN port — findings
`/root/linux`, Linux 7.2.0, one squashed commit. Legend: `ci/` = conninfra/,
`wg/` = wlan_drv_gen4m/, `bt/` = bt/mt66xx/, under `drivers/misc/mediatek/connectivity/`.

## A) Census
| subdir | files | .c | .h | C/H lines |
|---|---|---|---|---|
| bt | 69 | 24 | 26 | 36,985 |
| compat | 12 | 0 | 12 | 263 |
| connadp | 9 | 5 | 3 | 2,136 |
| connfem | 19 | 9 | 6 | 3,926 |
| conninfra | 179 | 65 | 109 | 105,486 |
| wlan_adaptor | 14 | 5 | 4 | 2,432 |
| wlan_drv_gen4m | 485 | 203 | 274 | 586,480 |

789 files (+ top `Makefile`, `Kconfig`), 737,708 C/H lines; wlan_drv_gen4m is 79% of it.
Git gives little: `git rev-list --count HEAD` = 1, `git status --short` empty,
`git log --oneline -5 -- …/connectivity` → `75716ec18 media: mediatek: vcodec: …` (one
snapshot commit touches everything).

Ported (bring-up markers): `Makefile:1-4` ("downstream CONNAC2X2 SOC7_0 fullmac port …
Built-in only for the xaga mainline bring-up"), exports `Makefile:15-24`, `Kconfig:1-30`;
`ci/Kbuild:14` (`MTK_CONNINFRA_CLOCK_BUFFER_API_AVAILABLE=1`), `:16-18`;
`ci/src/conninfra_dev.c:812-819` (late_initcall vs scpsys); `ci/platform/consys_hw.c:891-917`
("xaga mainline: this ran as an UNBOUNDED spin", now 30 s bounded) + XAGA-CONSYS traces
`:742-797`; `ci/core/conninfra_core.c:1919-1933` ("WiFi-only mainline port", BT optional);
`ci/platform/mt6895/mt6895_pmic.c:164-178` ("Mainline bring-up", skip missing rails);
`wg/os/linux/gl_init.c:2463-2592`, `:6014-6088` (XAGA-NVRAM); `bt/btif/Makefile:3-5`;
`bt/btmtk_main.c:4134`; `arch/arm64/boot/dts/mediatek/mt6895-xiaomi-xaga.dts:439-535`;
`arch/arm64/configs/xaga.config:148-156`.
Vendor verbatim: all of `connfem/` (0 markers) plus `*/Android.mk`, `bt/wmt/`,
`bt/uboot_driver/`, `*/init.*.rc`, `Makefile.ce`, `util/checkpatch.pl`,
`bt/bt_power_table/btPowerTable.dat`; `compat/` = 12 shim headers.

## B) Firmware into the CONN MCU?
**conninfra loads no code.** Its only `request_firmware` fetches text:
`ci/conf/conninfra_conf.c:517-547`, called by `conninfra_conf_init` for `"conninfra.cfg"`
(`:564`, `:573-591`), parsed as key=value (`:440-515`) — no image, entry address or SRAM
write. CONN-sysram writes are status words only (clock type / A-die id / efuse):
`ci/platform/mt6895/mt6895_pos.c:256-268`, `:308-312`. No `*_ram_mcu` loader, no ROM-patch
download, no CCIF mailbox inside conninfra.
Firmware is downloaded by the subsystem drivers through shared EMI:
- BT pushes the CONN-MCU image. Names are `soc7_0_ram_mcu_1` / `soc7_0_ram_bt_1`
  (`bt/include/btif/platform_mt6895.h:178-179`), composed to `*_1_hdr.bin` by
  `compose_fw_name` (`bt/include/btif/platform_base.h:219-245`); download by
  `bgfsys_mcu_rom_patch_dl` (`bt/btif/btmtk_mt66xx.c:536-555`) and `bgfsys_bt_ram_code_dl`
  (`:568-578`) via `__download_patch_to_emi` (`:419-505`, 48-byte EMI header then
  `memcpy_toio` at `emi_ap_phy_base+offset`), sequenced by `bgfsys_bt_patch_dl` (`:584-592`)
  from `bt_hw_and_mcu_on` (`:600-634`).
- WLAN downloads MAC RAM code/ROM patch: `wg/chips/common/fw_dl.c:2293` (`wlanDownloadFW`),
  `:2394` (`wlanDownloadPatch`); name `WIFI_RAM_CODE_soc7_0` (`wg/chips/soc7_0/soc7_0.c:152-155`,
  `:697`); power-on EMI pre-download (`soc7_0.c:1454-1456` → `soc7_0_wlanPowerOnInit` `:2210`),
  enabled by `wg/Makefile:225 -DCFG_POWER_ON_DOWNLOAD_EMI_ROM_PATCH=1`.
- NVRAM blob `mediatek/mt6895/WIFI`: `wg/os/linux/gl_init.c:2468`, `:2478-2498`.
CCIF: only `CONN_SEMA_CCIF_INDEX` (`ci/platform/mt6895/include/mt6895.h:19`), CCIF IRQ
remaps `0x1803C008/0x1803E008` (`ci/platform/mt6895/mt6895.c:207-211`, read `:371-385`), and
one `0x1024c014` write clearing "WiFi power-on status to MD"
(`wg/chips/soc7_0/soc7_0.c:1841-1851`). `0x1024c000` is never a download channel.
Verdict: no connsys co-processor firmware download exists; the CONNSYS/WFSYS CPU runs mask
ROM and only RAM code/patches go to EMI.

## C) How CONN is powered
`conninfra_pwr_on` (`ci/src/conninfra.c:138`) → `conninfra_core_power_on`
(`ci/core/conninfra_core.c:1326`) → msg `opfunc_power_on_internal` (`:265-340`, calls
`consys_hw_pwr_on` at `:303`) → `consys_hw_pwr_on` (`ci/platform/consys_hw.c:313-410`).
Order (`consys_hw.c:319-391`): `pmic_mng_common_power_ctrl(1)` `:324` +
`consys_plt_clock_buffer_ctrl(1)` `:325`; pinmux `:338`; `consys_plt_conninfra_on_power_ctrl`
`:341`; chipid poll `:349`; EMI remap `:366`; d_die_cfg `:367`; spi_master_cfg `:369`;
a_die_cfg `:371`; afe_sw_patch `:378`; afe_wbg_cal `:380`; subsys_pll_initial `:382`; sysram
status `:385`; raise voltage `:387`; low-power setting `:388`; PMIC low-power `:391`.
The switch: `consys_conninfra_on_power_ctrl_mt6895` (`ci/platform/mt6895/mt6895_pos.c:75-86`)
forks on `MTK_CONNINFRA_CLOCK_BUFFER_API_AVAILABLE`, set to **1** in `ci/Kbuild:14`, so the
live path is `consys_platform_spm_conn_ctrl_mt6895` (`mt6895.c:216-244`):
`pm_runtime_get_sync()` + `device_init_wakeup()` — **mainline genpd**,
`power-domains = <&spm MT6895_POWER_DOMAIN_CONN>`
(`arch/arm64/boot/dts/mediatek/mt6895-xiaomi-xaga.dts:460`), armed by `pm_runtime_enable` /
`dev_pm_syscore_device` (`mt6895.c:201-204`); that dependency is why the driver is a
late_initcall (`ci/src/conninfra_dev.c:812-819`).
Vendor raw-SPM variant (compiled out, but the register reference):
`consys_conninfra_on_power_ctrl_mt6895_gen` (`ci/platform/mt6895/mt6895_pos_gen.c:234-473`) —
SPM power-on config `:251-252`; `connsys_on_domain_pwr_on` `:255-271`; `pwr_on_s` `:275-291`;
`conn_clk_dis=0` `:296`; de-assert isolation `:302`; **whole-CONNSYS reset `ap_sw_rst_b=1`
`:306-307`**; AP2CONN sleep-protect release `:313-359`; off mirror `:362-469`
(`ap_sw_rst_b=0` `:449-451`).
Regulators `ci/platform/mt6895/mt6895_pmic.c:47-54` (mt6363_vcn13, mt6363_vrfio18,
mt6368_vcn33_1/2, mt6368_vant18, rt6160-buckboost), `devm_regulator_get*` `:109-146`,
enable/voltage `:170-194`, `:425-541`; DT supplies `mt6895-xiaomi-xaga.dts:461-465`.
Clocks: none via CCF — `consys_clk_get_from_dts_mt6895` (`mt6895.c:201-214`) only does
runtime-PM + CCIF ioremaps and `consys_clock_buffer_ctrl_mt6895` (`mt6895.c:246-259`) is a
no-op without `COMMON_KERNEL_CLK_SUPPORT`; bus clock is toggled in HW by
`consys_update_ap2conn_hclk_mt6895_gen` (`mt6895_pos_gen.c:475-500`, called `mt6895_pos.c:84`)
and `CONN_INFRA_CLKGEN_TOP_*` writes (`mt6895_pos_gen.c:1737-1760`).
No CONN-CPU-specific reset pulse: only whole-CONNSYS `ap_sw_rst_b`
(`mt6895_pos_gen.c:306-307`) plus per-subsystem RGU writes — `CONN_RGU_ON_BASE+…`
(`mt6895_pos_gen.c:654-672`), A-die reset de-assert (`:806-820`, `:1254-1263`), WFSYS CPU
reset `0x1800_0120[0]` (`wg/chips/soc7_0/soc7_0.c:1460-1467`), BGF CPU reset
(`bt/include/btif/platform_mt6895.h:1236`).

## D) 0x1D1E idle-loop poll
Yes. `CONNSYS_ROM_DONE_CHECK 0x00001D1E` (`wg/include/chips/soc7_0.h:65`; also
`soc3_0.h:130`, `soc5_0.h:82`) is polled in `wf_pwr_on_consys_mcu`
(`wg/chips/soc7_0/soc7_0.c:1200`, `.wmmcupwron` `:543`) on
`CONN_HOST_CSR_TOP_WF_ON_MONFLG_OUT_ADDR` (0x1806_0B10), 1000×1 ms (`:1469-1502`); comment:
"guarantee that CONNSYS CPU goes to cos_idle_loop"; failure dumps
`WF_TOP_CFG_ON_ROMCODE_INDEX_ADDR` (`:1490-1502`).
Preceding: (1) `conninfra_pwr_on(CONNDRV_TYPE_WIFI)` in `asicConnac2xPwrOnWmMcu`
(`wg/chips/common/cmm_asic_connac2x.c:2552-2585`, called `wg/os/linux/hif/axi/axi.c:286`);
(2) de-assert WFSYS CPU SW reset (`soc7_0.c:1460-1467`); then poll, RAM-code download,
netdev. BT mirrors it: `BGF_MCU_CFG_SW_DBG_CTL == 0x1D1E` after releasing
`CONN_INFRA_RGU_BGFSYS_CPU_SW_RST` (`bt/include/btif/platform_mt6895.h:1237-1279`).

## E) wlan_drv_gen4m: probe → wiphy → scan
`initWlan` (`wg/os/linux/gl_init.c:5946`; `device_initcall` `:6192`, `module_init` `:6196`)
→ `wlanCreateWirelessDevice` `:2670` → `wiphy_new(&mtk_cfg_ops)` `:2691` (table `:648`;
non-unify `mtk_wlan_ops` `:585`) → wiphy config `:2711-2860` → `wiphy_register` `:2856` →
`glRegisterBus(wlanProbe, wlanRemove)` `:6033` → `wg/os/linux/hif/axi/axi.c:976-992` →
`hifAxiProbe` `axi.c:266` → `asicConnac2xPwrOnWmMcu` `axi.c:286` → `wlanProbe`
`gl_init.c:5331`: `glBusInit` `:5395`, `wlanNetCreate` `:5414`, `glBusSetIrq` `:5428`,
`wlanOnPreAdapterStart` `:5441`, `wlanAdapterStart` `:5444` (FW download), `wlanNetRegister`
`:5480` → `register_netdev` `:2373`, `u4ReadyFlag=1` `:4899`; conninfra callbacks in
`register_conninfra_cb` (`axi.c:404-435`).
`mtk_cfg_ops` (`gl_init.c:648-734`): add/del/change_virtual_intf, add/get/del_key,
set_default_mgmt_key, set_default_key, get/del_station, TDLS set, `scan` `:665`,
`abort_scan` `:667`, `sched_scan_start/stop` `:670-671`, connect, update_connect_params,
disconnect, join/leave_ibss, set_power_mgmt, set/del/flush_pmksa, suspend, resume, assoc,
remain_on_channel/cancel, mgmt_tx, update_mgmt_frame_registrations,
start_radar_detection/channel_switch, AP set (change_bss, mgmt_tx_cancel_wait, deauth,
disassoc, start/stop_ap, change_beacon, set_wiphy_params, set_bitrate_mask, set/get_tx_power),
update_ft_ies, monitor channel, external_auth.
Scan: cfg80211 → `mtk_cfg_scan` (`wg/os/linux/gl_cfg80211.c:7219`, P2P vs STA) →
`mtk_cfg80211_scan` `:1006` (ready/reset/assert checks, builds `PARAM_SCAN_REQUEST_ADV`);
completion `WLAN_STATUS_SCAN_COMPLETE` → `kalCfg80211ScanDone`
(`wg/os/linux/gl_kal.c:1999-2016`).
7.3 notes: `net_device` is separate (`alloc_netdev_mq` `gl_init.c:3123,3134`) and hung off
`wdev->netdev`; scan dereferences `request->wdev->netdev` unconditionally
(`gl_cfg80211.c:1034`, `:7232`) — needs NULL-safety for netdev-less wdev. `link_id` is in the
signatures but ignored (`:374`, `:460`, `:6902-6996`, `:4635`, `:6608`; prototypes
`wg/os/linux/include/gl_cfg80211.h:262-267`); no MLO handling. Version ladders use
`CFG80211_VERSION_CODE = LINUX_VERSION_CODE` (`wg/os/linux/include/gl_os.h:209-211`) with
`KERNEL_VERSION` guards (`gl_init.c:600-626`, `:666-700`) — recheck against 7.3 drift.
Regulatory is self-managed (`REGULATORY_CUSTOM_REG` + `cfg80211_regd_set_wiphy`,
`gl_init.c:2754`, `:2767`), not regdb. Uses `register_netdev` + `.ndo_init` (`:2447`) rather
than cfg80211-managed netdev; P2P shares one wiphy (`:2938-2943`).

## F) Reuse for MT6833 (WMT/SOC2_1X1, gen4m, no CONN CPU)
Liftable: `wg/os/linux/gl_init.c`, `gl_cfg80211.c`, `gl_kal.c`, `gl_p2p*.c`, `nic/*`,
`mgmt/*`, `os/linux/platform.c`; AXI glue `wg/os/linux/hif/axi/axi.c` (`driver_data` select
`axi.c:119`, chip_info pattern `wg/chips/soc7_0/soc7_0.c:567`); chip templates
`wg/chips/connac/connac.c` (391 lines, only `ConstructFirmwarePrio`/`PatchName`) and
`wg/chips/soc2_1x1/soc2_1x1.c` (fw-name table `:25-28`, `fw_dl_ops` `:286`, `bus_info` `:221`)
with existing branch `wg/Makefile:111-118` — MT6833 is absent from the tree (grep "6833" = 0
hits). Also `connfem/` in full (DT EPA/ELNA, `connfem_dt_parser.c`,
`connfem_subsys_wifi.c`); `connadp/power_throttling/core.c`+`adapter.c`;
`wlan_adaptor/wifi_pwr_on.c` (`mtk_wcn_wlan_func_ctrl` `:223`) +
`wlan_adaptor/wmt_cdev_wifi.c` (`/dev/wmtWifi`, the WMT-era ABI MT6833 expects);
`compat/include/*.h`; NVRAM-load skeleton `gl_init.c:2470-2492` minus xaga naming.
Useless: `ci/platform/mt6879|mt6885|mt6893|mt6895` (CODA dumps, `*_pos_gen.c` raw
SPM/CLKGEN/EMI/PMIC writes, `*_consys_reg.c`) and generic `ci/platform/consys_hw.c`,
`emi_mng.c`, `coredump_mng.c`, `debug_utility/` (EMI-layout bound) — no CONN CPU to boot;
`wg/chips/soc3_0|soc5_0|soc7_0` and wmmcu paths in
`wg/chips/common/cmm_asic_connac2x.c:2552-2607` (incl. the 0x1D1E poll); `bt/btif/`
(BTIF + BGFSYS-MCU EMI download, `platform_mt68xx.h`) — MT6833 BT is UART/STP, so use
`bt/wmt/stp_chrdev_bt.c`; XAGA NVRAM deferral (`gl_init.c:2500-2592`, blob
`"mediatek/mt6895/WIFI"`) — generic `CFG_WLAN_ASSISTANT_NVRAM` is the reusable half.
