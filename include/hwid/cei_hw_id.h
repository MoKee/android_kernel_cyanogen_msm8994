#ifndef __CEI_HW_ID_H
#define __CEI_HW_ID_H

#define CEI_HWID_STRING_LEN 10

enum cei_project_type {
	CEI_PROJECT_PM97 = 0,
	CEI_PROJECT_PM98 = 1,
	CEI_PROJECT_PM99 = 2,
	CEI_PROJECT_INVALID = 3
};

enum cei_ddr_type {
	CEI_DDR_SRC_MAIN   = 0,    /* MAIN SOURCE: 3GB DDR */
	CEI_DDR_SRC_SECOND = 1, 	/* 2ND  SOURCE: 4GB DDR */
	CEI_DDR_SRC_INVALID = 2
};

enum cei_touch_type {
	CEI_TOUCH_SRC_MAIN   = 0,
	CEI_TOUCH_SRC_SECOND = 1,
	CEI_TOUCH_SRC_INVALID = 2
};

enum cei_hw_type {
	CEI_HW_EVT1 = 0,
	CEI_HW_EVT2 = 1,
	CEI_HW_DVT1 = 2,
	CEI_HW_DVT2 = 3,
	CEI_HW_DVT3 = 4,
	CEI_HW_PVT  = 5,
	CEI_HW_INVALID = 6
};

/* Macro to check CEI hw board */
#define is_cei_evt1_board() \
	(get_cei_hw_id() == CEI_HW_EVT1)
#define is_cei_evt2_board() \
	(get_cei_hw_id() == CEI_HW_EVT2)
#define is_cei_dvt1_board() \
	(get_cei_hw_id() == CEI_HW_DVT1)
#define is_cei_dvt2_board() \
	(get_cei_hw_id() == CEI_HW_DVT2)
#define is_cei_dvt3_board() \
	(get_cei_hw_id() == CEI_HW_DVT3)
#define is_cei_pvt_board() \
	(get_cei_hw_id() == CEI_HW_PVT)

/* Macro to check CEI project */
#define is_cei_pm97_project() \
	(get_cei_project_id() == CEI_PROJECT_PM97)
#define is_cei_pm98_project() \
	(get_cei_project_id() == CEI_PROJECT_PM98)
#define is_cei_pm99_project() \
	(get_cei_project_id() == CEI_PROJECT_PM99)

/* Macro to check HWID is valid or not */
#define is_cei_valid_board() \
	((get_cei_hw_id() >= CEI_HW_EVT1) || \
	 (get_cei_hw_id() <= CEI_HW_PVT) \
	? get_cei_hw_id() : CEI_HW_INVALID)

#define is_cei_valid_project() \
	((get_cei_project_id() >= CEI_PROJECT_PM97) || \
	 (get_cei_project_id() <= CEI_PROJECT_PM99) \
	? get_cei_project_id() : CEI_PROJECT_INVALID)

#define is_cei_valid_ddr_src() \
	((get_cei_ddr_id() == CEI_DDR_SRC_MAIN) || \
	 (get_cei_ddr_id() == CEI_DDR_SRC_SECOND) \
	? get_cei_ddr_id() : CEI_DDR_SRC_INVALID)

#define is_cei_valid_touch_src() \
	((get_cei_touch_id() >= CEI_TOUCH_SRC_MAIN) || \
	 (get_cei_touch_id() == CEI_TOUCH_SRC_SECOND) \
	? get_cei_touch_id() : CEI_TOUCH_SRC_INVALID)

/*
 * API to get CEI HWID information:
 *
 * get_cei_hw_id()-      return enum cei_hw_type
 * get_cei_project_id()- return enum cei_project_type
 * get_cei_ddr_id()-     return enum cei_ddr_type
 * get_cei_touch_id()- return enum cei_touch_type
 *
 * Please use **enum variable** in your function to get the return cei_***_type
 */
 
extern enum cei_hw_type get_cei_hw_id(void); 
extern enum cei_project_type get_cei_project_id(void);
extern enum cei_ddr_type get_cei_ddr_id(void);
extern enum cei_touch_type get_cei_touch_id(void);

#endif /* __CEI_HW_ID_H */

