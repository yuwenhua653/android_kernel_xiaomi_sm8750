#ifndef _MCA_COMMON_MCA_HWID_H_
#define _MCA_COMMON_MCA_HWID_H_

#include <linux/types.h>
#include <linux/hwid.h>

/* IDs from the pinned public SM8750 HWID header; never change board identity. */
#define MCA_PROJECT_O1 4
#define MCA_PROJECT_O8 5
#define MCA_PROJECT_O9 8

struct mca_hwid {
	u32 platform_version;
	u32 country_version;
	u32 major_version;
	u32 minor_version;
	u32 build_version;
	u32 product_adc;
	u32 build_adc;
	u32 hwid_value;
	const char *product_name;
};

const struct mca_hwid *mca_get_hwid_info(void);

#endif /* _MCA_COMMON_MCA_HWID_H_ */
