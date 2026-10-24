# Fast iteration workflow (no reflashing)

After the next flash the kernel does not have to be rebuilt for driver or
firmware experiments.  Three mechanisms make that possible.

## 1. `/proc/conn_cycle` - re-run power-on on demand

```
echo 1 > /proc/conn_cycle
dmesg | tail -3
```

Does a real `conn_hw_off()` / `conn_hw_on()` pair with whatever the current
parameters are, then logs `CONN_ON+0x600` and `CPUPCR` (`CONN_OFF+0x104`).

## 2. Runtime knobs

All writable through `/sys/module/mt6833_conn/parameters/`:

| parameter | meaning |
|---|---|
| `minimal` | 1 = only MTCMOS + CON_PWR_ON + chip id + reset + idle poll |
| `steps` | bitmask of extra steps in minimal mode: 1 EMI remap, 2 ROM patch, 4 vendor prep, 8 sleep-protect + WFSYS probe |
| `no_clear` | skip the 5 MB EMI wipe inside the EMI remap step |
| `patch_hdr` | header bytes stripped from a patch blob (default 32) |
| `patch_mode` | 0 all three blobs, 1 MCU only, 2 BT+WiFi only, 3 none |
| `rom_patch_emi_offset` | offset used for the MCU blob |
| `power_cycle` | power the domain down before powering up |
| `wf_pwr_ctl_write`, `wf_cpu_rst_write` | the two MT6895-only WFSYS writes, off by default |
| `ap2conn_hclk` | the MT6895 CLK_CFG_20 write, off (it panics this SoC) |

## 3. `/proc/conn_sweep` - a whole table per boot

```
echo "0 1 2 3 4 5 6 7 13 15" > /proc/conn_sweep
dmesg | grep SWEEP
```

Runs one power cycle per value and logs

```
SWEEP steps=0x1 -> idle600=0x00000000 cpupcr=0xf0056fd4
```

That is how the last bisect was obtained: only `steps` bit 0 (EMI remap) made
the CONN CPU execute (CPUPCR moving inside `0xF00xxxxx`), and bit 1 (ROM patch)
put it back to `0x74`.

## 4. Firmware swaps over the serial console

The initramfs root is a writable ramfs, and `conn_copy_patch()` calls
`request_firmware()` on every power-on, so a new blob is picked up by the next
`/proc/conn_cycle` with no rebuild:

```
powershell -ExecutionPolicy Bypass -File kvm\push_file.ps1 ^
    -Local kvm\..\soc2_2_ram_mcu_1_1_hdr.bin ^
    -Remote /lib/firmware/soc2_2_ram_mcu_1_1_hdr.bin
```

`push_file.ps1` base64-chunks the file over COM20 at 921600 and decodes it on
the device.

## What still needs a flash

Only the kernel itself: driver source changes, config changes, or a new
initramfs.  Everything else - every register experiment, every firmware
variant, every step combination - runs from the shell.
