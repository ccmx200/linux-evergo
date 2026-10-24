# SPDX-License-Identifier: GPL-2.0
#
# MediaTek Gen4M WiFi driver (SOC2_1X1 / AXI HIF) for MT6833.
#
# Installed by apply.sh as:
#   drivers/net/wireless/mediatek/gen4m/
#       Kconfig  Makefile  objects.mk  port/
#       wlan/core/gen4m/...        <- vendor wlan/core/gen4m, unmodified
#       wlan/adaptor/...           <- vendor wlan/adaptor, unmodified
#
# The module name reproduces the vendor's own $(MODULE_NAME) computation
# (wlan_$(WLAN_CHIP_ID)_$(CONFIG_MTK_COMBO_WIFI_HIF) -> wlan_6833_axi).

gen4m-dir	:= $(src)/wlan/core/gen4m
adaptor-dir	:= $(src)/wlan/adaptor

# ---------------------------------------------------------------------------
# Include paths: this is the *shortened* form of the vendor -I set.  Every
# vendor -I that pointed into drivers/misc/mediatek/... is gone because, with
# the feature flags below, none of those headers is reached any more (see
# README.md, section "Why no out-of-tree headers are needed").
# ---------------------------------------------------------------------------
ccflags-y += \
	-I$(gen4m-dir)/include \
	-I$(gen4m-dir)/include/nic \
	-I$(gen4m-dir)/include/mgmt \
	-I$(gen4m-dir)/include/chips \
	-I$(gen4m-dir)/wlan_service/include \
	-I$(gen4m-dir)/wlan_service/service/include \
	-I$(gen4m-dir)/wlan_service/glue/osal/include \
	-I$(gen4m-dir)/wlan_service/glue/hal/include \
	-I$(gen4m-dir)/os \
	-I$(gen4m-dir)/os/linux/include \
	-I$(gen4m-dir)/os/linux/hif/common/include \
	-I$(gen4m-dir)/os/linux/hif/axi/include \
	-I$(src)/port

# Linux 7.3 API shims.  Force-included so they also reach the
# core/ translation units, none of which include gl_os.h.
# See port/wlan_ml_compat.h for what is shimmed and why.
ccflags-y += -include $(src)/port/wlan_ml_compat.h

# The vendor tree is 4.14-era and trips a lot of modern warnings; keep the
# same -Wno-* set the vendor Makefile uses, but do not add -Werror.
ccflags-y += -Wno-unused-value -Wno-unused-result -Wno-format -Wno-parentheses
ccflags-y += -Wno-error=incompatible-pointer-types -Wno-error=declaration-after-statement

# ---------------------------------------------------------------------------
# Chip / bus selection.  Values verified against the vendor Makefile logic for
# MTK_COMBO_CHIP=SOC2_1X1, WLAN_CHIP_ID=6833, CONFIG_MTK_COMBO_WIFI_HIF=axi.
# ---------------------------------------------------------------------------
# The WLAN driver calls the MTK WMT/CONN core (mtk_wcn_wmt_*) to power up CONN
# and download firmware, so its headers are part of this build.
ccflags-y += \
	-I$(srctree)/drivers/misc/mediatek/connectivity/common/common_main/include \
	-I$(srctree)/drivers/misc/mediatek/connectivity/common/common_main/core/include \
	-I$(srctree)/drivers/misc/mediatek/connectivity/common/common_main/linux/include \
	-I$(srctree)/drivers/misc/mediatek/connectivity/common/common_main/platform/include \
	-I$(srctree)/drivers/misc/mediatek/connectivity/common/common_detect/drv_init/inc

ccflags-y += -DSOC2_1X1 -DCONFIG_MTK_WIFI_VHT80
ccflags-y += -D_HIF_AXI=1
ccflags-y += -DLINUX

# PHY capability: SOC2_1X1 is 802.11ac 1x1.  Nothing CONNAC2X/11ax/6G/TWT.
ccflags-y += -DCFG_SUPPORT_CONNAC2X=0
ccflags-y += -DCFG_SUPPORT_CONNAC2X_2x2=0
ccflags-y += -DCFG_SUPPORT_802_11AX=0
ccflags-y += -DCFG_SUPPORT_802_11BE=0
ccflags-y += -DCFG_SUPPORT_WIFI_6G=0
ccflags-y += -DCFG_SUPPORT_TWT=0
ccflags-y += -DCFG_TWT_SMART_STA=0
ccflags-y += -DCFG_SUPPORT_NAN=0

# CONN core flavour: this board uses the OLD combo/gen4m WMT path
# (CFG_SUPPORT_CONNINFRA=0), not conninfra/MT6895.
ccflags-y += -DCFG_SUPPORT_CONNINFRA=0
ccflags-y += -DCFG_ANDORID_CONNINFRA_SUPPORT=0
ccflags-y += -DCFG_ANDORID_CONNINFRA_COREDUMP_SUPPORT=0
ccflags-y += -DCFG_SUPPORT_PRE_ON_PHY_ACTION=0

ifeq ($(CONFIG_MTK_WLAN_GEN4M_WMT),y)
ccflags-y += -DCFG_MTK_ANDROID_WMT=1 -DMTK_WCN_WMT_STP_EXP_SYMBOL_ABSTRACT
ccflags-y += -DWMT_IDC_SUPPORT=0
else
ccflags-y += -DCFG_MTK_ANDROID_WMT=0
endif

# EMI: off by default.  LK programs the EMI MPU for the wifi reserved region,
# and mainline has no mtk_emimpu_* / <soc/mediatek/emi.h>.  Turning this on
# requires port/emi.h plus an implementation.
ifeq ($(CONFIG_MTK_WLAN_GEN4M_EMI),y)
ccflags-y += -DCFG_MTK_ANDROID_EMI=1 -DCONFIG_WLAN_MTK_EMI=1
else
ccflags-y += -DCFG_MTK_ANDROID_EMI=0
endif

# We are a loadable module, not the vendor built-in driver.
ccflags-y += -DCFG_BUILT_IN_DRIVER=0 -DCONFIG_WLAN_DRV_BUILD_IN=0

# ---------------------------------------------------------------------------
# The rest of the vendor flag set, reproduced verbatim from
# wlan/core/gen4m/Makefile so that the compiled-in feature mix matches the
# one the object list in objects.mk was generated for.
# ---------------------------------------------------------------------------
ccflags-y += \
	-DCFG_WIFI_IP_SET=1 \
	-DWLAN_INCLUDE_PROC \
	-DCFG_SUPPORT_DEBUG_FS=0 \
	-DCFG_SUPPORT_AGPS_ASSIST=0 \
	-DCFG_SUPPORT_TSF_USING_BOOTTIME=1 \
	-DARP_MONITER_ENABLE=1 \
	-DDBG=0 \
	-DCFG_ENABLE_HOST_BUS_TIMEOUT=0 \
	-DCFG_SUPPORT_SET_IPV6_NETWORK=0 \
	-DCFG_DOWNLOAD_DYN_MEMORY_MAP=0 \
	-DCFG_SUPPORT_POWER_THROTTLING=0 \
	-DCFG_SUPPORT_CHNL_CONFLICT_REVISE=0 \
	-DCFG_SUPPORT_AEE=0 \
	-DBUILD_QA_DBG=1 \
	-DCFG_WPS_DISCONNECT=1 \
	-DCFG_SUPPORT_PASSPOINT=1 \
	-DCFG_HS20_DEBUG=1 \
	-DCFG_MET_PACKET_TRACE_SUPPORT=1 \
	-DCFG_MET_TAG_SUPPORT=0 \
	-DCFG_TC3_FEATURE=0 \
	-DCFG_TC10_FEATURE=0 \
	-DCONFIG_MTK_MD_SUPPORT=0 \
	-DCFG_TC1_FEATURE=0 \
	-DCFG_MTK_MDDP_SUPPORT=0 \
	-DCONFIG_WLAN_SERVICE=1 \
	-DCONFIG_TEST_ENGINE_OFFLOAD=1 \
	-DCFG_DRIVER_INITIAL_RUNNING_MODE=3 \
	-DCFG_SUPPORT_DUAL_STA=1 \
	-DCFG_SUPPORT_PKT_OFLD=0

# DRIVER_BUILD_DATE is stringified into the driver version attribute
# (gl_sys.c, gl_vendor.c); it must be defined.  Fixed value keeps module
# builds reproducible.
ccflags-y += -DDRIVER_BUILD_DATE=\"mainline\"

# ---------------------------------------------------------------------------
# Objects
# ---------------------------------------------------------------------------
ifneq ($(CONFIG_MTK_WLAN_GEN4M_CHIP_SOC2_1X1),y)
$(error MTK_WLAN_GEN4M only ships the SOC2_1X1 object list; enable CONFIG_MTK_WLAN_GEN4M_CHIP_SOC2_1X1)
endif

include $(src)/objects.mk

obj-$(CONFIG_MTK_WLAN_GEN4M) += wlan_6833_axi.o

wlan_6833_axi-y := $(addprefix wlan/core/gen4m/,$(addsuffix .o,$(MTK_WLAN_GEN4M_CORE_OBJS)))

# mainline platform stubs: hooks the driver expects from MediaTek's
# mt-plat / connectivity_build_in_adapter layer.  See the file header.
wlan_6833_axi-y += port/wlan_plat_stubs.o

# the WMT/CONN core slice the driver needs (see port/wlan_wmt_shim.c)
wlan_6833_axi-y += port/wlan_wmt_shim.o

# mainline port: the vendor's wlan/adaptor (the /dev/wmtWifi character
# device) is compiled into this module rather than built as the separate
# connadp.ko.  It is the implementation of the platform hooks the WLAN
# core calls (update_driver_loaded_status, wifi_reset_start/end,
# register_file_buf_handler, register_set_p2p_mode_handler,
# get_wifi_standalone_log_mode), so folding it in keeps the module
# self-contained.  The per-object flags are the same ones the adaptor
# branch below uses.
ccflags-y += -DMTK_WCN_BUILT_IN_DRIVER
wlan_6833_axi-y += wlan/adaptor/wmt_cdev_wifi.o

# ---------------------------------------------------------------------------
# Optional /dev/wmtWifi character device (vendor "connadp" module).
# Only wmt_cdev_wifi.o is built: CONNAC_VER != 2_0 and
# CONFIG_MTK_CONNSYS_DEDICATED_LOG_PATH is not set, so fw_log_wifi.o,
# fw_log_ics.o, wlan_ring.o and wifi_pwr_on.o stay out.
# ---------------------------------------------------------------------------
ifneq ($(CONFIG_MTK_WLAN_GEN4M_ADAPTOR),)
ccflags-y-adap += \
	-DMTK_WCN_WMT_STP_EXP_SYMBOL_ABSTRACT \
	-DCREATE_NODE_DYNAMIC=1 \
	-DCFG_ANDORID_CONNINFRA_SUPPORT=0 \
	-DCFG_ANDORID_CONNINFRA_COREDUMP_SUPPORT=0 \
	-DWMT_IDC_SUPPORT=0

CFLAGS_connadp.o := $(ccflags-y-adap)
CFLAGS_wmt_cdev_wifi.o := $(ccflags-y-adap)

obj-$(CONFIG_MTK_WLAN_GEN4M_ADAPTOR) += connadp.o
connadp-y := wlan/adaptor/wmt_cdev_wifi.o
endif
