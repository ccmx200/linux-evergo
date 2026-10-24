## Final state (end of 2026-10-18, ~15 h of work)

### Verified working
* CONN power-on, MTCMOS, reset, clocks: MCU reaches ``0x18002600 = 0x1d1e``
* ROM patches at EMI +0x0 / +0x170000 / +0x2a0000
* EMI remap, entry registers (``0x18002508 = 0xF02A0000``)
* IRQ SPI 377 -> virq 159
* DRIVER OWN in ~2 ms
* firmware sections and EMI copy identical to the vendor (measured live)
* inband ECO read -> ``HW[0x8a00] SW[0x8a00]`` (same as vendor)
* **STP link verified**: clk_en/clk_138/clk_26/clk_dis (0x08), WMT_RESET (0x07),
  set_stp (0x04) all answered; payloads byte-identical to the vendor's events.
  Regression sentinels:
      80 40 08 00 | 02 08 04 00 00 00 00 01 | 00 00    (clk)
      80 40 06 00 | 02 04 02 00 00 03      | 00 00    (set_stp)

### The blocker, and the pattern that localises it

    set_stp / RESET / clocks  (MCU-local)            -> answered
    ACCESS_REG                (MCU-local registers)  -> answered
    ant_swap / rf_cal         (0x14, WiFi RF)        -> 0 bytes
    WIFI_START                (start WiFi firmware)  -> 0 bytes, 28525 empty polls

Everything needing the **WiFi subsystem** is silent; everything the MCU handles
on its own works.  The MCU is alive throughout: ``set_stp`` still answers 80 s
after WIFI_START failed, with CPUPCR 0x22052 and ON_MISC 0x13.

### Eliminated (all measured, not assumed)
STP frame format, TX ring, RX path, wait mechanism (CFG_ENABLE_FW_DOWNLOAD_ACK=1
on both sides), firmware file (md5 identical), command parameters, entry address
origin, PDA, STP type field, 0x14 payload bytes.

### Where the remaining work is
The vendor's 0x14 replies exist (it waits for a 6-byte event and only skips the
content comparison for opcode 0x14), but they happen at ~6.4 s during
``wmt_core_stp_init``, before the earliest userspace hook (~7.7 s).  Capturing
them needs an earlier probe:
  1. rebuild the vendor kernel with ``-D WMT_DBG_SUPPORT=1`` (tree configured);
  2. put the tracer in ``vendor_boot``;
  3. or find the WFSYS power-on sequence that makes the WiFi subsystem answer.

Our ``conn_probe_wfsys`` currently only pulses a CPU software reset; the actual
power-on writes (0x18000000, key 0x5746) are gated off because the values came
from MT6895 and never brought the block up on this chip (pwr_status stayed 0).
