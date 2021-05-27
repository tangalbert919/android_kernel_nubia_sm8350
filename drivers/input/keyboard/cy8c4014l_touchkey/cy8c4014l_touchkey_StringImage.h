#ifndef __CYPRESS8C40141_TOUCHKEY_STRINGIMAGE_H__
#define __CYPRESS8C40141_TOUCHKEY_STRINGIMAGE_H__

#ifdef TARGET_NUBIA_NX629J_V1S
#define CURRENT_NEW_FIRMWARE_VER    0x0d

#define LINE_CNT_0 77

const char *stringImage_0[]={
#include "CE220891_CapSense_with_Breathing_LED0d.cyacd"
};

#else
#define CURRENT_NEW_FIRMWARE_VER_4024    0x0d
#define CURRENT_NEW_FIRMWARE_VER_4025    0x04
#define CURRENT_NEW_FIRMWARE_VER_4024_S412    0x20

#define LINE_CNT_0 77
#define LINE_CNT_1 77
#define LINE_CNT_2 77

const char *stringImage_0[]={
#include "CE220891_CapSense_with_Breathing_LED0d.cyacd"
};


const char *stringImage_1[]={
#include "CE220891_CapSense_with_4025_left_04.cyacd"

};


const char *stringImage_2[]={
#include "Nubia_NX669_4024FNI-S412_20.cyacd"

};

#endif

#endif
