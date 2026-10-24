/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Port stub for <mtk_pmic_api_buck.h> (mt-plat PMIC buck/LDO API).
 *
 * The vendor CONN sequence uses these to put the CONN rails into a PMIC
 * low-power mode and to toggle the XO clock buffer.  On mainline the rails are
 * regulator consumers owned by mt6833-conn, and MT6833's clock-buffer hook is
 * compiled out in the vendor source too, so every call here is a no-op that
 * keeps the vendor control flow intact.
 */
#ifndef _PORT_MTK_PMIC_API_BUCK_H_
#define _PORT_MTK_PMIC_API_BUCK_H_

/* PMIC register ids referenced by the CONN sequence. */
#define PMIC_RG_LDO_VCN18_LP	0x2000
#define PMIC_RG_LDO_VCN13_LP	0x2001
#define PMIC_RG_LDO_VCN33_1_LP	0x2002
#define PMIC_RG_LDO_VCN33_2_LP	0x2003

/* SRCLKEN request ids used by the LDO low-power helper. */
#define SRCLKEN0	0
#define SRCLKEN1	1
#define SRCLKEN2	2
#define SRCLKEN3	3
#define SRCLKEN4	4
#define SRCLKEN5	5
#define SRCLKEN6	6
#define SRCLKEN7	7

#define HW_OFF		0
#define HW_LP		1
#define HW_ON		2

#define KERNEL_pmic_set_register_value(id, val)		(0)
#define KERNEL_pmic_ldo_vcn33_1_lp(...)			(0)
#define KERNEL_pmic_ldo_vcn33_2_lp(...)			(0)
#define KERNEL_pmic_ldo_vcn18_lp(...)			(0)
#define KERNEL_pmic_ldo_vcn13_lp(...)			(0)
#define KERNEL_clk_buf_ctrl(...)			(0)
#define KERNEL_clk_buf_show_status_info(...)		(0)

#endif /* _PORT_MTK_PMIC_API_BUCK_H_ */
