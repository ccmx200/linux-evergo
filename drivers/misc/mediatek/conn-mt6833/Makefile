# SPDX-License-Identifier: GPL-2.0
# Vendor MT6833 CONN platform layer, staged for the mainline port.
obj-y += mt6833.o
obj-y += mtk_wcn_consys_hw.o
obj-y += mt6833_dbg.o
obj-y += port_stubs.o

ccflags-y += -include $(srctree)/drivers/misc/mediatek/conn-mt6833/include/conn_port_compat.h
ccflags-y += \
	-I$(srctree)/drivers/misc/mediatek/conn-mt6833/include \
	-I$(srctree)/drivers/misc/mediatek/connectivity/common/common_main/include \
	-I$(srctree)/drivers/misc/mediatek/connectivity/common/common_main/core/include \
	-I$(srctree)/drivers/misc/mediatek/connectivity/common/common_main/linux/include \
	-I$(srctree)/drivers/misc/mediatek/connectivity/common/common_main/platform/include \
	-I$(srctree)/drivers/misc/mediatek/connectivity/common/common_detect/drv_init/inc

ccflags-y += -DMTK_WCN_WMT_STP_EXP_SYMBOL_ABSTRACT
ccflags-y += -Wno-error -Wno-unused-variable -Wno-unused-function
