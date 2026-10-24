// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal definitions for symbols the vendor MT6833 CONN platform layer
 * references from files this port does not build.
 *
 * The three dump_conn_*() symbols live in platform/mt6833_dbg.c, which is only
 * reached from the coredump / debug path; wmtPlatLogLvl is the WMT core's log
 * verbosity.  Neither is involved in power-on or MCU bring-up, so they are
 * stubbed with an explicit message instead of faking a dump.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>

#include "osal_typedef.h"
#include "mt6833.h"

/* WMT_PLAT_LOG_INFO level, see wmt_plat.h. */
INT32 wmtPlatLogLvl = 1;

/* dump_conn_*() are implemented by the real vendor
 * platform/mt6833_dbg.c, which this port builds. */
