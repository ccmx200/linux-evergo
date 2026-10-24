// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal MT6833 CONN bring-up probe.
 *
 * Purpose: prove the MediaTek INTEGRATED CONN subsystem can be reached from
 * mainline AT ALL, before committing to porting the vendor driver (582k lines
 * of wlan/core + wlan/adaptor).  If the chip id comes back, the bus, the clocks,
 * the power domain and the register map are all correct and what remains is
 * volume of work.  If it does not, no amount of driver porting will help until
 * the power-on sequence is right.
 *
 * The three steps are not invented; they are what the vendor
 * connectivity/common/common_main/platform/mt6833.c does, in order:
 *
 *   1. clk_prepare_enable("ccif")  -- infracfg_ao CLK_IFRAO_CCIF4_AP, the
 *      AP <-> CONN firmware mailbox clock (vendor mt6833.c:)
 *   2. power the CONN domain on -- scpsys MTCMOS.  The vendor reaches it through
 *      its own "conn" power-gate-as-clock; here it is the DT power-domain
 *      MT6833_POWER_DOMAIN_CONN, powered by pm_runtime.
 *   3. write 1 to PERICFG + 0x314 -- CON_PWR_ON in the AP2MD PCCIF4 CCU config
 *      (vendor mt6833.c:)
 *
 * then poll the chip id at CONN_MCU_CONFG + 0x10.  The vendor
 * polling_consys_chipid() (mt6833.c:951) expects 0x100A0000.
 *
 * Register window indices match the reg order in the DTS consys node, which was
 * copied from the vendor device tree.
 *
 * Deliberately uses of_iomap() and not devm_ioremap_resource(): four of the
 * twelve windows are already claimed by other nodes (infracfg_ao, pericfg,
 * scpsys, the watchdog), so the resource-claiming helper fails with -EBUSY.  The
 * vendor driver does raw ioremap for the same reason.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#define CONN_ON_IDX	0	/* 0x18002000 CONN_ON / PMU / top control   */
#define CONN_OFF_IDX	4	/* 0x18007000 CONN_OFF / WFSYS              */
#define MCU_CFG_IDX	5	/* 0x180b1000 CONN_MCU_CONFG                */
#define MCU_CR_IDX	6	/* 0x180a3000 CONN_MCU_CR                   */
#define MCU_EMI_IDX	9	/* 0x18004000 CONN_MCU_EMI                  */
#define PERICFG_IDX	11	/* 0x10003000 PERICFG                       */

#define CONN_CHIP_ID_OFF	0x0010
#define CONN_CHIP_ID_EXPECT	0x100a0000u
#define CON_PWR_ON_OFF		0x0314

/* version / status registers worth printing while we are here */
#define CONN_ON_VERSION_OFF	0x0000
#define CONN_ON_CFG_OFF		0x0004

struct conn_probe {
	struct device *dev;
	void __iomem *conn_on;
	void __iomem *conn_off;
	void __iomem *mcu_cfg;
	void __iomem *mcu_cr;
	void __iomem *mcu_emi;
	void __iomem *pericfg;
	struct clk *ccif;
};

static int conn_probe_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct conn_probe *cp;
	unsigned int val, i;
	int ret = 0;

	cp = devm_kzalloc(dev, sizeof(*cp), GFP_KERNEL);
	if (!cp)
		return -ENOMEM;
	cp->dev = dev;

	dev_info(dev, "mt6833-conn: probing (chip-id test)\n");

	/* The windows overlap other nodes, so map by index without claiming. */
	cp->conn_on  = of_iomap(dev->of_node, CONN_ON_IDX);
	cp->conn_off = of_iomap(dev->of_node, CONN_OFF_IDX);
	cp->mcu_cfg  = of_iomap(dev->of_node, MCU_CFG_IDX);
	cp->mcu_cr   = of_iomap(dev->of_node, MCU_CR_IDX);
	cp->mcu_emi  = of_iomap(dev->of_node, MCU_EMI_IDX);
	cp->pericfg  = of_iomap(dev->of_node, PERICFG_IDX);

	if (!cp->conn_on || !cp->mcu_cfg || !cp->pericfg) {
		dev_err(dev, "mt6833-conn: ioremap failed (on=%p cfg=%p pcfg=%p)\n",
			cp->conn_on, cp->mcu_cfg, cp->pericfg);
		ret = -ENOMEM;
		goto unmap;
	}
	dev_info(dev, "mt6833-conn: windows mapped\n");

	/* step 3 first: CON_PWR_ON.  The vendor writes it after the clocks are
	 * on, but on a cold boot the block is already clocked by the bootloader,
	 * and having the write in place before power-on keeps the firmware from
	 * seeing a half-initialised mailbox.  Ordering is preserved below anyway.
	 */

	/* step 1: ccif clock */
	cp->ccif = devm_clk_get(dev, "ccif");
	if (IS_ERR(cp->ccif)) {
		ret = PTR_ERR(cp->ccif);
		dev_err(dev, "mt6833-conn: no ccif clock: %d\n", ret);
		cp->ccif = NULL;
	} else {
		ret = clk_prepare_enable(cp->ccif);
		dev_info(dev, "mt6833-conn: ccif clk %s (ret=%d, rate=%lu)\n",
			 ret ? "FAILED" : "on", ret, clk_get_rate(cp->ccif));
		if (ret)
			dev_err(dev, "mt6833-conn: ccif enable failed %d\n", ret);
	}

	/* step 2: power the CONN domain on via the DT power-domain */
	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	dev_info(dev, "mt6833-conn: pm_runtime_get_sync -> %d\n", ret);
	if (ret < 0)
		dev_warn(dev, "mt6833-conn: CONN power domain did not come up\n");

	/* let the MTCMOS settle before touching the CONN register file */
	usleep_range(1000, 2000);

	dev_info(dev, "mt6833-conn: conn_on +0x00 = %08x  +0x04 = %08x\n",
		 readl(cp->conn_on + CONN_ON_VERSION_OFF),
		 readl(cp->conn_on + CONN_ON_CFG_OFF));
	if (cp->mcu_cr)
		dev_info(dev, "mt6833-conn: mcu_cr  +0x00 = %08x\n",
			 readl(cp->mcu_cr));
	if (cp->mcu_emi)
		dev_info(dev, "mt6833-conn: mcu_emi +0x00 = %08x\n",
			 readl(cp->mcu_emi));

	/* step 3: CON_PWR_ON */
	writel(1, cp->pericfg + CON_PWR_ON_OFF);
	dev_info(dev, "mt6833-conn: wrote CON_PWR_ON, readback %08x\n",
		 readl(cp->pericfg + CON_PWR_ON_OFF));
	mdelay(5);

	/* chip id, polled like the vendor does */
	val = 0;
	for (i = 0; i < 50; i++) {
		val = readl(cp->mcu_cfg + CONN_CHIP_ID_OFF);
		if (val == CONN_CHIP_ID_EXPECT)
			break;
		usleep_range(2000, 3000);
	}

	if (val == CONN_CHIP_ID_EXPECT) {
		dev_info(dev, "mt6833-conn: CHIP ID OK = %08x (wanted %08x) after %u tries\n",
			 val, CONN_CHIP_ID_EXPECT, i + 1);
		dev_info(dev, "mt6833-conn: CONN IS ALIVE - bus, clocks and power are correct\n");
	} else {
		dev_err(dev, "mt6833-conn: CHIP ID MISMATCH = %08x (wanted %08x) after %u tries\n",
			val, CONN_CHIP_ID_EXPECT, i);
		dev_err(dev, "mt6833-conn: conn_off +0x00 = %08x\n",
			cp->conn_off ? readl(cp->conn_off) : 0);
	}

	/* Leave the block powered: the real driver will want it, and powering it
	 * back down from a probe would only hide the result.
	 */

unmap:
	if (cp->pericfg)
		iounmap(cp->pericfg);
	if (cp->mcu_emi)
		iounmap(cp->mcu_emi);
	if (cp->mcu_cr)
		iounmap(cp->mcu_cr);
	if (cp->mcu_cfg)
		iounmap(cp->mcu_cfg);
	if (cp->conn_off)
		iounmap(cp->conn_off);
	if (cp->conn_on)
		iounmap(cp->conn_on);
	return 0;	/* never fail the probe: we want the log either way */
}

static const struct of_device_id conn_probe_of_match[] = {
	{ .compatible = "mediatek,mt6833-consys" },
	{ }
};
MODULE_DEVICE_TABLE(of, conn_probe_of_match);

static struct platform_driver conn_probe_driver = {
	.probe = conn_probe_probe,
	.driver = {
		.name = "mt6833-conn-probe",
		.of_match_table = conn_probe_of_match,
	},
};
module_platform_driver(conn_probe_driver);

MODULE_DESCRIPTION("MT6833 integrated CONN bring-up probe");
MODULE_LICENSE("GPL");
