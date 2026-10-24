/* SPDX-License-Identifier: GPL-2.0 */
/*
 * pmic_lbat_service.h - stub for the MediaTek PMIC low-battery service.
 *
 * The vendored gen4m driver uses this to back off TX power when the battery
 * voltage drops (CFG_MODIFY_TX_POWER_BY_BAT_VOLT).  On mainline there is no
 * mtk_lbat_service driver, and no MT6359 low-battery notifier is wired up, so
 * the registration always fails and the feature stays inert.
 *
 * That is safe: the caller (gl_init.c:6332, kalBatNotifierReg) ignores the
 * return value, wlan_bat_volt stays 0, and the backoff paths in
 * gl_init.c:5244 and gl_init.c:5248 (which only act on 3550/3650 mV) never
 * trigger.  TX power therefore stays at the firmware default, which is what
 * the calibration data asks for at nominal battery voltage.
 *
 * The signature is the 4.19+ one, which is the branch this driver compiles
 * on Linux 7.3 (gl_kal.c:10350 selects it by KERNEL_VERSION).
 *
 * RESTORE_VOLT / BACKOFF_VOLT are deliberately NOT defined here: gl_kal.c
 * defines them itself at line 137-138 right after including this header, and
 * a duplicate unconditional define would warn.
 */

#ifndef __MTK_LBAT_SERVICE_H__
#define __MTK_LBAT_SERVICE_H__

#include <linux/err.h>
#include <linux/types.h>

struct lbat_user;

static inline struct lbat_user *lbat_user_register(const char *name,
						   unsigned int hv_thd_volt,
						   unsigned int lv1_thd_volt,
						   unsigned int lv2_thd_volt,
						   void (*callback)(unsigned int))
{
	return ERR_PTR(-EOPNOTSUPP);
}

#endif /* __MTK_LBAT_SERVICE_H__ */
