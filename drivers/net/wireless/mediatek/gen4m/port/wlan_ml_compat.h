/* SPDX-License-Identifier: GPL-2.0 */
/*
 * wlan_ml_compat.h - Linux 7.3 compatibility shims for the vendored
 * MediaTek gen4m (SOC2_1X1) WLAN driver.
 *
 * The driver is 4.14-era vendor source.  A handful of kernel APIs it uses
 * were removed or renamed between then and Linux 7.3.  Rather than scatter
 * version #ifdefs through the vendor tree, every shim lives here, and this
 * header is force-included into every translation unit of the module (see
 * the -include line in the module Makefile).  That also covers the core/
 * translation units, which do not include gl_os.h.
 *
 * Every item below was checked against the actual 7.3 tree, not assumed:
 *
 *   strncpy()          removed        -> wlan_strncpy() below, identical
 *                                        semantics including zero padding
 *   strlcpy()          removed        -> wlan_strlcpy() below
 *   del_timer_sync()   removed        -> timer_delete_sync()
 *                                        (include/linux/timer.h:166)
 *   del_timer()        removed        -> timer_delete()
 *                                        (include/linux/timer.h:167)
 *   from_timer()       removed        -> timer_container_of()
 *                                        (include/linux/timer.h:132)
 *   sched_clock()      moved header   -> <linux/sched/clock.h>
 *   <stdarg.h>         not available under the kernel's -nostdinc build
 *                                     -> <linux/stdarg.h>
 *   napi_gro_flush()   moved          -> <net/gro.h>, now a static inline
 *                                        taking struct napi_struct *
 *   napi_struct's      moved          -> they live in struct gro_node now,
 *   rx_list/rx_count                     napi->gro, drained by
 *                                        gro_normal_list(&napi->gro)
 *   struct             moved          -> <net/netdev_rx_queue.h>; look queues
 *   netdev_rx_queue                      up with netif_get_rx_queue()
 *
 * netif_napi_add()'s 4-argument form is NOT shimmed here: the 3-argument
 * form still exists under the same name, so a macro cannot distinguish the
 * two.  The two call sites were changed to netif_napi_add_weight() instead.
 */

#ifndef _WLAN_ML_COMPAT_H_
#define _WLAN_ML_COMPAT_H_

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/stdarg.h>
#include <linux/sched/clock.h>
#include <linux/netdevice.h>
#include <net/gro.h>
#include <net/netdev_rx_queue.h>

/*
 * strncpy() was removed from the kernel.  This reproduces its exact
 * contract: copy at most @count bytes, stopping after the terminating NUL,
 * and zero-fill the remainder of the destination.
 */
static inline char *wlan_strncpy(char *dest, const char *src, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++) {
		dest[i] = src[i];
		if (!src[i])
			break;
	}
	for (; i < count; i++)
		dest[i] = '\0';

	return dest;
}

/*
 * strlcpy() was removed from the kernel.  Same contract: copy at most
 * @size - 1 characters, always NUL-terminate, return strlen(src).
 */
static inline size_t wlan_strlcpy(char *dest, const char *src, size_t size)
{
	size_t ret = strlen(src);

	if (size) {
		size_t len = (ret >= size) ? size - 1 : ret;

		memcpy(dest, src, len);
		dest[len] = '\0';
	}

	return ret;
}

#ifndef strncpy
#define strncpy(dest, src, count) wlan_strncpy(dest, src, count)
#endif
#ifndef strlcpy
#define strlcpy(dest, src, size) wlan_strlcpy(dest, src, size)
#endif

/* timer API renames */
#ifndef del_timer_sync
#define del_timer_sync(t) timer_delete_sync(t)
#endif
#ifndef del_timer
#define del_timer(t) timer_delete(t)
#endif

/* container_of from a timer callback */
#ifndef from_timer
#define from_timer(var, callback_timer, timer_fieldname) \
	timer_container_of(var, callback_timer, timer_fieldname)
#endif

#endif /* _WLAN_ML_COMPAT_H_ */
