/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Force-included compatibility shim for the vendor CONN platform code, which
 * was written against MTK's 4.19 tree.
 */
#ifndef _CONN_PORT_COMPAT_H_
#define _CONN_PORT_COMPAT_H_

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/string.h>

/* Gone since 5.6; plain ioremap() is already non-cached on arm64. */
#ifndef ioremap_nocache
#define ioremap_nocache(addr, size)	ioremap((addr), (size))
#endif


#include <linux/thermal.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>

/*
 * Thermal: the vendor registers a zone to expose the CONN temperature.  The
 * OF thermal API changed shape in 6.x and thermal is irrelevant to getting
 * WiFi up, so registration becomes a no-op and the ops type a local struct.
 */
struct port_thermal_zone_ops {
	int (*get_temp)(void *data, int *temp);
};
#define thermal_zone_of_device_ops	port_thermal_zone_ops
#define devm_thermal_zone_of_sensor_register(dev, id, data, ops)	\
	((void)(dev), (void)(id), (void)(data), (void)(ops), NULL)

/*
 * Legacy OF-GPIO API removal: nothing under include/linux provides
 * of_get_named_gpio()/of_get_named_gpiod_flags() any more.  The only consumer
 * in the vendor CONN code is the optional "wifi_ant_swap_gpio" strap, so it is
 * reported absent.  Re-add via devm_gpiod_get_index() if a board needs it.
 */
#define of_get_named_gpio(np, name, index)	(-ENOENT)

#endif /* _CONN_PORT_COMPAT_H_ */
