# MT6833 CONN/WFSYS power-on — register-level findings

Tree: `/root/linux-evergo-new` (Linux 7.3.0-rc4). Vendor sources are in-tree:
`drivers/misc/mediatek/connectivity/common/common_main/platform/` (CONN driver) and
`drivers/net/wireless/mediatek/gen4m/` (vendor gen4m WLAN, being ported).
Port under development: `drivers/soc/mediatek/mt6833-conn.c` (+ `mt6833-conn-probe.c`).

Legend: **[V]** = read out of source / measured on device, **[I]** = inferred.

---

## 0. Headline

**MT6833 is a Connac1x part ("SOC2_1X1", chip id 0x0001, no CR4) and it has no
WFSYS top-power gate in the AP address map.** Neither vendor driver ever writes
0x18000010 / 0x18000120 / 0x18000140 for this chip:

* vendor CONN driver `platform/mt6833.c` powers WFSYS only as part of the single
  CONN MTCMOS domain (ccif clock + `conn` clock + PERICFG CON_PWR_ON), then polls
  the chip id, releases the AP_RGU CPU reset and polls `0x18002600 == 0x1D1E`;
* vendor WLAN `chips/soc2_1x1/soc2_1x1.c` has **no** `.wmmcupwron` and no WFSYS
  address at all; the only code path that would do a WFSYS keyed write
  (`asicConnac2xPwrOnWmMcu()` → `wf_pwr_on_consys_mcu()`) is compiled out because
  `CFG_SUPPORT_CONNINFRA=0` for SOC2_1X1.

The 0x18000010/+0x120/+0x140 recipe is the **Connac2x/3x** scheme (SOC5_0/SOC7_0 =
MT6893/MT6895, and MT7921/MT7986 upstream). `mt6833-conn.c` currently mixes three
different generations, which is why nothing sticks:

| what the port uses | where it really comes from | that chip's layout |
|---|---|---|
| `0x18000000` + key 0x5746 | SOC3_0 (MT6765/6768/6785) pwr ctl at RGU+0x0 | +0x0 pwr, +0x10 CPU rst, +0x18 WFSYS rst |
| `0x18000120` CPU reset | SOC5_0/SOC7_0 (MT6893/MT6895), MT7986 | +0x10 pwr, +0x120 CPU rst, +0x140 WFSYS rst |
| `0x180602CC` bit 2 | SOC3_0 status `wfsys_rgu_off_hreset_rst_b` | bit 2 on SOC3_0, bit 30 on SOC5_0/SOC7_0 |

Device-observed **[V]**, recorded in the port's own comments: `0x18000000` reads a
constant `0x42424242` and stays that way even with the keyed write; `0x18000120`
writes do not stick; `0x18000110` is zero.

---

## 1. Inventory: every WFSYS-related register the local tree uses

### 1.1 `drivers/soc/mediatek/mt6833-conn.c` (the port)

| define | addr | value / mask | file:line |
|---|---|---|---|
| `CONN_WF_ON_TOP_PWR_CTL` | 0x18000000 | key `0x57460080`, clear mask `0x0000FF7F` | :211,216,217 |
| `CONN_WF_CPU_RST_B` | 0x18000120 | bit0 | :212 |
| `CONN_WF_CPU_RST_B_ALT` | 0x18000010 | bit0 (misnamed: this is pwr ctl on Connac2x) | :213,1330 |
| `CONN_WF_PWR_STATUS` | 0x180602CC | polled with `BIT(2)` | :214,218 |
| `CONN_WF_MONFLG_OUT` | 0x18060B10 | expect `0x1D1E` | :215,219 |
| `CONN_WFSYS_BASE` | 0x18000000 | 8 MiB ioremap; CPU rst at +0x120, monflg at +0x60B10 | :260-264 |
| SPM `CONN_SPM_PWR_CON` | SPM+0x304 | BIT(0) RST_B, BIT(1) ISO, BIT(2) PWR_ON, BIT(3) PWR_ON_2ND, BIT(4) CLK_DIS | :155-161 |
| SPM status | +0x16c / +0x170 | BIT(1) | :157-158 |
| AP_RGU CPU SW rst | TOPRGU+0x18 | BIT(12) \| key `0x88<<24`; also BIT(9) whole-CONNSYS | :243-248 |
| PERICFG CON_PWR_ON | 0x10003000+0x314 | write 1 (CON_PWR_ON=bit0, CON_SW_READY=bit1) | :286-288 |
| chip id | 0x180b1000+0x10 | expect `0x100a0000`, 10 × 20 ms | :294-296 |
| idle loop | CONN_ON(0x18002000)+0x600 | expect `0x1D1E`, 400 ms | :1766-1778 |
| INFRA_AO gate | 0x10001000+0x80 | OR `0x88800000` (default `infra_cg_extra`) | :816-817, 925-931 |
| AP2CONN hclk mux | TOPCKGEN CLK_CFG_20 | via `CLK_CFG_20_SET/CLR`, `UPDATE2=0x40000` | :860-870, 1004 |
| vendor prep | 0x180c1000+0x168 (EMI hw mode), CONN_ON+0x504/0x508 = `0xF0170000`/`0xF02A0000`, 0x180c1000+0x40 bit7, pinmux 0x10005000+0x480/0x490, 0x11EA0000 pads, SPM+0x0 |= `0x0B16<<16\|BIT(0)` | :150-200, 1425-1520 |

Notes in the port worth keeping: writing TOPCKGEN `CLK_CFG_20` **panicked the
handset** (comment at :968-972); `CONN_HIF_ON` answers `0xdeadfeed` unless
INFRA_AO+0x80 gates are set (:925-928).

### 1.2 `drivers/net/wireless/mediatek/gen4m` — Connac2x/3x recipes (not MT6833)

`soc7_0/soc7_0.c` (MT6895-class) **[V]**:

| step | register | value |
|---|---|---|
| wake conn_infra | 0x180601A4 | `\|= BIT(0)`, then udelay 200 |
| IP version | 0x18011000 | poll `== 0x02050100`, 10 × 1 ms |
| conn_infra ready | 0x18001210 | poll bit16, 10 × 0.5 ms |
| PTA clocks | 0x18012064 / 0x18012074 | `0x01010101` / `0x00000101` |
| CPU reset assert | 0x18000120 | `&= ~BIT(0)` |
| WF slp prot off | 0x18001440 | `&= ~BIT(0)` |
| **power on** | **0x18000010** | `(v & ~(0xFFFF0000\|0x80)) \| (0x5746<<16) \| BIT(7)` |
| status | 0x18060A10 | poll bit30 == 1, 10 × 0.5 ms |
| slp status | 0x18001444 | poll bits31&29 == 0, 100 × 0.5 ms |
| monflg cfg | 0x18060B00/B04 | `\|= BIT(0)`; sel = `0xC` |
| CPU reset release | 0x18000120 | `\|= BIT(0)` |
| **idle** | **0x18060B10** | poll `== 0x1D1E`, 1000 × 1 ms; fail dumps 0x184C1604 |
| power off | 0x18000010 | `(v & 0xFF7F) \| 0x57460000`, then poll 0x18060A10 bit30 == 0 |

`soc5_0/soc5_0.c` (MT6893-class) **[V]**: same 0x18000010 keyed write; slp ctrl
0x18001540 bit0, slp status 0x18001544 bits 31/29; status **0x180602CC bit30**;
idle **0x18060260 == 0x1D1E**; power-off toggles the WFSYS semaphore reset
**0x18000140** (`0` then `1`).

`soc3_0/soc3_0.c` (MT6765/6768/6785) **[V]**: pwr ctl at **0x18000000** keyed
0x57460080; CPU rst at **0x18000010**; WFSYS rst at **0x1800018**; status
**0x180602CC bit2**.

### 1.3 gen4m `soc2_1x1` (the actual MT6833 chip code) — no WFSYS at all

`soc2_1x1.c` has only a bus mapping entry `{0x820D0000, 0x000AE000, 0x1000}
/* WFSYS_ON */` (:71) and no `.wmmcupwron`, no `.pwrondownload` hook.

### 1.4 Upstream mainline (closest authoritative code)

* `mt76/mt7915/soc.c` — MT7986 embedded WFSYS: `MT_TOP_WFSYS_PWR` (0x18000010),
  mask `EN|KEY`, value `KEY | BIT(7)`, poll `MT_TOP_WFSYS_RESET_STATUS`
  (0x180602CC) bit30; `MT7986_TOP_WM_RESET` (0x18000120) bit0 then poll
  `MT_TOP_CFG_ON_ROM_IDX` (0x184C1604) == 0x1d1e; clocks `"mcu"`, `"ap2conn"`;
  reset `"consys"`; `MT_CONN_INFRA_WF_SLP_PROT` 0x18001540/44.
* `mt76/mt792x_dma.c` — CONNAC2X discrete MT7921: WFSYS reset = **0x18000140**
  bit0 (`WFSYS_SW_RST_B`), status bit4 (`WFSYS_SW_INIT_DONE`), no power-gate write.
* `mt76/mt7996/init.c` — MT7996: `MT_WF_SUBSYS_RST` 0x70028600 / `MT_WF_L05_RST`.

There is **no upstream MT6833 CONN driver or CONN genpd**: `mt6833-pm-domains.h`
in this tree (with the CONN domain entry) does not exist upstream — elixir returns
404 for v7.3-rc4, and git.kernel.org 404 for master.

---

## 2. Register semantics of CONN_INFRA_RGU (Connac2x/3x only)

Base 0x18000000 (`CONN_INFRA_RGU_ON_BASE`), from
`gen4m/wlan/core/gen4m/include/chips/coda/soc7_0/conn_infra_rgu_on.h`.

### +0x010 — `WFSYS_ON_TOP_PWR_CTL`
```
[31:16] WFSYS_ON_TOP_WRITE_KEY   0x5746   (write-only; reads back 0)
[7]     WFSYS_ON_TOP_PWR_ON      1 = request WFSYS power
[6]     PWR_ACK                  read-only ack (MT7986 mainline MT_TOP_PWR_ACK_MASK)
```
**Write-key convention:** the key must be present in bits[31:16] of the *same*
32-bit write that carries the payload; it is not a separate "unlock" register and
it is not retained (reads give 0). A write without 0x5746 is silently ignored.
Read-modify-write is required, with the key/payload masked out of the read value
first, because the read value has 0 in [31:16] and a stale ACK in [6].

**The write that actually asserts power** is a single store of
`(v & ~(0xFFFF0000 | BIT(7))) | (0x5746 << 16) | BIT(7)`.
Vendor and mainline do exactly this (soc5_0.c:1134, soc7_0.c:1211, mt7915/soc.c:950).
Power-off is the same write with bit7 = 0 and the key still present.

### +0x120 — `WFSYS_CPU_SW_RST_B`
```
[0] 1 = CPU reset released (running), 0 = CPU held in reset
```
The WFSYS CPU must be held in reset (bit0 = 0) *before* the +0x10 power write and
released (bit0 = 1) *after* the power-good poll; the ROM then runs and the idle
signature appears. Upstream MT7986 does the same on this register
(`mt7986_wmac_wm_enable`), then polls 0x1d1e.

### +0x140 — `WFSYS_SW_RST_B`
```
[0] 1 = WFSYS subsystem (semaphore/logic) out of reset
[4] WFSYS_SW_INIT_DONE  (status, read-only) — upstream mt792x uses this as the ack
```
Used to reset the whole WFSYS (soc5_0 power-off pulses it 0→1), and by upstream
mt7921 as the only WFSYS control it has (`addr = is_connac2 ? 0x18000140 : 0x7c000140`).

### Status / ack registers (Connac2x/3x)
| signal | register | bit |
|---|---|---|
| `wfsys_rgu_off_hreset_rst_b` | 0x180602CC (SOC5_0) / 0x18060A10 (SOC7_0) | 30 (bit 2 on SOC3_0) |
| PWR_ACK | 0x18000010 | 6 |
| WFSYS_SW_INIT_DONE | 0x18000140 | 4 |
| conn_infra bus-sleep-protect | 0x18001444 | 31 (wf2conn rdy), 29 (conn2wf rdy), 30 (hw_en) |
| CONN CPU in ROM idle | 0x18060B10 / 0x18060260 / 0x184C1604 | value `0x1D1E` |

### Order that works (Connac2x/3x)
1. `0x18000120 &= ~1` (hold CPU reset)
2. `0x18001440 &= ~1` (drop WF↔CONN sleep protect)
3. `0x18000010 = key|BIT(7)`
4. poll `0x18060A10[30]` (or `0x180602CC[30]`) == 1
5. poll `0x18001444` bits 31/29 == 0
6. `0x18000120 |= 1` (release CPU)
7. poll `0x18060B10 == 0x1D1E`

---

## 3. MT6833: what actually powers WFSYS (vendor-verified)

MT6833's CONN is one MTCMOS domain; WFSYS/BTSYS sit inside it. Vendor order
(`mtk_wcn_consys_hw.c:640-720`, confirmed by the vendor ftrace order in
`kvm/vendor-full-sequence.txt`):

| # | action | register / value | source |
|---|---|---|---|
| 0 | rails VCN18/VCN33_1, clk_buf, pinmux, ADIE id | 0x180c1130 bit8, RF SPI 0x180c6000+0x50/54/58 | mt6833.c:486,1120 |
| 1 | **assert CONN CPU reset** | `0x10007018 \|= BIT(12) \| (0x88<<24)` | mt6833.c:555-560 |
| 2 | SPM power-on config clock | `SPM+0x000 \|= BIT(0) \| (0x0B16<<16)` | mt6833.c:599 |
| 3 | **ccif clock** | `CLK_IFRAO_CCIF4_AP` | mt6833.c:615 |
| 4 | **conn clock** (`clk_scp_conn_main`, "conn") — *this is the CONN domain gate* | `clk_prepare_enable` | mt6833.c:430,622 |
| 5 | **AP→CONN power request** | `PERICFG+0x314 = 0x1` (CON_PWR_ON) | mt6833.c:636 |
| 6 | chip alive | poll `0x180b1010 == 0x100A0000`, 10 × 20 ms | mt6833.c:951-975 |
| 7 | ROM patch / entry addresses | `CONN_ON+0x504 = 0xF0170000`, `+0x508 = 0xF02A0000` | mt6833.c:1023 |
| 8 | bus timeout | `0x18002440 = 0x90000002` | mt6833.h |
| 9 | **release CONN CPU reset** | `0x10007018 &= ~BIT(12)`, key 0x88<<24 | mt6833.c:562-566 |
| 10 | **idle check** | poll `0x18002600 == 0x1D1E`, 10 × 20 ms | mt6833.c:577-595 |

If the clock/power-domain API path is unavailable, the vendor's raw-SPM equivalent
(`CONSYS_PWR_ON_OFF_API_AVAILABLE == 0`) is:

* `SPM+0x304`: set BIT(2) PWR_ON → poll `SPM+0x16C` BIT(1) → set BIT(3) PWR_ON_2ND
  → poll `SPM+0x170` BIT(1) → clear BIT(4) CLK_DIS → clear BIT(1) ISO → set BIT(0) RST_B
* whole-CONNSYS reset: `TOPRGU+0x18` bit9 clear, bit12 clear, key 0x88<<24

Device-verified value **[V]**: in the working dump (`kvm/conn_on.txt`) `SPM+0x300`
and `SPM+0x304` both read `0x0000000d` = RST_B|PWR_ON|PWR_ON_2ND, i.e. CONN on with
CLK_DIS and ISO clear; `TOPRGU+0x18` reads `0x00000000` (key field reads back 0).

### Clocks: what a mainline port misses
* `ccif` (`CLK_IFRAO_CCIF4_AP`) — already in the DT (`mt6833.dtsi:898`).
* **`conn` = `clk_scp_conn_main`** — the vendor's "ctrl conn_power_on/off" clock
  (mt6833.c:173,430). Upstream has no MT6833 CONN genpd, and no `scp_conn` gate in
  `clk-mt6833-*`, so a naive mainline port silently skips the domain gate. The
  correct mainline model is a CONN power domain (as the local
  `drivers/pmdomain/mediatek/mt6833-pm-domains.h` does: sta BIT(1), ctl 0x0304,
  sram_pdn 8 / ack 12, 3 bus-protect steps), otherwise do the SPM writes by hand.
* **AP2CONN hclk / CONN bus gates** — the vendor toggles them in HW via its pg
  driver; on this board `INFRA_AO+0x80 |= 0x88800000` is what makes CONN_HIF_ON
  answer instead of `0xdeadfeed` **[V, port]**. Writing TOPCKGEN `CLK_CFG_20`
  instead **panics** the handset **[V, port]**.
* There is no `conn_wf`/`scp_conn_wf` gate for WFSYS in the MT6833 vendor tree
  (`conn_wf` only appears as pinmux `CONN_WF_CTRL*` and in the Connac2x
  `WF_SLP_PROT` pair 0x18001540/44, which MT6833 does not use).

### Status / ack on MT6833
* domain: `SPM+0x16C`/`SPM+0x170` BIT(1)
* chip alive: `0x180b1010 == 0x100A0000`
* **WFSYS CPU actually running: `0x18002600 == 0x1D1E`** (CONN_ON+0x600,
  `CONSYS_COM_REG0`; same 0x1D1E signature other chips expose at 0x18060B10)
* no `wfsys_rgu_off_hreset_rst_b` bit exists on MT6833 — nothing to poll at
  0x180602CC / 0x18060A10

---

## 4. Mainline vs vendor MT6833

| aspect | "typical" mainline (mt7986/mt7921 in mt76, plus a genpd CONN) | vendor MT6833 |
|---|---|---|
| CONN domain | `power-domains` + genpd/scpsys, or `pm_runtime` | `clk_scp_conn_main` ("conn") clk_prepare_enable |
| bus clock | CCF clocks `"mcu"`, `"ap2conn"` + `MT_CONN_INFRA_HW_CTRL` bit0 + `0x180601A0` wakeup pulse | done in HW by pg driver; `INFRA_AO+0x80` gates on this board |
| WFSYS power switch | `RGU+0x10 = 0x5746<<16 \| BIT(7)`, poll `0x180602CC[30]` | **none** — comes up with the domain |
| WFSYS CPU reset | `RGU+0x120[0]` (MT7986) / `RGU+0x140[0]` (MT7921) | `TOPRGU+0x18[12]` (AP_RGU), key `0x88<<24` |
| alive check | `0x1D1E` at 0x18060B10 / 0x184C1604 / 0x18060260 | `0x1D1E` at 0x18002600, plus `0x100A0000` at 0x180b1010 |
| sleep protect | `0x18001540/44` | AXI/AHB protect via INFRA_AO 0x2A4/0x2AC, sta 0x228/0x258 |
| data source | upstream `linux` (elixir) | vendor alps drop in this tree |

---

## 5. Verified vs inferred

**Verified (source + device):**
* MT6833 vendor code contains zero WFSYS/RGU power writes; SOC2_1X1 has no
  `.wmmcupwron`; `CFG_SUPPORT_CONNINFRA=0` for this chip.
* Vendor MT6833 sequence: reset assert → SPM clk → ccif+conn clk → PERICFG 0x314=1
  → `0x100A0000` → reset release → `0x1D1E` at 0x18002600.
* 0x18000010/+0x120/+0x140 semantics and the 0x5746 same-write key rule
  (soc5_0/soc7_0 coda headers + mt76 mainline).
* 0x18000000 reads constant `0x42424242`, 0x18000120 does not stick (port).
* `SPM+0x304 = 0xd` and `TOPRGU+0x18 = 0` on the working device.
* Topckgen `CLK_CFG_20` write panics; `INFRA_AO+0x80 |= 0x88800000` fixes deadfeed.

**Inferred:**
* That the MT6833 AP map simply does not decode 0x18000000–0x180001FF (the
  constant 0x42424242 is consistent with an unmapped/dummy slave, but no MT6833
  register manual was available to confirm the decode).
* That the CONN MTCMOS domain alone is sufficient for WFSYS — implied by the
  vendor bringing WiFi up without any WFSYS register write, but the vendor does it
  through a pg-domain clock that mainline must reproduce exactly.
* The exact meaning of `INFRA_AO+0x80` bits 31/27/23 (empirical, not documented).

## 6. Recommended next steps

1. Drop 0x18000000 / 0x18000120 / 0x18000140 from the MT6833 driver (dead region,
   three-generation mix-up). Keep them behind a "connac2x" flag if ever reused.
2. Model the CONN domain properly: either enable a `conn` clock (needs a
   CLK_SCP_CONN_MAIN gate) or rely on the local `mt6833-pm-domains.h` CONN entry;
   verify `SPM+0x304` ends at `0xd`.
3. Sequence: rails → ccif → domain on → SPM+0x0 pwr-cfg clk → assert AP_RGU bit12 →
   PERICFG 0x314 = 1 → poll 0x180b1010 == 0x100a0000 → INFRA_AO+0x80 gates →
   release AP_RGU bit12 → poll 0x18002600 == 0x1D1E.
4. Treat `0x1D1E` at 0x18002600 as the WFSYS-alive milestone; only then start the
   gen4m firmware download (the WLAN probe otherwise sees a mute MAC).

## Sources

Upstream code (read locally from the 7.3-rc4 tree; canonical URLs):
* https://elixir.bootlin.com/linux/latest/source/drivers/net/wireless/mediatek/mt76/mt7915/soc.c
* https://elixir.bootlin.com/linux/latest/source/drivers/net/wireless/mediatek/mt76/mt7915/regs.h
* https://elixir.bootlin.com/linux/v7.2.11/source/drivers/net/wireless/mediatek/mt76/mt792x_dma.c
* https://elixir.bootlin.com/linux/latest/source/drivers/net/wireless/mediatek/mt76/mt792x_regs.h
* https://elixir.bootlin.com/linux/v7.3-rc4/source/drivers/pmdomain/mediatek/mt6833-pm-domains.h (404 — not upstream)

Vendor code (same content as the in-tree copies; public mirrors):
* https://github.com/MotorolaMobilityLLC/vendor-mediatek-kernel_modules-connectivity-wlan-core-gen4-mt79xx
* https://github.com/rsuntk-oss/mtk_connectivity_module
* https://github.com/xiaomi-mt6833-dev/kernel_xiaomi_mt6833
* https://github.com/techyminati/alps-4.19
