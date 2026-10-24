// SPDX-License-Identifier: GPL-2.0
/*
 * wlan_plat_stubs.c - platform hooks the vendored gen4m driver expects from
 * MediaTek's mt-plat / connectivity_build_in_adapter layer.
 *
 * The vendor builds these two out of other connectivity sources:
 *
 *   connectivity_export_show_stack(): connectivity_build_in_adapter.c, which
 *       is a thin wrapper around show_stack() and is EXPORT_SYMBOL()'d because
 *       the WLAN driver is a separate module there.
 *   get_low_latency_mode():           wlan/adaptor/wmt_cdev_wifi.c, backed by
 *       a module-global that /dev/wmtWifi sets from userspace.
 *
 * Neither source is part of this build: the adaptor is disabled
 * (CONFIG_MTK_WLAN_GEN4M_ADAPTOR=n) and the connectivity_build_in_adapter
 * layer is the vendor's built-in glue, which mainline does not have.  Both are
 * __weak, so a real implementation wins if one is linked in later.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/export.h>

/*
 * Used through kal_show_stack() in gl_kal.h, which is itself only compiled
 * when CFG_MTK_ANDROID_WMT is set.  It is a debug aid (dump the task stack on
 * an assertion), not a functional path.
 *
 * The vendor body is show_stack(tsk, sp).  On Linux 7.3 show_stack() takes a
 * third log-level argument and is NOT exported to modules, so a module cannot
 * call it.  dump_stack() is exported and dumps the current stack; the @tsk and
 * @sp arguments are therefore ignored.
 */
void __weak connectivity_export_show_stack(struct task_struct *tsk,
					   unsigned long *sp)
{
	dump_stack();
}

/*
 * Vendor body returns the module-global low_latency_mode, which starts at 0
 * and is only set from userspace through /dev/wmtWifi.  With the adaptor not
 * built, nothing can set it, so 0 is the correct value: it is what the driver
 * sees when no low-latency (game mode) request is active.
 */
uint32_t __weak get_low_latency_mode(void)
{
	return 0;
}
