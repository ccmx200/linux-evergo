# Register diff: working vendor Android vs this mainline port

Two sources, both captured on the handset:

* vendor: `connreg.ko` built against the stock kernel and loaded with `insmod`
  under KernelSU root, with WiFi enabled and a scan forced so the CONN CPU was
  powered and idle.  Full dump: `vendor-regs/connreg-all.txt`.
* mainline: this port's own dumps (`conon`, `topmisc`, `hif70`, `spm`, `rgu`).

## What already matches (so these are NOT the problem)

| window | value | notes |
|---|---|---|
| `conn_on +0x000` | `00008a00 00008a00 00000000 00000000 ffff0000` | identical |
| `conn_on +0x120` | `... fffe0010 00000000 00000000 000000aa ffffffff` | identical |
| `conn_on +0x140` | `00000001 00006000 ... 84840000` | identical |
| `conn_on +0x180` | `00033fff 0001ffff 0001ffff ...` | identical |
| `conn_on +0x200` | `... 0000000f 55555555 ffffffff` | identical |
| `conn_on +0x500` | `00000000 f0170000 f02a0000` | BT/WiFi EMI entry addresses - identical |
| `spm +0x304` | `0000000d` | CONN MTCMOS state - identical |
| `spm +0x16c` / `+0x170` | `f83a1bff` / `f83a1bff` | power status - identical |
| `topmisc +0x130` | `f0000100` | identify_adie - identical (after the 0x6631 fix) |
| `topmisc +0x168` | `00000001` | access_emi_hw_mode - identical (after the base-address fix) |
| `rgu +0x18` | `00000000` | CONN CPU reset released - identical |

Also worth recording: `conn_off +0x100` reads `0x87654321` **in the working
vendor system too**.  An earlier note in this project treated that value as an
"unmapped address" symptom; it is not.

## What differs - and all of it follows from the CPU not running

| window | vendor (working) | this port |
|---|---|---|
| `conn_on +0x160` | `00008030 00000000 00022052 10001800` | `00000000 00000000 00000000 00000000` |
| `conn_on +0x4c4` | `00022052` | `00000000` |
| `conn_on +0x600` | **`00001d1e`** | `00000000` |
| `conn_on +0x100` | `807f0026` (+0x110 = 4) | `807f0c30` (+0x110 = 0) |
| `conn_on +0x440` | `80000080` | `90000002` |
| `conn_on +0x560` | `80000104 00000281 00404e39 ...` | `0a115afe 00000000 0a115afe ...` |

`0x22052` is the CONN CPU's program counter (it also appears at `0x18007104` in
the vendor's own idle-poll logging, where it walks
`0x0 -> 0xf0000030 -> 0xf0003cea -> 0x22052` before the idle signature lands).

`0x18002440` holds `0x90000002` in this port because that is what
`consys_bus_timeout_config()` writes; the vendor has `0x80000080` there in the
steady state, i.e. the CONN side rewrote it after power-on.  That is a
consequence, not a cause.

## The conclusion this diff supports

Every configuration register this project set out to replicate now matches the
working system.  The only remaining difference is that **the CONN CPU never
executes**: its program counter stays 0, the idle signature never appears, and
the registers that the running CPU would have written (`+0x160`, `+0x22052`,
`+0x600`) stay zero.

So the missing piece is not a value this port writes wrongly - it is something
that must happen for the CPU to start fetching instructions at all.
