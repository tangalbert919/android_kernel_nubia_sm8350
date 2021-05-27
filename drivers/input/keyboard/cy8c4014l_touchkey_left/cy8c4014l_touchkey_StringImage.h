#ifndef __CYPRESS8C40141_TOUCHKEY_STRINGIMAGE_H__
#define __CYPRESS8C40141_TOUCHKEY_STRINGIMAGE_H__

#ifdef TARGET_NUBIA_NX629J_V1S
#define CURRENT_NEW_FIRMWARE_VER    0x0f

#define LINE_CNT_0 77

const char *stringImage_left_0[]={
#include "CE220891_CapSense_with_Breathing_LED0f.cyacd"
};

#else
#define CURRENT_NEW_FIRMWARE_VER_4024    0x0f
#define CURRENT_NEW_FIRMWARE_VER_4025    0x01
#define CURRENT_NEW_FIRMWARE_VER_4024_S412    0x21
#define LINE_CNT_0 77
#define LINE_CNT_1 77
#define LINE_CNT_2 77

const char *stringImage_left_0[]={
#include "CE220891_CapSense_with_Breathing_LED0f.cyacd"
};


const char *stringImage_left_1[]={
#include "CE220891_CapSense_with_4025_right_01.cyacd"
};

const char *stringImage_left_2[]={
#include "Nubia_NX669_4024FNI-S412_21.cyacd"

};
#endif

#endif
