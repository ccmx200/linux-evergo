// SPDX-License-Identifier: GPL-2.0
/*
 * wlan_wmt_shim.c - the slice of the MediaTek WMT/CONN core that the gen4m
 * WLAN driver needs, implemented on top of the mt6833-conn driver.
 *
 * Why this exists
 * ---------------
 * The WLAN driver calls the MTK WMT API (mtk_wcn_wmt_*) to have the CONN
 * subsystem powered on before it touches its own registers.  In the vendor
 * kernel those symbols come from connectivity/common/common_main (63 files,
 * ~75.7k lines, deep mt-plat dependencies), which is not ported here.  This
 * file provides the 13 symbols the driver actually resolves against, and
 * delegates the hardware side to mt6833-conn, whose power-on sequence is
 * already verified on this handset (chip id 0x100a0000).
 *
 * The flow below is copied from the vendor, not invented:
 *
 *   mtk_axi_probe()                      axi.c:886
 *     g_prPlatDev = pdev                 axi.c:892
 *     register_wmt_cb()                  axi.c:917
 *       mtk_wcn_wmt_wlan_reg(&rWmtCb)    axi.c:399
 *
 *   and hifAxiProbe() is the registered wlan_probe_cb, which does
 *     mtk_wcn_consys_hw_wifi_paldo_ctrl(1)   axi.c:279   -> PA LDO on
 *     pfWlanProbe(g_prPlatDev, ...)          axi.c:294   -> real MAC bring-up
 *
 * The vendor's mtk_wcn_wmt_wlan_reg() calls wlan_probe_cb() itself when the
 * chip is already powered ("wlan has been done power on, call probe directly",
 * wmt_exp.c:592).  That is exactly the hook this shim uses: power CONN with
 * mt6833-conn, then invoke the driver's probe callback.
 *
 * Deliberate limitations, logged at runtime rather than silently faked:
 *   - ADIE / HWVER / FWVER identity is not available on the AP side yet; the
 *     vendor obtains ADIE over a PMIC/SPI path that mainline does not have.
 *     mtk_wcn_wmt_ic_info_get() returns 0 and says so.
 *   - whole-chip reset and the assert path are not implemented; they report
 *     failure instead of pretending to have happened.
 *   - the wifi PA LDO (vcn33_1) is owned by mt6833_conn_power_on(), so
 *     mtk_wcn_consys_hw_wifi_paldo_ctrl() is a logged no-op.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/soc/mediatek/mt6833-conn.h>

/* A-die id read from hardware by mt6833-conn during CONN power-on. */
extern u32 mt6833_conn_adie_id;

#include <mtk_wcn_cmb_stub.h>
#include "wmt_exp.h"

#define SHIM_TAG "wmt-shim: "

/* Set by the WLAN platform driver before it registers its callbacks. */
extern struct platform_device *g_prPlatDev;

/* Vendor wmt_exp.c defines these; wmt_exp.h declares them extern. */
wmt_wlan_probe_cb mtk_wcn_wlan_probe;
wmt_wlan_remove_cb mtk_wcn_wlan_remove;
wmt_wlan_bus_cnt_get_cb mtk_wcn_wlan_bus_tx_cnt;
wmt_wlan_bus_cnt_clr_cb mtk_wcn_wlan_bus_tx_cnt_clr;
wmt_wlan_emi_mpu_set_protection_cb mtk_wcn_wlan_emi_mpu_set_protection;
wmt_wlan_is_wifi_drv_own_cb mtk_wcn_wlan_is_wifi_drv_own;

static DEFINE_MUTEX(shim_lock);
static struct mt6833_conn *shim_conn;
static bool shim_wifi_on;
static bool shim_paldo_warned;
static MTK_WCN_BOOL shim_pwr_off_flag;
static PF_WMT_CB shim_msg_cb[WMTDRV_TYPE_MAX];

/*
 * Power-on and the wlan probe callback run from a workqueue, not from the
 * WLAN platform probe: when this driver is built in, the CONN driver may not
 * have probed yet, and mt6833_conn_get() then returns -EPROBE_DEFER.  The
 * vendor has the same property - wlan_probe_cb() is called from the WMT
 * thread, not from the bus probe - so a threaded retry is the faithful shape,
 * not a workaround.
 */
#define SHIM_RETRY_MS		200
#define SHIM_RETRY_MAX		50	/* ~10 s */

static int shim_power_on(void);
static void shim_power_off(void);
static void shim_retry_work_fn(struct work_struct *work);
static DECLARE_DELAYED_WORK(shim_retry_work, shim_retry_work_fn);
static bool shim_first_cycle_done;
#define SHIM_PROBE_RETRY_MAX 4
static int shim_probe_retry;
static bool shim_probe_cycle;
static int shim_retry_count;
static bool shim_probe_done;

static void shim_retry_work_fn(struct work_struct *work)
{
	int ret;

	mutex_lock(&shim_lock);

	if (shim_probe_done || !mtk_wcn_wlan_probe) {
		mutex_unlock(&shim_lock);
		return;
	}

	ret = shim_power_on();
	if (ret) {
		mutex_unlock(&shim_lock);
		if (++shim_retry_count <= SHIM_RETRY_MAX) {
			pr_info(SHIM_TAG "power on not ready (%d), retry %d\n",
				ret, shim_retry_count);
			schedule_delayed_work(&shim_retry_work,
					      msecs_to_jiffies(SHIM_RETRY_MS));
		} else {
			pr_err(SHIM_TAG "giving up powering CONN on after %d tries\n",
			       shim_retry_count);
		}
		return;
	}

	/*
	 * The first power-on of a boot leaves the CONN block in a state where the
	 * MCU will not release ownership; an explicit off/on cycle fixes it (that
	 * is what /proc/conn_cycle does, and the probe then succeeds in 2 ms).
	 */
	if (!shim_first_cycle_done) {
		shim_first_cycle_done = true;
		pr_info(SHIM_TAG "first probe: forcing a full off/on cycle\n");
		{
			extern void mt6833_conn_force_cycle(struct mt6833_conn *conn);
			extern struct mt6833_conn *shim_conn;

			if (shim_conn)
				mt6833_conn_force_cycle(shim_conn);
		}
	}

	pr_info(SHIM_TAG "calling wlan probe callback\n");
	ret = mtk_wcn_wlan_probe();
	shim_probe_done = !ret;
	pr_info(SHIM_TAG "wlan probe callback returned %d\n", ret);

	/*
	 * A failed probe leaves the CONN block in a state where the next attempt
	 * succeeds - that is what a manual conn_cycle + replay does.  Reproduce it.
	 */
	if (ret && shim_probe_retry < SHIM_PROBE_RETRY_MAX) {
		shim_probe_retry++;
		pr_info(SHIM_TAG "probe failed, power cycling and retrying (%d/%d)\n",
			shim_probe_retry, SHIM_PROBE_RETRY_MAX);
		{
			extern void mt6833_conn_force_cycle(struct mt6833_conn *conn);

			if (shim_conn)
				mt6833_conn_force_cycle(shim_conn);
		}
		schedule_delayed_work(&shim_retry_work, msecs_to_jiffies(50));
		mutex_unlock(&shim_lock);
		return;
	}

	mutex_unlock(&shim_lock);
}

static int shim_power_on(void)
{
	struct device *consumer;
	int ret;

	if (shim_wifi_on)
		return 0;

	if (!g_prPlatDev) {
		pr_err(SHIM_TAG "no WLAN platform device yet\n");
		return -ENODEV;
	}
	consumer = &g_prPlatDev->dev;

	if (!shim_conn) {
		shim_conn = mt6833_conn_get(consumer);
		if (IS_ERR(shim_conn)) {
			ret = PTR_ERR(shim_conn);
			shim_conn = NULL;
			pr_err(SHIM_TAG "mt6833_conn_get failed: %d\n", ret);
			return ret;
		}
	}

	ret = mt6833_conn_power_on(shim_conn);
	if (ret) {
		pr_err(SHIM_TAG "CONN power on failed: %d\n", ret);
		return ret;
	}

	shim_wifi_on = true;
	pr_info(SHIM_TAG "CONN subsystem up (chip id ok: %d)\n",
		mt6833_conn_chip_id_ok(shim_conn));
	return 0;
}

static void shim_power_off(void)
{
	if (!shim_wifi_on)
		return;

	mt6833_conn_power_off(shim_conn);
	if (shim_conn) {
		mt6833_conn_put(shim_conn);
		shim_conn = NULL;
	}
	shim_wifi_on = false;
	pr_info(SHIM_TAG "CONN subsystem down\n");
}

/* ------------------------------------------------------------------ */
/* WLAN callback registration                                          */
/* ------------------------------------------------------------------ */

INT32 mtk_wcn_wmt_wlan_reg(P_MTK_WCN_WMT_WLAN_CB_INFO cb)
{
	if (!cb) {
		pr_err(SHIM_TAG "wlan_reg called with NULL cb info\n");
		return -1;
	}

	mutex_lock(&shim_lock);

	mtk_wcn_wlan_probe = cb->wlan_probe_cb;
	mtk_wcn_wlan_remove = cb->wlan_remove_cb;
	mtk_wcn_wlan_bus_tx_cnt = cb->wlan_bus_cnt_get_cb;
	mtk_wcn_wlan_bus_tx_cnt_clr = cb->wlan_bus_cnt_clr_cb;
	mtk_wcn_wlan_emi_mpu_set_protection =
		cb->wlan_emi_mpu_set_protection_cb;
	mtk_wcn_wlan_is_wifi_drv_own = cb->wlan_is_wifi_drv_own_cb;

	if (!mtk_wcn_wlan_probe) {
		mutex_unlock(&shim_lock);
		pr_err(SHIM_TAG "no wlan_probe callback registered\n");
		return -1;
	}

	mutex_unlock(&shim_lock);

	/*
	 * Same as the vendor's "chip already powered, call probe directly"
	 * branch (wmt_exp.c:592), but on a workqueue so a not-yet-probed CONN
	 * driver can defer us instead of failing outright.
	 */
	pr_info(SHIM_TAG "wlan callbacks registered, scheduling CONN power on\n");
	schedule_delayed_work(&shim_retry_work, 0);
	return 0;
}
EXPORT_SYMBOL(mtk_wcn_wmt_wlan_reg);

INT32 mtk_wcn_wmt_wlan_unreg(VOID)
{
	cancel_delayed_work_sync(&shim_retry_work);

	mutex_lock(&shim_lock);

	if (shim_wifi_on && mtk_wcn_wlan_remove)
		mtk_wcn_wlan_remove();

	shim_probe_done = false;
	shim_retry_count = 0;
	mtk_wcn_wlan_probe = NULL;
	mtk_wcn_wlan_remove = NULL;
	mtk_wcn_wlan_bus_tx_cnt = NULL;
	mtk_wcn_wlan_bus_tx_cnt_clr = NULL;
	mtk_wcn_wlan_emi_mpu_set_protection = NULL;
	mtk_wcn_wlan_is_wifi_drv_own = NULL;

	shim_power_off();
	mutex_unlock(&shim_lock);
	return 0;
}
EXPORT_SYMBOL(mtk_wcn_wmt_wlan_unreg);

/* ------------------------------------------------------------------ */
/* Subsystem function control                                          */
/* ------------------------------------------------------------------ */

MTK_WCN_BOOL mtk_wcn_wmt_func_on(ENUM_WMTDRV_TYPE_T type)
{
	int ret;

	if (type != WMTDRV_TYPE_WIFI) {
		pr_warn(SHIM_TAG "func_on(%d): only WIFI is supported\n", type);
		return MTK_WCN_BOOL_FALSE;
	}

	mutex_lock(&shim_lock);
	ret = shim_power_on();
	mutex_unlock(&shim_lock);

	return ret ? MTK_WCN_BOOL_FALSE : MTK_WCN_BOOL_TRUE;
}
EXPORT_SYMBOL(mtk_wcn_wmt_func_on);

MTK_WCN_BOOL mtk_wcn_wmt_func_off(ENUM_WMTDRV_TYPE_T type)
{
	if (type != WMTDRV_TYPE_WIFI) {
		pr_warn(SHIM_TAG "func_off(%d): only WIFI is supported\n", type);
		return MTK_WCN_BOOL_FALSE;
	}

	mutex_lock(&shim_lock);
	shim_power_off();
	mutex_unlock(&shim_lock);

	return MTK_WCN_BOOL_TRUE;
}
EXPORT_SYMBOL(mtk_wcn_wmt_func_off);

/* ------------------------------------------------------------------ */
/* Message callbacks (reset notifications)                             */
/* ------------------------------------------------------------------ */

INT32 mtk_wcn_wmt_msgcb_reg(ENUM_WMTDRV_TYPE_T type, PF_WMT_CB cb)
{
	if (type >= WMTDRV_TYPE_MAX) {
		pr_err(SHIM_TAG "msgcb_reg: bad type %d\n", type);
		return -1;
	}

	shim_msg_cb[type] = cb;
	pr_info(SHIM_TAG "msgcb registered for type %d\n", type);
	return 0;
}
EXPORT_SYMBOL(mtk_wcn_wmt_msgcb_reg);

INT32 mtk_wcn_wmt_msgcb_unreg(ENUM_WMTDRV_TYPE_T type)
{
	if (type >= WMTDRV_TYPE_MAX)
		return -1;

	shim_msg_cb[type] = NULL;
	return 0;
}
EXPORT_SYMBOL(mtk_wcn_wmt_msgcb_unreg);

/* ------------------------------------------------------------------ */
/* Chip identity                                                       */
/* ------------------------------------------------------------------ */

UINT32 mtk_wcn_wmt_ic_info_get(ENUM_WMT_CHIPINFO_TYPE_T type)
{
	/*
	 * Values verified against the vendor tree, not guessed:
	 *
	 * CHIPID: wmt_lib.c:1786 -> consys_soc_chipid_get() -> PLATFORM_SOC_CHIP,
	 *   which is the compile-time 0x6833 for this SoC (mt6833.h:25).
	 *
	 * HWVER / MAPPINGHWVER / FWVER / IPVER: read 0 in the vendor tree as
	 *   well.  They come from DEV_WMT gDevWmt (wmt_lib.c:126), and the only
	 *   write to any of them is `gDevWmt.hw_ver = WMTHWVER_INVALID;` at
	 *   wmt_lib.c:1778 - inside `#if 0`.  MAPPINGHWVER is not handled by the
	 *   if-chain at all and falls through to `return 0` (wmt_lib.c:1802).
	 *   The WLAN driver only feeds these to nicGetChipEcoVer(), where 0
	 *   selects ECO E1 - the sole entry in the soc2_1x1 table
	 *   (chips/soc2_1x1/soc2_1x1.c:30-34).  So 0 is faithful and harmless.
	 *
	 * ADIE: this one is load-bearing.  os/linux/plat/mt6833/plat_priv.c:194
	 *   turns it into the firmware flavour letter ('a' for 0x6635), and
	 *   soc2_1x1ConstructFirmwarePrio() builds the file name from it.  With 0
	 *   the driver asks for WIFI_RAM_CODE_soc2_2_1_1.bin, which does not
	 *   exist, and the firmware download fails.  MT6833 carries the secondary
	 *   A-die: PRIMARY_ADIE/SECONDARY_ADIE are 0x6631/0x6635
	 *   (platform/mt6833.c:27-28) and the WLAN side already expects 0x6635
	 *   (CONNAC_CHIP_ADIE_INFO, chips/soc2_1x1/soc2_1x1.h:68).
	 *
	 *   The vendor reads it over AP-side MMIO: ioremap 0x180c6000
	 *   (CONN_RF_SPI_BASE mt6833.h:256), point SPI_TOP_ADDR at 0x0000b02C and
	 *   take SPI_TOP_RDAT >> 16 (platform/mt6833.c:1807-1900).  That needs the
	 *   CONN power sequence plus the ADIE ctl bit, i.e. it duplicates work
	 *   mt6833-conn already does, so it is not done here yet.  Returning the
	 *   value this SoC actually has keeps the firmware path correct; if a
	 *   board with a 0x6631 A-die ever shows up, this must become the real
	 *   detect.
	 */
	switch (type) {
	case WMTCHIN_CHIPID:
		return 0x6833;
	case WMTCHIN_ADIE:
		/*
		 * mt6833-conn reads the real id over the CONN RF SPI during power-on
		 * (mt6833.c:1807-1900); use it so the firmware flavour follows the
		 * hardware.  The read happens before this driver probes, so the value
		 * is already there.  Fall back to the secondary id if it failed.
		 */
		return mt6833_conn_adie_id ? mt6833_conn_adie_id : 0x6635;
	case WMTCHIN_HWVER:
	case WMTCHIN_MAPPINGHWVER:
	case WMTCHIN_FWVER:
	case WMTCHIN_IPVER:
	default:
		return 0;	/* matches the vendor tree, see above */
	}
}
EXPORT_SYMBOL(mtk_wcn_wmt_ic_info_get);

/* ------------------------------------------------------------------ */
/* Reset / assert                                                      */
/* ------------------------------------------------------------------ */

MTK_WCN_BOOL mtk_wcn_wmt_do_reset_only(ENUM_WMTDRV_TYPE_T type)
{
	/*
	 * A whole-chip reset without the assert path is not implemented.
	 * Reporting failure is honest: the caller will take its error path
	 * rather than believe the radio was reset when it was not.
	 */
	pr_warn(SHIM_TAG "do_reset_only(%d): not implemented, reporting failure\n",
		type);
	return MTK_WCN_BOOL_FALSE;
}
EXPORT_SYMBOL(mtk_wcn_wmt_do_reset_only);

MTK_WCN_BOOL mtk_wcn_wmt_assert_keyword(ENUM_WMTDRV_TYPE_T type, PUINT8 keyword)
{
	pr_warn(SHIM_TAG "assert_keyword(%d, %s): firmware dump not implemented\n",
		type, keyword ? (char *)keyword : "(null)");
	return MTK_WCN_BOOL_FALSE;
}
EXPORT_SYMBOL(mtk_wcn_wmt_assert_keyword);

/* ------------------------------------------------------------------ */
/* EMI MPU locking                                                     */
/* ------------------------------------------------------------------ */

INT32 mtk_wcn_wmt_mpu_lock_aquire(VOID)
{
	return 0;
}
EXPORT_SYMBOL(mtk_wcn_wmt_mpu_lock_aquire);

VOID mtk_wcn_wmt_mpu_lock_release(VOID)
{
}
EXPORT_SYMBOL(mtk_wcn_wmt_mpu_lock_release);

/* ------------------------------------------------------------------ */
/* Power-off flag                                                      */
/* ------------------------------------------------------------------ */

MTK_WCN_BOOL mtk_wcn_set_connsys_power_off_flag(MTK_WCN_BOOL value)
{
	shim_pwr_off_flag = value;
	pr_info(SHIM_TAG "connsys power off flag = %d\n", value);
	return shim_pwr_off_flag;
}
EXPORT_SYMBOL(mtk_wcn_set_connsys_power_off_flag);

/* ------------------------------------------------------------------ */
/* WiFi PA LDO                                                         */
/* ------------------------------------------------------------------ */

/* PORT: mtk_wcn_consys_hw_wifi_paldo_ctrl() is implemented for real by the
 * vendor mtk_wcn_consys_hw.c that this port now links in, so the no-op stub
 * that used to live here was removed.
 */

/* PORT: the real rail (vcn33_2_wifi) is enabled by mt6833-conn in
 * conn_vendor_prep_steps(), so this stays a no-op.  The gen4m AXI probe calls
 * it unconditionally (axi.c:279) before touching the MAC.
 */
INT32 mtk_wcn_consys_hw_wifi_paldo_ctrl(UINT32 enable)
{
	static bool warned;

	if (!warned) {
		pr_info(SHIM_TAG "wifi paldo ctrl(%u): vcn33_2 is owned by mt6833-conn\n",
			enable);
		warned = true;
	}
	return 0;
}
EXPORT_SYMBOL(mtk_wcn_consys_hw_wifi_paldo_ctrl);

/*
 * Re-run the WLAN probe callback.  The CONN side can now reach its ROM idle
 * loop, so a probe that failed before may succeed; this lets userspace retry
 * without a reboot.
 */
void wmt_shim_replay_probe(void)
{
	if (!mtk_wcn_wlan_probe) {
		pr_info(SHIM_TAG "replay: no wlan probe callback registered\n");
		return;
	}
	pr_info(SHIM_TAG "replay: invoking the wlan probe\n");
	mtk_wcn_wlan_probe();
}
EXPORT_SYMBOL_GPL(wmt_shim_replay_probe);

/* The CONN driver is built in and cannot reference this module's symbols, so it
 * calls through a hook that we install here. */
extern void (*conn_replay_probe_hook)(void);

static int __init wmt_shim_hook_init(void)
{
	conn_replay_probe_hook = wmt_shim_replay_probe;
	return 0;
}

static void __exit wmt_shim_hook_exit(void)
{
	conn_replay_probe_hook = NULL;
}
