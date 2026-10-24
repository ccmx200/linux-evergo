# MT6833 CONN-MCU firmware download - minimal protocol from source
Paths relative to `drivers/misc/mediatek/connectivity/common/common_main/` = `CM/`.
Tree: /root/kernel-lxc_xiaomi_mtk810_mt6833-resukisu (4.14, SOC2_1X1,
connectivity/Makefile:53). Read-only inspection; nothing modified.
## HEADLINE (changes the plan)
1. **No CCIF TX/RX FIFO handshake exists in this tree.** `grep -rln "ccif\|CCIF"
   drivers --include=*.c --include=*.h` inside connectivity returns only
   platform/mt6833.c, platform/include/mt6833.h, platform/include/mtk_wcn_consys_hw.h,
   the identical mt6785/mt6853 copies, and WLAN gen4m files (unrelated to this path).
   CCIF4 is used for exactly one bit (power-on request) + an ACK wipe at power-off.
2. **The ROM patch is not streamed to the MCU at all.** It is copied by the AP into
   CONN EMI (`ioremap` + `memcpy_toio`) while the CPU is held in reset.
   `consys_get_options()` sets `OPT_DISABLE_ROM_PATCH_DWN` platform/mt6833.c:2571-2583,
   which makes `mtk_wcn_soc_sw_init()` skip the STP fragment download (`patch_num = 0`)
   core/wmt_ic_soc.c:1265-1279. The STP fragment protocol still exists (legacy/combo)
   and is documented below.
3. `soc2_2_ram_mcu_1a_1_hdr.bin`, `MTK_WCN_FW_*`, `wmt_download_firmware`,
   `wmt_core_download_*` **do not exist in the tree** (0 grep hits). Names are injected
   from userspace; kernel knows only suffix `"_hdr.bin"` core/include/wmt_ic.h:70 and
   fallback `"mt66xx_patch_hdr.bin"` core/include/wmt_core.h:59, resolved by
   `request_firmware()` linux/wmt_dev.c:447.
## A) Entry point -> actual transfer
`mtk_wcn_wmt_func_on()` core/wmt_exp.c:210 -> `mtk_wcn_wmt_func_ctrl()` :217 ->
`opfunc_func_on()` core/wmt_core.c:1295 -> `opfunc_pwr_on()` :1328 (body :1188) ->
1. `wmt_core_ctrl(WMT_CTRL_HW_PWR_ON)` core/wmt_core.c:1205
2. `wmt_ctrl_hw_pwr_on()` core/wmt_ctrl.c:431 -> `wmt_plat_pwr_ctrl(FUNC_ON)` :442
3. `wmt_plat_pwr_ctrl()` platform/wmt_plat_alps.c:552, SOC branch :559-560
4. `mtk_wcn_consys_hw_pwr_on()` platform/mtk_wcn_consys_hw.c:889 (EMI check :893-897)
   -> `mtk_wcn_consys_hw_reg_ctrl(1,..)` :898, body :622
5. reset assert :665-666; power :669-670; `polling_consys_ic_chipid` :677-679
6. `polling_consys_chipid()` platform/mt6833.c:951 - polls `mcu_top_misc_off+0x10`
   (mt6833.h:77) for `0x100a0000` (mt6833.h:84), 10 x 20 ms (:962-980); on match
   `consys_dl_rom_patch(ip_ver,fw_ver)` :975
7. `consys_dl_rom_patch()` platform/mt6833.c:1923 (guarded by `rom_patch_dl_flag`,
   init 1 at :309) -> `mtk_wcn_soc_rom_patch_dwn()` core/wmt_ic_soc.c:3510
   - `wmt_ctrl(GET_ROM_PATCH_INFO)` :3539 -> core/wmt_ctrl.c:647; name+4 address bytes
     come from `pWmtRomPatchInfo[type]` core/wmt_lib.c:2568-2584, set by ioctl
     `WMT_IOCTL_SET_ROM_PATCH_INFO` linux/wmt_dev.c:1327-1350
   - `wmt_ctrl(GET_PATCH)` :3549 -> core/wmt_ctrl.c:911 -> `wmt_dev_patch_get()`
     linux/wmt_dev.c:436,447 `request_firmware()`
   - strip 32-byte `struct wmt_rom_patch` header :3592-3598
   - `ioremap(emi_ap_phy_addr+patchEmiOffset, patchSize)` :3621, `osal_memcpy_toio()`
     :3625, `iounmap()` :3626  **<-- this is the download**
   - build-time stamp into EMI :3638-3643
8. back in mtk_wcn_consys_hw.c: reset deassert :701-702; `consys_polling_goto_idle()`
   :704-706 (`mcu_base+0x600`==0x1D1E, 10 x 20 ms, platform/mt6833.c:577-592)
9. `opfunc_pwr_on` then `wmt_core_stp_init()` core/wmt_core.c:1216 -> `sw_init` ->
   `mtk_wcn_soc_sw_init()` core/wmt_ic_soc.c:1184: legacy patch download skipped
   (:1265-1279), chip-id cmd :1362-1365, reset cmd :1344, coex :1480-1487,
   `mtk_wcn_soc_calibration()` :1496-1501.
## B) Wire protocol
### B.1 Message header (host <-> MCU command channel)
`WMT_PKT` core/include/wmt_core.h:390-395: `UINT8 eType; UINT8 eOpCode;
UINT16 u2SduLen/*LE*/; UINT8 aucParam[32];`  `WMT_HDR_LEN 4`, `WMT_STS_LEN 1`,
`WMT_FLAG_LEN 1` (wmt_core.h:86-90); types `CMD=1/EVENT=2` (:398-403); opcodes
`PATCH=1 TEST=2 WAKEUP=3 HIF=4 STRAP_CONF=5 FUNC_CTRL=6 RESET=7 INT=8` (:405-416).
Little endian proven by `osal_memcpy(&WMT_PATCH_CMD[2], &cmdLen, 2)` core/wmt_ic_soc.c:3027,3441.
Frames are literal byte arrays (no packing ambiguity):
`WMT_RESET_CMD {01 07 01 00 04}` / `_EVT {02 07 01 00 00}` core/wmt_ic_soc.c:144-145;
`WMT_SET_CHIP_ID_CMD {01 02 05 00 11 ..}` :147-148; `WMT_PATCH_CMD {01 01 len_lo len_hi
flag}` :142 (flag 1 first/2 mid/3 last :54-56); `WMT_PATCH_EVT {02 01 01 00 00}` :143;
addr cmd :160-167; 4-byte-addr variant `{01 01 05 00 04 a0 a1 a2 a3}` :187.
### B.2 CCIF4 registers
DT region `0x1024C000, 0x40` platform/mt6833.dts:919-920, index
`AP_PCCIF4_BASE_INDEX` platform/include/mtk_wcn_consys_hw.h:182, iomap platform/mt6833.c:1709.
- `..._AP_PERI_AP_CCU_CONFIG = 0x314` mt6833.h:196 - **on infra_ao_pericfg_base
  (0x10003000)**, not on the CCIF base: mt6833.c:636-638. bit0 `CON_PWR_ON`,
  bit1 `CON_SW_READY` (`..._CON_PWR_ON_CON_SW_READY_MASK (0x3<<0)` mt6833.h:198).
  power-on write `0x1` mt6833.c:636-638; power-off write `0` :826-827.
- `..._AP_PCCIF_ACK_OFFSET = 0x14` mt6833.h:197, on `ap_pccif4_base`, power-off only:
  `udelay(100)` then write `0xFF` and read back mt6833.c:838-853.
- clock `clk_infracfg_ao_ccif4_ap_cg` on at :615-620, off at :855 (DT "ccif" mt6833.dts:929-931).
- **No TX reg, no RX reg, no polled handshake bit, no CCIF IRQ**: consys node has only
  3 IRQs (conn2ap_sw_irq/WDT/BGF_EINT) mt6833.dts:924-928. Answer to "which bit is
  polled / what ACK is written": nothing during download; only `0x314[0]=1` request
  and `0x14=0xFF` wipe at power-off.
### B.3 Splitting / checksum / done
EMI path (used on MT6833): **no chunking** - one ioremap + one memcpy_toio, bounded by
`patchEmiOffset+patchSize < emi_size` core/wmt_ic_soc.c:3609-3628. **No CRC, no
checksum verify**: a 2-byte field is only *skipped* when `OPT_PATCH_CHECKSUM` is set
(option present, platform/mt6833.c:2579) :3276-3282; `BCNT_PATCH_BUF_CHECKSUM (2)`
core/include/wmt_core.h:68. "Download done" = patch present in EMI before reset release
(mt6833.c:701-702 + goto-idle poll). No completion message.
STP path (compiled, disabled here): 1000-byte fragments `DEFAULT_PATCH_FRAG_SIZE`
:53, fragNum=ceil(size/1000) :3421-3422, each prefixed with 5-byte `WMT_PATCH_CMD`,
acked by `WMT_PATCH_EVT` :3449-3490; 4-byte target address sent first
(`OPT_NORMAL_PATCH_DWN_3`) :2928-2958. After the last fragment the host sends
`WMT_RESET_CMD` (init_table_3 :801-802, sent :1344) - that RESET frame is the closest
thing to a "download done" message.
### B.4 Timeouts / retries
`WMT_LIB_RX_TIMEOUT 2000` ms core/include/wmt_lib.h:72, divided by `loopCnt=4` when WMT
func on -> 500 ms per wait (core/wmt_ctrl.c:258-259,274); one 4000 ms extension
(wmt_lib.h:74, wmt_ctrl.c:304-306); then `wmt_lib_trigger_assert(..,44)` :331-332.
TX retry when STP wrote 0 bytes (SOC only): 3 x 360 ms core/wmt_core.c:394-405.
BTIF TX: `STP_BTIF_TX_RTY_LMT 10` x `STP_BTIF_TX_RTY_DLY 5` ms, max 2000 B
linux/stp_btif.c:46-49,181-183. chip-id poll 10 x 20 ms (mt6833.c:953,979); goto-idle
10 x 20 ms (:579,590); ADIE flow 3 tries (:1906); patch-info retry 3
(core/wmt_ic_soc.c:1187,1268); init-script single pass core/wmt_core.c:752-755.
## C) Preconditions
Order enforced by `mtk_wcn_consys_hw_reg_ctrl` platform/mtk_wcn_consys_hw.c:628-712:
1. `gConEmiPhyBase` non-zero else abort `-WMT_ERRCODE_EMI_NOT_READY` (:893-897);
   EMI remap regs written by `consys_emi_set_remapping_reg()` mt6833.c:1600-1650.
2. VCN18/VCN28 (+VCN13 for secondary ADIE) rails: mtk_wcn_consys_hw.c:633-663,
   vcn28 switch mt6833.c:1834-1835, regulators mt6833.c:1819-1833.
3. IF pinmux on mtk_wcn_consys_hw.c:636-637, `udelay(150)` :639.
4. `clk_infracfg_ao_ccif4_ap_cg` + `clk_scp_conn_main` enabled mt6833.c:615-627.
5. `CON_STA_REG(0x10003314)=0x1` (`CON_PWR_ON`) mt6833.c:636-638.
6. **CONN CPU in reset**: `ap_rgu_base(0x10007000)+0x18` bit12
   (`CONSYS_CPU_SW_RST_BIT`) =1 with key `0x88<<24` (mt6833.h:68,135-136;
   mt6833.c:553-560), called at mtk_wcn_consys_hw.c:665-666.
7. `consys_hw_spm_clk_gating_enable()` mt6833.c:596-601, then
   `consys_hw_power_ctrl(ENABLE)` :604-644 (2-stage SPM power-on, ack polls
   10 x 0.5 ms :656-688); `mdelay(5)` :810.
8. IP_VER poll -> EMI patch write (mt6833.c:962-976) with reset still asserted.
9. Reset released only after the EMI write: `reset_bit_set(DISABLE)`
   (mtk_wcn_consys_hw.c:701-702, bit12->0, mt6833.c:562-564), then
   `mcu_base+0x600 == 0x1D1E` (mt6833.c:568-593).
So: CPU **in reset during download, out of reset and idling after it**. Peripheral
state that must be live: CCIF4 clock, `CON_PWR_ON`, EMI mapping, VCN rails.
## D) Dependency surface (porting cost: function counts)
- linux/osal.c ~114 funcs (41 KB) - download path uses ~35 `osal_*`: malloc/free,
  memcpy, memcpy_toio/fromio, memset, memcmp, sizeof, sleep_ms, snprintf, str*,
  assert, set/test/clear/test_and_* bit, lock_sleepable/unlock, signal_init,
  wait_for_signal_timeout, trigger_event, get_local_time, dump_thread_state,
  thread_sched_mark/unmark, dbg_assert_aee, array_size (sorted, from
  wmt_core.c+wmt_ctrl.c+wmt_ic_soc.c). All must be reimplemented or stubbed.
- core/stp_core.c ~139 funcs (109 KB) - 26 symbols used: stp_send_data, send_data_raw,
  receive_data, flush_rx_queue, register_event_cb, set_if_tx_type, parser_data,
  open_btif, close_btif, rxcb_register/unregister, is_sdio_mode,
  is_btif_fullset_mode, set_mode, get/set_wmt_trg_assert, is_wmt_last_close,
  dbg_dump_package, assert_flow_get/ctrl, wakeup_consys, set_psm_state.
  core/stp_exp.c (25 funcs) only re-exports the same API.
- core/wmt_lib.c ~165 funcs (89 KB): get_drv_status, set_patch_name/num/info,
  get_patch_info, set_rom_patch_info, get/set_need_update_patch_version,
  trigger_assert, trigger_cmd_signal, mpu_lock_aquire/release, co_clock_flag_get.
- core/wmt_ctrl.c ~79 funcs (35 KB): whole opcode dispatch table :127-165 (host side).
- linux/wmt_dev.c ~34 funcs (51 KB): chardev, `request_firmware` :436-464, patch ioctls
  :1145-1200/:1327-1350, rx waitq.
- HIF: linux/stp_btif.c 13 funcs -> `mtk_wcn_btif_open/write/close/rx_cb_register` in
  drivers/misc/mediatek/btif/common/ (`mtk_btif.c` 13 funcs; `btif_plat.c`,
  `btif_dma_plat.c` are the register-level driver, BTIF base 0x1100C000
  mt6833.dts:874-893). Combo-only alternates (not needed on SOC): stp_sdio.c 46,
  hif_sdio.c 42, stp_uart.c 21 funcs.
- Platform: mt6833.c ~143 funcs, wmt_plat_alps.c ~84, mtk_wcn_consys_hw.c ~65. Path
  calls wmt_plat_pwr_ctrl, wmt_plat_get_soc_chipid, wmt_plat_soc_co_clock_flag_get,
  mtk_wcn_consys_get_options, _get_adie_chipid, _soc_get_emi_phy_add,
  _hw_pwr_on/off/rst, _poll_cpucpr_dump.
Minimum port of the download: WMT header codec + `init_script` engine
(core/wmt_core.c:690-755) + patch reader + EMI mapper + the 9 power steps ~ 25 functions
if STP/BTIF is dropped for a direct EMI path; ~40 plus BTIF if the command channel stays.
## Not determinable from this source
- Real firmware file names/sizes (userspace launcher + `request_firmware`); no
  `soc2_*_ram_mcu_*_hdr.bin` string exists in the kernel.
- How the MCU ROM learns the patch EMI offset: `addressByte` only feeds
  `patchEmiOffset` core/wmt_ic_soc.c:3600 and the buildtime stamp; no register write
  or message carries it -> convention lives in MCU ROM.
- Checksum/CRC semantics: the 2-byte field is skipped, never computed/compared.
- Whether a preloader/bootloader stage programs any CCIF channel (outside this tree);
  `0x1024C000` appears only as the `ap_pccif4` DT range.
