#ifndef _NUBIA_DISP_PREFERENCE_
#define _NUBIA_DISP_PREFERENCE_

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/timer.h>
#include <linux/err.h>
#include <linux/ctype.h>
#include "sde_kms.h"
#include "dsi_panel.h"
#include "dsi_display.h"


/* ------------------------- General Macro Definition ------------------------*/
#define NUBIA_DISP_COLORTMP_DEBUG        0
#define MAX_REGISTERS 255

enum{
	LHBM_OFF = 0,
	LHBM_ON
};

enum{
	AOD_OFF = 23,
	AOD_ON,
	AOD_BRIGHTNESS_L1,
	AOD_BRIGHTNESS_L2,
	AOD_BRIGHTNESS_L3,
	AOD_BRIGHTNESS_L4,
	AOD_BRIGHTNESS_L5,
	AOD_BRIGHTNESS_L6
};

enum{
	CABC_OFF = 23,
	CABC_LEVEL1,
	CABC_LEVEL2,
	CABC_LEVEL3
};

enum{
	DFPS_60 = 60,
	DFPS_62 = 62,
	DFPS_90 = 90,
	DFPS_120 = 120,
	DFPS_144 = 144
};

enum{
	B0 = 176,
	B1,
	B2,
	B3,
	B4
};


#define NUBIA_DISP_LOG_TAG "ZtemtDisp"
#define NUBIA_DISP_LOG_ON

#ifdef  NUBIA_DISP_LOG_ON
#define NUBIA_DISP_ERROR(fmt, args...) printk(KERN_ERR "[%s] [%s: %d] "  fmt, \
	NUBIA_DISP_LOG_TAG, __FUNCTION__, __LINE__, ##args)
#define NUBIA_DISP_INFO(fmt, args...) printk(KERN_ERR "[%s] [%s: %d] "  fmt, \
	NUBIA_DISP_LOG_TAG, __FUNCTION__, __LINE__, ##args)
#else
#define NUBIA_DISP_ERROR(fmt, args...)
#define NUBIA_DISP_INFO(fmt, args...)
#endif

#ifdef  NUBIA_DISP_DEBUG_ON
#define  NUBIA_DISP_DEBUG(fmt, args...) printk(KERN_DEBUG "[%s] [%s: %d] "  fmt, \
	NUBIA_DISP_LOG_TAG, __FUNCTION__, __LINE__, ##args)
#else
#define NUBIA_DISP_DEBUG(fmt, args...)
#endif


/* ----------------------------- Structure ----------------------------------*/
struct nubia_disp_type{
  int en_cabc;
  unsigned int cabc;

  int hbm_mode;
  int lhbm_mode;
  int en_dfps;
  unsigned int dfps;
  int en_fps_change;
  unsigned int fps_change;
  u8 panel_reg_val[MAX_REGISTERS];
  u8 panel_reg_addr;
  u8 panel_reg_lens;
  char panel_trace_code[MAX_REGISTERS];
  u8 trace_code_val1[MAX_REGISTERS];
  u8 trace_code_val2[MAX_REGISTERS];
  u8 panel_firmware_id;
  unsigned int aod_brightness;
  int bl_limit;
  unsigned int panel_cur_brightness;
  unsigned int hbm_state;
};

/* ------------------------- Function Declaration ---------------------------*/
void nubia_disp_preference(void);
void nubia_set_dsi_ctrl(struct dsi_display *display, const char *dsi_type);
extern struct nubia_disp_type nubia_disp_val;
#endif

