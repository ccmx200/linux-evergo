# MT6833 (`evergo`) gen4m scan finish-line — file:line evidence

`G=/root/kernel-lxc_xiaomi_mtk810_mt6833-resukisu/drivers/misc/mediatek/connectivity/wlan/core/gen4m`
(all paths below are relative to `G` unless prefixed). Build facts from
`/root/kernel-lxc_xiaomi_mtk810_mt6833-resukisu/drivers/misc/mediatek/connectivity/Makefile:47-52`:
`HIF=axi`, `WLAN_CHIP_ID=6833`, `MTK_COMBO_CHIP=SOC2_1X1`, `MTK_ANDROID_WMT=y` =>
`CFG_SUPPORT_CONNINFRA=0`, `CFG_MTK_ANDROID_WMT=1` (G/Makefile:250-300), chip driver
`chips/soc2_1x1/soc2_1x1.c` (connac1x, no CR4).

## A) Scan call path cfg80211 -> firmware

1. `os/linux/gl_init.c:701` `mtk_cfg_ops` (CFG_ENABLE_UNIFY_WIPHY=1, `include/config.h:809-810`); `.scan = mtk_cfg_scan` at `gl_init.c:718`.
2. `os/linux/gl_cfg80211.c:7059` `mtk_cfg_scan()` — rejects if `u4ReadyFlag==0` (:7066), P2P (:7071), else `mtk_cfg80211_scan` (:7075).
3. `gl_cfg80211.c:1108` `mtk_cfg80211_scan()` — gates (:1125 kalIsResetting, :1134 BSS idx, :1146 chip-assert, :1169 pending), builds `PARAM_SCAN_REQUEST_ADV` (:1194-1342), calls `kalIoctl(..., wlanoidSetBssidListScanAdv, ...)` (:1345).
4. `common/wlan_oid.c:816` `wlanoidSetBssidListScanAdv()` — radio gates (:825-848), `aisFsmScanRequestAdv()` (:867/:871), scan-done timer (:875).
5. `mgmt/ais_fsm.c:5041` `aisFsmScanRequestAdv()` -> scan FSM (`mgmt/scan_fsm.c:600`/`:679` `scnFsmHandleScanMsg[V2]`) -> `scan_fsm.c:205-207` `scnFsmSteps()` -> `scnSendScanReqV2()`.
6. `mgmt/scan_fsm.c:246` `scnSendScanReqV2()` — fills `struct CMD_SCAN_REQ_V2` (:262-447), issues FW cmd at `:449-457`: `wlanSendSetQueryCmd(prAdapter, CMD_ID_SCAN_REQ_V2, TRUE, ...)`.
7. `common/wlan_oid.c:10552` `wlanSendSetQueryCmd()` — `NIC_FILL_CMD_TX_HDR` (:10599), payload (:10607), `kalEnqueueCommand` (:10611).
8. `common/wlan_lib.c:1972` `wlanSendCommand()` (`:1829` mthread) -> resource (:1997) -> `nicTxCmd` `nic/nic_tx.c:2521` -> WPDMA TX ring (`os/linux/hif/common/hal_pdma.c`) -> MCU.

Return: RX event ring -> `nic/nic_cmd_event.c:3781` `nicEventScanDone()` ->
`scan_fsm.c:860` `scnEventScanDone()` -> `os/linux/gl_kal.c:5438` `kalScanDone()` ->
`mgmt/scan.c:4321` `scanReportBss2Cfg80211()` + `kalIndicateStatusAndComplete`
(`gl_kal.c:5456`) -> `gl_kal.c:1996` `kalCfg80211ScanDone()` -> `cfg80211_scan_done()`
(:1986). Results come from the driver's own cache (`mgmt/scan.c:3200`
`scanAddScanResult`), so FW need only deliver beacon/probe-resp events.

## B) Ordered prerequisites

0. `os/linux/gl_init.c:2910` `wlanCreateWirelessDevice()`: `:2931` `wiphy_new(&mtk_cfg_ops,...)`, `:3100` `wiphy_register()`; called from module init `:6296`. Pure SW.
1. `os/linux/hif/axi/axi.c:886` `mtk_axi_probe()`: `axiCsrIoremap()` (:902/`:481`: DT `"mediatek,wifi"` :489, `of_address_to_resource`+`of_iomap` :495-524), `axiDmaSetup` (:905), `register_wmt_cb` (:917/`:387-399`).
2. `axi.c:266` `hifAxiProbe()`: `mtk_wcn_consys_hw_wifi_paldo_ctrl(1)` (:279) then `pfWlanProbe()` (:294).
3. `gl_init.c:5610` `wlanProbe()`: `glBusInit` (:5674), `wlanNetCreate` (:5693, SW only), `glBusSetIrq` (:5708), `wlanAdapterStart` (:5726).
4. `common/wlan_lib.c:1122` `wlanAdapterStart()` — the real order:
   - `wlanOnPreAllocAdapterMem` (:1149 -> `:717`); BSS_INFO is *host* memory bound here (:755-759 `aprBssInfo[i]=&...arBssInfoPool[i]`) — no MCU needed;
   - `nicAllocateAdapterMemory` (:1153 -> `nic/nic.c:187`) mgmt/RX/TX/coalescing/HIF buffers;
   - `ACQUIRE_POWER_CONTROL_FROM_PM` (:1170), `nicpmSetDriverOwn` (:1175 -> `nic/nic_pwr_mgt.c:145` -> `hal_pdma.c:456` `halSetDriverOwn()`, LP-OWN clear/read :484/:490);
   - `nicInitializeAdapter` (:1197 -> `nic/nic.c:677`), `nicVerifyChipID` (nic.c:687/632) -> `hal_pdma.c:196` `halVerifyChipID()` reading `CONN_CFG_BASE`=0x80020000 (`include/nic/connac_reg.h:78`) + `TOP_HW_CONTROL` 0x1008 / `TOP_HW_VERSION` 0x1000 (`include/nic/mt66xx_reg.h:474-475`) vs `SOC2_1X1_CHIP_ID` 0x0001 (`include/chips/soc2_1x1.h:26`);
   - `nicHifInit` (nic.c:697), `halHifSwInfoInit` (:1216), `HAL_ENABLE_FWDL(TRUE)` (:1224 -> `include/nic/hal.h:554` -> `hal_pdma.c:1953`);
   - `wlanSetChipEcoInfo` (:1227 -> `wlan_lib.c:3496`) reads TOP_HVR/TOP_FVR via **inband init cmd** `wlanAccessRegister()` (`:3552`) — needs a live MCU;
   - `wlanDownloadFW` (:1247 -> `chips/common/fw_dl.c:2279`);
   - `wlanCheckWifiFunc(TRUE)` (:1269 -> `wlan_lib.c:1605`) polling `sw_sync0` (`soc2_1x1.h:28` `CONN_CFG_ON_CONN_ON_MISC_ADDR`; bits `soc2_1x1.c:353-355`; macro `include/nic/hal.h:391`);
   - `fgIsFwDownloaded=TRUE` (:1296), `wlanQueryNicResourceInformation` (:1299), `wlanQueryNicCapabilityV2` (:1316), `wlanUpdateNicResourceInformation` (:1330), `wlanPrintVersion` (:1332), `wlanUpdateBasicConfig` (:1337), `wlanUpdateNetworkAddress`/`nicApplyNetworkAddress` (:1343/:1346);
   - `wlanOnPostFirmwareReady` (:1368 -> `:910` -> `nicInitMGMT` :989 -> `nic/nic.c:3405`: `cnmInit` :3416, `wmmInit` :3418, `rlmFsmEventInit` :3421, `scnInit` :3424, `aisFsmInit` :3428/`mgmt/ais_fsm.c:329`) — AIS/scan FSM exists only now.
5. `gl_init.c:5754` `wlanNetRegister` (`:2670`) -> `register_netdevice` => `wlan0` appears only after step 4 succeeded; `u4ReadyFlag=1` in `gl_init.c:5200` (`wlanOnWhenProbeSuccess`, `:5135`).

## C) Firmware files and handshake

- Requested names (`chips/soc2_1x1/soc2_1x1.c:25-28`): `apucSoc2_1x1FwName[] = { CFG_FW_FILENAME "_soc2_2" }`; `CFG_FW_FILENAME="WIFI_RAM_CODE"` (`include/config.h:146`/`:149`).
- Construction `soc2_1x1.c:116-188`: four probes per base name (:135, :151, :167, :178) of `"%s_%u%s_%u.bin"` = base + `CFG_WIFI_IP_SET`(=1, `config.h:706-707`) + flavor char + `wlanGetEcoVersion()`. Concrete: **`WIFI_RAM_CODE_soc2_2_1a_1.bin`**, then `WIFI_RAM_CODE_soc2_2_1a_1`, `WIFI_RAM_CODE_soc2_2`, `WIFI_RAM_CODE_soc2_2.bin`. Eco table `soc2_1x1.c:30-34` (E1: factory 0xA, ver 1).
- The flavor char **is** part of the name: `kalGetFwFlavor()` `os/linux/gl_kal.c:8491` returns `prAdapter->fw_flavor[0]`, set from DT property **`flavor_bin`** on `"mediatek,wifi"` (`axi.c:859-874`, applied `:900`); fallback `kalGetFwFlavorByPlat()` `os/linux/plat/mt6833/plat_priv.c:191-207` returns `'a'` iff `mtk_wcn_wmt_ic_info_get(WMTCHIN_ADIE)==0x6635`.
- ROM patch `mtsoc1_0_patch_e%x_hdr.bin` (`soc2_1x1.c:190-200`) is registered as `.downloadPatch` **only when `CFG_MTK_ANDROID_WMT==0`** (`soc2_1x1.c:278-289`). With WMT on (this build) the WLAN driver skips it; connsys does it: `connectivity/common/common_main/platform/mt6833.c:1923` `consys_dl_rom_patch()` -> `mtk_wcn_soc_rom_patch_dwn()`.
- Driver sequence `fw_dl.c:2279` `wlanDownloadFW`: ready-check (:2299), `HAL_ENABLE_FWDL(TRUE)` (:2313), optional patch (:2315), `downloadFirmware(IMG_DL_IDX_N9_FW)` (:2350) -> `wlanConnacFormatDownload` (`:2207`) over WPDMA fw-dl ring (ring 3, `soc2_1x1.c:228`), `HAL_ENABLE_FWDL(FALSE)` (:2368). Download-done: `fw_dl.c:911` `wlanPatchSendComplete()` sends `INIT_CMD_ID_PATCH_FINISH` (:955) and `wlanConfigWifiFuncStatus()` (:997/`:1465`) blocks on `nicRxWaitResponse(IMG_DL_STATUS_PORT_IDX)` for `INIT_EVENT_ID_CMD_RESULT` with `ucStatus==0` (:1503-1526); then the `sw_sync0` ready-bit poll.

## D) What survives without the CONN MCU

Succeeds with MTCMOS/clocks ok but CONN CPU dead / WF CSR mute:
- `wiphy_new`+`wiphy_register` and all cfg80211 registration / `phy0` (`gl_init.c:2931`, `:3100`) — pure SW.
- ADAPTER/GLUE/netdev alloc (`gl_init.c:5693`), `request_irq` (`:5708`), `nicAllocateAdapterMemory` (`nic.c:187`), `aprBssInfo[]` binding (`wlan_lib.c:755`) — host memory only.
- `request_firmware()` of the names above (filesystem only; no WLAN blobs in `R/firmware`, so their origin is outside this tree — cannot be determined here).
- DRV-OWN (`hal_pdma.c:456`) touches only CONN_HIF/WPDMA own bit and chip-id verify reads `CONN_CFG` 0x80020000, not the WF MAC — both *may* pass while the WiFi block is dead. Treat "chip id OK" as no evidence the WiFi block is alive.

Strictly require a running CONN MCU (plus the connsys ROM patch):
- `wlanSetChipEcoInfo` (`wlan_lib.c:1227`/`:3496` -> `wlanAccessRegister` `:3552`): inband cmd answered by ROM/MCU; otherwise `SET_CHIP_ECO_INFO_FAIL` aborts at `:1230`.
- `wlanDownloadFW` (`fw_dl.c:2279`) incl. the `INIT_CMD_ID_PATCH_FINISH`/`INIT_EVENT_ID_CMD_RESULT` handshake (`:911`, `:1465`); failure => `RAM_CODE_DOWNLOAD_FAIL` (`wlan_lib.c:1249`).
- The ready bit (`wlan_lib.c:1269`/`:1605`) is set only by FW.
- `wlanQueryNicCapabilityV2` (`wlan_lib.c:1316`), `wlanUpdateBasicConfig` (:1337), every later CMD incl. `CMD_ID_SCAN_REQ_V2`.
- `nicInitMGMT`/`scnInit`/`aisFsmInit` (`nic.c:3416-3428`) — the scan FSM itself.
- `wlanNetRegister` (`gl_init.c:5754`) => `wlan0`, and `u4ReadyFlag=1` (`gl_init.c:5200`). A failed `wlanAdapterStart` breaks out before netdev registration (`:5734`), so there is **no `wlan0`**; even partially, `mtk_cfg_scan` returns `-EFAULT` (`gl_cfg80211.c:7066`).
- Scan *results* need the RX event/data rings serviced by FW (`nic_cmd_event.c:3781`, `scan.c:3200`).

Finish line for the port: CONN MCU executing connsys ROM patch + `WIFI_RAM_CODE_soc2_2_1a_1.bin`, ready bit asserted, NIC capability V2 answered — before that, wiphy registration is the only thing that works.

## E) MTK-specific platform dependencies

On the scan/init path (mandatory):
- `mtk_wcn_consys_hw_wifi_paldo_ctrl()` — `axi.c:279` (on) / `:338` (off).
- `mtk_wcn_wmt_wlan_reg()` — `axi.c:399`; without it connsys never calls `hifAxiProbe` (`:392`), so probe never runs.
- `mtk_wcn_wmt_mpu_lock_aquire/release()` — `gl_init.c:6307`/`:6316`.
- `mtk_wcn_wmt_ic_info_get()` — `nic.c:5071/5076/5081` (chip HW/FW ver), `plat/mt6833/plat_priv.c:194` (ADIE -> FW flavor): decides the file *name*, hence on the FW-load path.
- MTK EMI/MPU `kalSetEmiMpuProtection`/`kalSetDrvEmiMpuProtection` (`plat_priv.c:157-188`, `gl_init.c:6313`, `mtk_emimpu_*`): DMA buffers must be reachable — effectively required.
- DT contract `"mediatek,wifi"` + `reg[0]` CSR window + `flavor_bin` (`axi.c:128-131`, `:489-524`, `:859-874`) and the bus<->chip table `soc2_1x1_bus2chip_cr_mapping[]` (`soc2_1x1.c:37-60`: MCU_CFG 0x80000000, CONN_HIF/PDMA 0x50000000, WF_LMAC_TOP 0x820F0000). Mainline must reproduce this window/mapping.

Optional / off the scan path:
- `mtk_wcn_wmt_msgcb_reg`, `mtk_wcn_wmt_assert_keyword`, `mtk_wcn_set_connsys_power_off_flag` (`gl_rst.c:251`, `:509-511`) — reset/coredump only.
- `mtk_wcn_wmt_do_reset_only` (`soc2_1x1.c:205`) — cal-debug only.
- `conn_pwr_*` power throttling and `asicConnac2xPwrOnWmMcu` — CONNINFRA-only, compiled out (`axi.c:280-291`).
- CPU boost `kalBoostCpu` (`plat_priv.c:150`), MET, DBDC (`scan_fsm.c:301-306` only sets a flag; DBDC needs no platform API), power save.

Cannot be determined from this tree: the exact `IMG_DL_IDX_N9_FW` load address / dyn-mem map (`downloadByDynMemMap` is NULL for soc2_1x1), whether mainline must itself reproduce connsys ROM-patch download, and the vendor `reg[0]` CSR base (lives in the downstream DTS, not here).
