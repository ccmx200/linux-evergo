/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MediaTek BTIF public API.
 *
 * Trimmed from the vendor drivers/misc/mediatek/btif/common/inc/mtk_btif_exp.h:
 * only the types and the twelve entry points that the connectivity STP layer
 * actually calls are kept.  The prototypes are byte-for-byte compatible with
 * the vendor header, so common/common_main/linux/stp_btif.c compiles unchanged.
 */
#ifndef _MTK_BTIF_EXP_H_
#define _MTK_BTIF_EXP_H_

#include <linux/types.h>

struct task_struct;

#define BTIF_MAX_LEN_PER_PKT	2048

enum _ENUM_BTIF_DPIDLE_ {
	BTIF_DPIDLE_DISABLE = 0,
	BTIF_DPIDLE_ENABLE = BTIF_DPIDLE_DISABLE + 1,
	BTIF_DPIDLE_MAX,
};

enum _ENUM_BTIF_LPBK_MODE_ {
	BTIF_LPBK_DISABLE = 0,
	BTIF_LPBK_ENABLE = BTIF_LPBK_DISABLE + 1,
	BTIF_LPBK_MAX,
};

enum _ENUM_BTIF_DBG_ID_ {
	BTIF_DISABLE_LOGGER = 0,
	BTIF_ENABLE_LOGGER = BTIF_DISABLE_LOGGER + 1,
	BTIF_DUMP_LOG = BTIF_ENABLE_LOGGER + 1,
	BTIF_CLR_LOG = BTIF_DUMP_LOG + 1,
	BTIF_DUMP_BTIF_REG = BTIF_CLR_LOG + 1,
	BTIF_MAX_DBG_ID,
};

/* error codes, kept identical to the vendor enum */
enum _ENUM_BTIF_OP_ERROR_CODE_ {
	E_BTIF_AGAIN = 0,
	E_BTIF_FAIL = -1,
	E_BTIF_BAD_POINTER = -2,
	E_BTIF_NO_SPACE = -3,
	E_BTIF_INTR = -4,
	E_BTIF_INVAL_PARAM = -5,
	E_BTIF_ALREADY_OPEN = -6,
	E_BTIF_NOT_OPEN = -7,
	E_BTIF_INVAL_STATE = -8,
};

typedef int (*MTK_WCN_BTIF_RX_CB)(unsigned char *p_buf, unsigned int len);

int mtk_wcn_btif_open(char *p_owner, unsigned long *p_id);
int mtk_wcn_btif_close(unsigned long u_id);
int mtk_wcn_btif_write(unsigned long u_id, unsigned char *p_buf,
		       unsigned int len);
int mtk_wcn_btif_rx_cb_register(unsigned long u_id, MTK_WCN_BTIF_RX_CB rx_cb);
int mtk_wcn_btif_wakeup_consys(unsigned long u_id);
int mtk_wcn_btif_dpidle_ctrl(unsigned long u_id,
			     enum _ENUM_BTIF_DPIDLE_ en);
int mtk_wcn_btif_loopback_ctrl(unsigned long u_id,
			       enum _ENUM_BTIF_LPBK_MODE_ mode);
int mtk_wcn_btif_dbg_ctrl(unsigned long u_id, enum _ENUM_BTIF_DBG_ID_ flag);
bool mtk_wcn_btif_parser_wmt_evt(unsigned long u_id, const char *sub_str,
				 unsigned int str_len);

int mtk_btif_exp_rx_has_pending_data(unsigned long u_id);
int mtk_btif_exp_tx_has_pending_data(unsigned long u_id);
struct task_struct *mtk_btif_exp_rx_thread_get(unsigned long u_id);

#endif /* _MTK_BTIF_EXP_H_ */
