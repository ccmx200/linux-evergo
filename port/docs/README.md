# gen4m WLAN port - Linux 7.3 API drift, solved

## Where it stands now (measured on the handset)

The WLAN driver is **built in**, probes on real hardware, and reaches the
WLAN MAC.  Measured chain, latest boot:

    mt6833-conn: CONN up, chip id 0x100a0000 after 1 tries
    mt6833-conn: CONN CPU idle check: CONN_ON+0x600 = 0x00000000 (NOT in idle loop)
    mt6833-conn: CONN CPU state: CONN_ON+0x16c = 0x00000000, CONN_OFF+0x104 = 0x00000070
    wmt-shim:    CONN subsystem up (chip id ok: 1)
    wmt-shim:    calling wlan probe callback
    [wlan]:      halSetDriverOwn: DRIVER OWN Done[2051913 us]
    [wlan]:      wlanAdapterStart: nicpmSetDriverOwn() failed!
    [wlan]:      wlanProbe: probe failed, reason:3

`phy0` exists in `/sys/class/ieee80211` and `/proc/net/wlan` is registered -
the wiphy and the driver's proc node come up - but **the WiFi MAC never
answers**.

### What the measurement proves

`CONN_ON+0x600` is the register the vendor polls for `0x1D1E`
(`consys_polling_goto_idle()`, platform/mt6833.c:568-594) to confirm the CONN
CPU reached its ROM idle loop.  It reads **0**, so the CONN CPU is not
executing.  `CONN_ON+0x16c` - the vendor's `connsys_on_domain_pwr_ack` - also
reads **0**, while the chip id (a static register) reads correctly.

So: the CONN block's config registers are reachable, but the CPU inside it
never runs, and with it the WFSYS/WiFi CSR block stays dead.  That is why the
WLAN driver's `DRIVER_OWN` handshake times out and every WiFi CSR read returns
a constant `0x00030001`.

Two candidates remain, and they are the next piece of work:

1. ~~**CONN CPU clock.**~~ **Resolved, and it is not the problem.**  The vendor's
   `"conn"` clock is `<&scpsys SCP_SYS_CONN>` (vendor mt6833.dts:929-931), i.e.
   it is *the CONN MTCMOS power gate itself* - `PGATE(SCP_SYS_CONN, "PG_CONN",
   ..., SYS_CONN)` at drivers/clk/mediatek/clk-mt6833-pg.c:4287, register
   `CONN_PWR_CON` = SPM 0x10006000 + 0x304 bit 2 - not an infracfg_ao gate.
   This driver already writes exactly that as part of power-on and the SPM
   status bit comes back set, so there is nothing to add.
2. **ROM patch / MCU firmware download.**  mt6833.c declares
   `consys_dl_rom_patch()`; the vendor's WMT core downloads
   `soc2_2_ram_mcu_1a_1_hdr.bin` over CCIF before WFSYS comes alive.  The
   idle-loop check is most likely unattainable until that runs.

Also ruled out as the blocker: the other vendor steps between
`power_ctrl(ENABLE)` and the reset release are refinements, not the CPU boot
path.  `consys_ahb_clock_ctrl()` is a no-op for MT6833 (mt6833.c:946-949),
`consys_wifi_ctrl_setting()` only reprograms WF_CTRL2 pinmux on the secondary
A-die, `consys_bus_timeout_config()` just enables an AHB timeout, and
`consys_set_xo_osc_ctrl()` tunes the XO stable time.

So the remaining work is the WMT firmware-download protocol over CCIF
(`AP_CCIF4`, 0x1024c000), which is what actually starts the CONN CPU.

### What was added this round, and what it did not fix

* `mt6833_conn_check_clock()` now restores CONN_OFF after its diagnostic write
  (it used to leave `0x30000` behind).  Correct, but it was not the root cause.
* `mt6833_conn_power_on()` now pulses the CONN CPU reset exactly like the
  vendor (`mtk_wcn_consys_hw_reg_ctrl()`: assert at :665 before
  `power_ctrl(ENABLE)`, release at :701 after the chip id answers;
  `consys_hw_reset_bit_set()` writes AP_RGU + 0x18, bit 12, key 0x88).
  Correct per the vendor order, but the CPU still does not reach the idle
  loop - consistent with the clock/firmware explanation above.
* The CONN CPU idle-loop check itself, which is what produced the measurement.

`consys_ahb_clock_ctrl()` is a no-op for MT6833 (mt6833.c:946-949), so nothing
was needed there.

The probe self test deliberately keeps the block powered (a documented
bring-up accommodation, see `fix_keep_powered.py`): power-off followed by
power-on reads the chip id back as `0x00030001`, so the reference is handed to
the first consumer instead.  The block therefore stays powered for the life of
the system until a real off->reset->on sequence is implemented.

## Earlier state: API drift, solved

The driver compiles with zero errors and links into vmlinux.  It cannot yet
produce a `.ko`: `make O=out M=<in-tree dir> modules` passes modpost but dies
in `__modfinal` with "No rule to make target 'wlan_6833_axi.ko'" (the O=
objtree and the in-tree M= object directory do not line up).  Building it in
sidesteps that and is simpler to operate on the handset, so
`CONFIG_MTK_WLAN_GEN4M=y`.

## The blocker that was reported as "vendored header does not exist"

`common/common_main/include/wmt_exp.h:23` includes `<mtk_wcn_cmb_stub.h>`, and
the previous analysis concluded the file did not exist in the vendor tree and
that a stub had to be invented.

It does exist, verbatim and unmodified, in the vendor tree:

    drivers/misc/mediatek/include/mt-plat/mtk_wcn_cmb_stub.h
    md5 a7ecc8e61937a774ebc2259f3941c686, 204 lines

The catch: the copy of the vendor tree on the **Ubuntu-24.04** WSL distro is a
trimmed one that only has `drivers/misc/mediatek/connectivity/`, so
`include/mt-plat/` is not there.  The full tree on the **archlinux** distro has
it (an identical copy, same md5, is also in `/root/kernel_mt6893/`).

So no stub was invented.  The genuine header was copied next to `wmt_exp.h`
(a directory already on the `-I` list) as `modified-sources/mtk_wcn_cmb_stub.h`.

Verified before installing: the target tree has no other definition of
`enum COMBO_IF`, `enum CMB_STUB_AIF_X`, `enum CMB_STUB_AIF_CTRL`,
`struct sdio_ops`, `struct _CMB_STUB_CB_` or `mt_sdio_ops`, so there is no
clash.

## What was wrong, and what each fix is grounded in

45 compile errors, then 3, then 1, then 0.  All of them are 4.14 -> 7.3 API
drift, each checked against the actual 7.3 source tree rather than assumed.

| symbol / construct | 7.3 reality | fix |
|---|---|---|
| `strncpy()` | removed from `include/linux/string.h` | `wlan_strncpy()` in `port/wlan_ml_compat.h`, identical semantics incl. zero padding |
| `strlcpy()` | removed | `wlan_strlcpy()` in the same header |
| `del_timer_sync()`, `del_timer()` | removed; renamed `timer_delete_sync()`, `timer_delete()` (`timer.h:166,167`) | macros in the compat header |
| `from_timer()` | removed; `timer_container_of()` (`timer.h:132`) | macro in the compat header |
| `sched_clock()` | moved to `<linux/sched/clock.h>` | included by the compat header |
| `<stdarg.h>` | unavailable under the kernel's `-nostdinc` | `<linux/stdarg.h>` (gl_kal.c) |
| `MODULE_SUPPORTED_DEVICE()` | removed from `module.h` | call dropped (gl_init.c) |
| `netif_napi_add(dev,napi,poll,weight)` | 4-arg form gone; 3-arg kept the name | call sites use `netif_napi_add_weight()` - a macro cannot tell the forms apart |
| `napi_gro_flush()` | now a static inline in `<net/gro.h>` | header included |
| `napi->rx_count` / `rx_list` | moved into `struct gro_node`; `napi->gro` | `napi->gro.rx_count` / `&napi->gro.rx_list` |
| `netif_rx_ni()` | removed; `netif_rx()` is context-safe | single `netif_rx()` call |
| `netif_receive_skb_list_internal()` | not exported to modules | use the exported `netif_receive_skb_list()` |
| `cfg80211_ch_switch_notify(dev, chandef)` | gained `link_id` | third argument `0` (single-link device) |
| `cfg80211_roam_info.bss` | moved to `links[0].bss` (`net/wireless/sme.c:1124`) | `rRoamInfo.links[0].bss` |
| `station_parameters.{supported_rates,supported_rates_len,ht_capa,vht_capa}` | moved into the `link_sta_params` sub-struct | 28 sites prefixed |
| `net_device.tx_dropped` / `rx_dropped` | gone; per-cpu core counters only via `dev_get_stats()` | replaced with `rtnl_link_stats64` |
| `struct rps_map`, `rps_needed`, `netdev_rx_queue::rps_map` | private to `net/core/net-sysfs.c` | RPS map setting disabled; userspace can still set `rps_cpus` |
| `netdev_rx_queue` | moved to `<net/netdev_rx_queue.h>` | header included, `__netif_get_rx_queue()` for lookup |
| `FB_EVENT_BLANK` | framebuffer notifier blank events removed | callback made inert |
| `MODULE_IMPORT_NS(VFS_internal_...)` | namespace removed | dropped |
| `mtk_wcn_cmb_stub.h` | in the vendor tree under `mt-plat/` | real file installed |

Three things were **not** API drift but genuine vendor inconsistency, fixed
with a comment at each site:

* `CFG_MTK_ANDROID_EMI` gated the *declarations* of
  `kalSet*EmiMpuProtection` and of `gConEmiPhyBaseFinal` / `gConEmiSizeFinal`,
  while the code that uses them is unconditional.  With EMI off (the mainline
  choice - LK already programs the EMI MPU) all four `gConEmi*` globals had no
  definition at all, so the declarations were un-gated and the four globals are
  now defined unconditionally and stay 0, which is exactly what the
  "invalid Consys EMI memory address" guards test for.
* `kalUpdateBssTimestamp()` walked cfg80211's private `bss_list`, which is not
  visible to drivers on mainline; it is now a documented no-op returning
  success.
* `wlan/adaptor/wmt_cdev_wifi.c` carries its own `module_init()/module_exit()`,
  which collides with the WLAN driver's when both are linked into one module.
  The file already has the switch for that (`MTK_WCN_BUILT_IN_DRIVER`), and it
  is its only use in the file.  `CREATE_NODE_DYNAMIC` is deliberately not set:
  it would compile the never-called `WIFI_init()` char-device path, which uses
  the 4.x `class_create(name, owner)`.

## Kernel configuration bugs found along the way

`CONFIG_CFG80211=y` could not be set: any `conf` run silently rewrote it back
to `m`.  Cause:

    net/wireless/Kconfig:19   depends on RFKILL || !RFKILL

With `RFKILL=m`, kconfig's tristate arithmetic evaluates `m || n` as **m**, and
a dependency that evaluates to `m` caps the dependent at `m`.  Setting
`CONFIG_RFKILL=y` first makes `CFG80211=y` stick.  Measured both ways.

This mattered beyond build hygiene: `CFG80211` was `=m`, the handset has no
module infrastructure (nothing runs `modprobe`, the initramfs ships no `.ko`),
and `grep cfg80211 out/System.map` returned **0** - so cfg80211 was simply
absent from the running kernel and no cfg80211-based driver could ever work.
With it built in, `System.map` now has 580 cfg80211 matches.

`Module.symvers` also did not exist, because only `make Image` had ever been
run.  `make Image` does produce `out/vmlinux.symvers` (all built-in exports),
and this kernel has no in-tree modules built yet, so `Module.symvers` is
exactly that file.  Copying it lets modpost resolve symbols for real instead of
needing `KBUILD_MODPOST_WARN`, which would have disabled the check.

## Reproducing from a pristine vendor drop

    ./fix_wl_api.py            # wave 1: headers, cfg80211, timers, strncpy
    ./fix_wl_kal.py            # wave 2: gl_kal.c internals, EMI globals, stubs
    ./cfg_wlan.sh              # RFKILL=y, CFG80211=y, Image, symvers, module
    ./wl1.sh /root/logs/x.log  # rebuild just the module, compact report

Both patch scripts are count-asserted per edit: a mismatch aborts instead of
half-patching the tree.  `fix_wl_kal.py` also takes `--only N` to re-run a
single step.

Logs must go to `/root/logs`, not `/tmp`: `/tmp` does not survive between
separate `wsl ... -- bash` invocations on this setup (measured - a log written
by one call was gone in the next).

## What is still missing, and why it is not a compile problem

*(Historical: this was the state before the WMT shim was written.  The 13
symbols are now implemented by `port/wlan_wmt_shim.c` on top of `mt6833-conn`;
see the top of this file for where the port stands today.)*

modpost reported exactly 13 undefined symbols, all from `wmt_exp.h`:

    mtk_wcn_wmt_wlan_reg / _unreg          wlan probe/remove callback registration
    mtk_wcn_wmt_msgcb_reg / _unreg         reset-message callback registration
    mtk_wcn_wmt_func_on / _off             subsystem power on/off
    mtk_wcn_wmt_ic_info_get                chip / adie / fw version
    mtk_wcn_wmt_do_reset_only              whole-chip reset without assert
    mtk_wcn_wmt_assert_keyword             assert trigger + firmware dump
    mtk_wcn_wmt_mpu_lock_aquire / _release EMI MPU locking
    mtk_wcn_set_connsys_power_off_flag     power-off flag
    mtk_wcn_consys_hw_wifi_paldo_ctrl      wifi PA LDO (vcn33) control

Measured, so it does not have to be retried: turning
`CONFIG_MTK_WLAN_GEN4M_WMT` off (`CFG_MTK_ANDROID_WMT=0`, "standalone MAC"
mode) does **not** remove any of them - the dependency is structural, not a
config switch.

In the vendor these are thin wrappers over `wmt_lib_*`
(`common/common_main/core/wmt_exp.c`), i.e. they are the MTK **WMT/CONN core**.
The vendor's `connectivity/common` is 63 `.c` files / ~75.7k lines with deep
mt-plat dependencies (pmic, spm, pmic_api_buck, pppm, mmc), so it is not a
mechanical port.

The important structural fact: `mtk_wcn_wmt_wlan_reg()` only stores the six
callbacks and, if the chip happens to be already powered, calls
`wlan_probe_cb()` directly.  In the normal flow the *WMT core* powers CONN on
and then calls `wlan_probe_cb()`.  This project already has a working CONN
power-on driver (`mt6833-conn`, verified on hardware: chip id 0x100a0000), so a
shim is feasible - but it has to own power sequencing, reset and the PA LDO,
and those are hardware behaviours to verify on the phone, not to guess.

## Design for the WMT shim, traced from the vendor source

The exact flow, read out of `axi.c` and `wmt_exp.c` (not guessed):

    axi platform probe
      -> register_wmt_cb()                       axi.c:387
           fills rWmtCb with
             wlan_probe_cb            = hifAxiProbe
             wlan_remove_cb           = hifAxiRemove
             wlan_bus_cnt_get_cb      = hifAxiGetBusCnt
             wlan_bus_cnt_clr_cb      = hifAxiClrBusCnt
             wlan_emi_mpu_set_protection_cb = hifAxiSetMpuProtect
             wlan_is_wifi_drv_own_cb  = hifAxiIsWifiDrvOwn
           then calls mtk_wcn_wmt_wlan_reg(&rWmtCb)   axi.c:399

With `CFG_SUPPORT_CONNINFRA=0` and `CFG_MTK_ANDROID_WMT=1` (this build),
`hifAxiProbe()` does exactly two things:

    axi.c:279   mtk_wcn_consys_hw_wifi_paldo_ctrl(1);     /* wifi PA LDO on */
    axi.c:294   pfWlanProbe(g_prPlatDev, prDriverData);   /* real MAC bring-up */

and `hifAxiRemove()` mirrors it (`pfWlanRemove()`, then paldo ctrl 0).

So the shim's `mtk_wcn_wmt_wlan_reg()` is the natural power-on trigger - the
vendor itself calls `wlan_probe_cb()` from there when the chip is already up.
The shim should:

1. store a copy of the six callbacks;
2. `mt6833_conn_get(dev)` + `mt6833_conn_power_on()` - both already exist and
   are refcounted, and `power_on()` already verifies the chip id
   (`MT6833_CONN_IP_VER_ID` = 0x100a0000, `mt6833-conn.h`);
3. call `wlan_probe_cb()`;
4. on `mtk_wcn_wmt_wlan_unreg()` do the reverse: `wlan_remove_cb()`,
   `mt6833_conn_power_off()`, `mt6833_conn_put()`.

Two things the shim cannot invent and that need measurement on the phone:

* **ADIE chip id.** `mtk_wcn_wmt_ic_info_get(WMTCHIN_ADIE)` is used by
  `plat/mt6833/plat_priv.c:194` to pick a calibration variant.  The vendor gets
  it from `mtk_wcn_consys_detect_adie_chipid()`, which reads the ADIE over the
  PMIC/SPI path; nothing in mainline does that yet.  Returning 0 has to be
  tried and the resulting calibration path observed in dmesg.
* **PA LDO ownership.** `mtk_wcn_consys_hw_wifi_paldo_ctrl()` is the vendor's
  vcn33_1 (wifi PA) regulator toggle, but `mt6833-conn` already claims vcn33_1
  during `power_on()`.  The shim must not double-enable it; either the shim
  no-ops and mt6833-conn keeps owning the rail, or ownership moves.  Decide by
  measuring, not by reading.

Also needed before a `.ko` can be tested: a way to get the module onto the
handset.  `/proc/modules` is empty and nothing runs `modprobe`, so the file has
to be pushed over the serial console (~700 KB base64 for a ~500 KB `.ko`) or
through a gadget link, then `insmod`ed by hand.

