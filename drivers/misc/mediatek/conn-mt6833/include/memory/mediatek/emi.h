/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Port stub for <memory/mediatek/emi.h>.
 *
 * The vendor CONN platform code calls mtk_emimpu_set_apc()/set_protection() to
 * declare its EMI reservation as unprotected for AP and CONN.  The bootloader
 * has already programmed the EMI MPU for that exact region, and mainline
 * exposes no mtk_emimpu API, so these become no-ops.  The region bookkeeping is
 * kept so the caller's logic still reads naturally.
 */
#ifndef _PORT_MEMORY_MEDIATEK_EMI_H_
#define _PORT_MEMORY_MEDIATEK_EMI_H_

struct emimpu_region_t {
	unsigned long long start;
	unsigned long long end;
	int id;
};

#define REGION_CONN			0
#define DOMAIN_AP			0
#define DOMAIN_CONN			1
#define MTK_EMIMPU_NO_PROTECTION	0

static inline void mtk_emimpu_init_region(struct emimpu_region_t *region, int id)
{
	region->start = 0;
	region->end = 0;
	region->id = id;
}

static inline void mtk_emimpu_set_addr(struct emimpu_region_t *region,
				       unsigned long long start,
				       unsigned long long end)
{
	region->start = start;
	region->end = end;
}

static inline void mtk_emimpu_set_apc(struct emimpu_region_t *region,
				      int domain, int apc)
{
	/* already set by the bootloader */
}

static inline void mtk_emimpu_set_protection(struct emimpu_region_t *region)
{
	/* already set by the bootloader */
}

static inline void mtk_emimpu_free_region(struct emimpu_region_t *region)
{
}

#endif /* _PORT_MEMORY_MEDIATEK_EMI_H_ */
