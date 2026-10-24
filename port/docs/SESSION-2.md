# Session log — 2026-10-18 (evening), multi-agent round

## Established by measurement

### The MCU answers ROM opcodes only

| opcode | nature | result |
|---|---|---|
| `0x02` chip id (0x11) | ROM | answered, `80 40 05 00 02 02 01 00 00 00 00`, byte-identical to `WMT_SET_CHIP_ID_EVT` |
| `0x04` set STP | ROM | answered, 12 bytes, identical to `WMT_SET_STP_EVT` |
| `0x07` WMT reset | ROM | answered |
| `0x08` clock commands | ROM | answered, 14 bytes, identical to `WMT_SET_MCU_CLK_EVT` |
| `0x14` ant swap / RF cal | firmware | **silent** |
| `INIT_CMD_ID_WIFI_START` | starts the WiFi image | **silent** (28525 empty polls) |

Per the vendor's own protocol enum (`core/include/wmt_core.h:406-415`) it stops at
`OPCODE_INT (8)`: `0x14` is not a ROM opcode at all.  So the MCU is running its
boot ROM and the downloaded firmware is not executing.

### Verified identical to the vendor

* ROM patch EMI offsets and sizes — the vendor prints them itself
  (`[Rom Patch]Name=...,EmiOffset=...,Size=...`):
  BT `0x170000`/`0xa7a10`, WiFi `0x2a0000`/`0x3ad8c`, MCU `0x0`/`0x2d630`.
  All three match this port exactly.
* firmware file md5 `f271f634c89968657e9256b81d573977` (732688 bytes)
* firmware bytes read back from EMI match the file at offset `0x34d0`
* `wlanImageSectionConfig` / `wlanDownloadEMISection` / `wlanConfigWifiFunc`
  arguments (measured live on the vendor via kprobes)
* `INIT_CMD_PACKET_TYPE_ID`, `START_OVERRIDE_START_ADDRESS`,
  `INIT_CMD_ID_*` — `nic_init_cmd_event.h` is byte-identical between the trees
* `CFG_ENABLE_FW_DOWNLOAD_ACK = 1` on both sides

### Newly implemented this round

* **Build-time stamping** into the EMI coredump area
  (`EMI+0x88300` BT, `+0x88310` WiFi, `+0x88320` MCU), exactly as
  `wmt_ic_soc.c:3637-3648` does.  Confirmed on device: `[2307...]` and `[2023...]`
  written.  Did **not** unblock `0x14`.
* `chipid`/`adie` reply logging.

### Negative results (measured, keep them)

* `power_cycle=0` → `DRIVER_OWN_FAIL` (reason 1).  The off/on cycle before
  power-up is **required**; the vendor's claim that it never cycles does not
  apply to how this port reaches the block.
* MT6833 has **no WFSYS power gate** in the AP map (agents 2 and 3 agree): the
  `0x18000000` / `0x18000120` / `0x18060B10` writes in this driver are SOC3_0 /
  SOC5_0 / SOC7_0 addresses and are now disabled by default.
* `0x88000` is the EMI **coredump** offset, not a patch offset; moving the MCU
  patch there breaks the boot (`0x18002600` reads 0).

## Remaining unknown

Why the ROM does not apply/execute the patch that is provably in the right place
with the right bytes.  Everything the AP can observe is correct.
