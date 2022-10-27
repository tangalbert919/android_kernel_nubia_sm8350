// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#include <drm/drm_edid.h>
#include <linux/hdmi.h>

#include "sde_kms.h"
#include "sde_edid_parser.h"
#include "sde/sde_connector.h"

#define DBC_START_OFFSET 4
#define EDID_DTD_LEN 18
#ifdef CONFIG_NUBIA_HDMI_FEATURE
#include <linux/kernel.h>
#include "../nubiadp/nubia_dp_preference.h"
#define H_MIN	1920
#define V_MIN	1080
#define PHILIPS_2 0x10
#define IVR_9 	0x20
#define IVR_4 	0x40
enum{
	FPS_30 = 30,
	FPS_45 = 45,
	FPS_60 = 60,
	FPS_75 = 75,
	FPS_90 = 90,
	FPS_100 = 100,
	FPS_120 = 120,
	FPS_144 = 144,
	FPS_165 = 165,
	FPS_240 = 240,
};
char monitor_white_list[][20]={
	{"AVT GC553"},
	{"AVT GC573"},
	{"27G2G4"},
	{"LS27R75"},
	{"Mi 245HF2"},
};
extern struct _select_sde_edid_info select_sde_edid_info;
extern char edid_mode_best_info[32];
extern u32 nubia_global_cc_orientation;
extern char edid_device_name[20];
extern struct _user_select_sde_edid_info *edid_info;
bool has_found = false;
#endif

enum data_block_types {
	RESERVED,
	AUDIO_DATA_BLOCK,
	VIDEO_DATA_BLOCK,
	VENDOR_SPECIFIC_DATA_BLOCK,
	SPEAKER_ALLOCATION_DATA_BLOCK,
	VESA_DTC_DATA_BLOCK,
	RESERVED2,
	USE_EXTENDED_TAG
};

static u8 *sde_find_edid_extension(struct edid *edid, int ext_id)
{
	u8 *edid_ext = NULL;
	int i;

	/* No EDID or EDID extensions */
	if (edid == NULL || edid->extensions == 0)
		return NULL;

	/* Find CEA extension */
	for (i = 0; i < edid->extensions; i++) {
		edid_ext = (u8 *)edid + EDID_LENGTH * (i + 1);
		if (edid_ext[0] == ext_id)
			break;
	}

	if (i == edid->extensions)
		return NULL;

	return edid_ext;
}

static u8 *sde_find_cea_extension(struct edid *edid)
{
	return sde_find_edid_extension(edid, SDE_CEA_EXT);
}

static int
sde_cea_db_payload_len(const u8 *db)
{
	return db[0] & 0x1f;
}

static int
sde_cea_db_tag(const u8 *db)
{
	return db[0] >> 5;
}

static int
sde_cea_revision(const u8 *cea)
{
	return cea[1];
}

static int
sde_cea_db_offsets(const u8 *cea, int *start, int *end)
{
	/* Data block offset in CEA extension block */
	*start = 4;
	*end = cea[2];
	if (*end == 0)
		*end = 127;
	if (*end < 4 || *end > 127)
		return -ERANGE;
	return 0;
}

#define sde_for_each_cea_db(cea, i, start, end) \
for ((i) = (start); \
(i) < (end) && (i) + sde_cea_db_payload_len(&(cea)[(i)]) < (end); \
(i) += sde_cea_db_payload_len(&(cea)[(i)]) + 1)

static bool sde_cea_db_is_hdmi_hf_vsdb(const u8 *db)
{
	int hdmi_id;

	if (sde_cea_db_tag(db) != VENDOR_SPECIFIC_DATA_BLOCK)
		return false;

	if (sde_cea_db_payload_len(db) < 7)
		return false;

	hdmi_id = db[1] | (db[2] << 8) | (db[3] << 16);

	return hdmi_id == HDMI_FORUM_IEEE_OUI;
}

static u8 *sde_edid_find_extended_tag_block(struct edid *edid, int blk_id)
{
	u8 *db = NULL;
	u8 *cea = NULL;

	if (!edid) {
		SDE_ERROR("%s: invalid input\n", __func__);
		return NULL;
	}

	cea = sde_find_cea_extension(edid);

	if (cea && sde_cea_revision(cea) >= 3) {
		int i, start, end;

		if (sde_cea_db_offsets(cea, &start, &end))
			return NULL;

		sde_for_each_cea_db(cea, i, start, end) {
			db = &cea[i];
			if ((sde_cea_db_tag(db) == SDE_EXTENDED_TAG) &&
				(db[1] == blk_id))
				return db;
		}
	}
	return NULL;
}

static u8 *
sde_edid_find_block(struct edid *edid, int blk_id)
{
	u8 *db = NULL;
	u8 *cea = NULL;

	if (!edid) {
		SDE_ERROR("%s: invalid input\n", __func__);
		return NULL;
	}

	cea = sde_find_cea_extension(edid);

	if (cea && sde_cea_revision(cea) >= 3) {
		int i, start, end;

		if (sde_cea_db_offsets(cea, &start, &end))
			return NULL;

		sde_for_each_cea_db(cea, i, start, end) {
			db = &cea[i];
			if (sde_cea_db_tag(db) == blk_id)
				return db;
		}
	}
	return NULL;
}


static const u8 *_sde_edid_find_block(const u8 *in_buf, u32 start_offset,
	u8 type, u8 *len)
{
	/* the start of data block collection, start of Video Data Block */
	u32 offset = start_offset;
	u32 dbc_offset = in_buf[2];

	SDE_EDID_DEBUG("%s +", __func__);
	/*
	 * * edid buffer 1, byte 2 being 4 means no non-DTD/Data block
	 *   collection present.
	 * * edid buffer 1, byte 2 being 0 means no non-DTD/DATA block
	 *   collection present and no DTD data present.
	 */

	if ((dbc_offset == 0) || (dbc_offset == 4)) {
		SDE_EDID_DEBUG("EDID: no DTD or non-DTD data present\n");
		return NULL;
	}

	while (offset < dbc_offset) {
		u8 block_len = in_buf[offset] & 0x1F;

		if ((offset + block_len <= dbc_offset) &&
		    (in_buf[offset] >> 5) == type) {
			*len = block_len;
			SDE_EDID_DEBUG("block=%d found @ 0x%x w/ len=%d\n",
				type, offset, block_len);

			return in_buf + offset;
		}
		offset += 1 + block_len;
	}

	return NULL;
}

static void sde_edid_extract_vendor_id(struct sde_edid_ctrl *edid_ctrl)
{
	char *vendor_id;
	u32 id_codes;

	SDE_EDID_DEBUG("%s +", __func__);
	if (!edid_ctrl) {
		SDE_ERROR("%s: invalid input\n", __func__);
		return;
	}

	vendor_id = edid_ctrl->vendor_id;
	id_codes = ((u32)edid_ctrl->edid->mfg_id[0] << 8) +
		edid_ctrl->edid->mfg_id[1];

	vendor_id[0] = 'A' - 1 + ((id_codes >> 10) & 0x1F);
	vendor_id[1] = 'A' - 1 + ((id_codes >> 5) & 0x1F);
	vendor_id[2] = 'A' - 1 + (id_codes & 0x1F);
	vendor_id[3] = 0;
	SDE_EDID_DEBUG("vendor id is %s ", vendor_id);
	SDE_EDID_DEBUG("%s -", __func__);
}

static void sde_edid_set_y420_support(struct drm_connector *connector,
u32 video_format)
{
	u8 cea_mode = 0;
	struct drm_display_mode *mode;
	u32 mode_fmt_flags = 0;

	/* Need to add Y420 support flag to the modes */
	list_for_each_entry(mode, &connector->probed_modes, head) {
		/* Cache the format flags before clearing */
		mode_fmt_flags = mode->flags;
		/* Clear the RGB/YUV format flags before calling upstream API */
		mode->flags &= ~SDE_DRM_MODE_FLAG_FMT_MASK;
		cea_mode = drm_match_cea_mode(mode);
		/* Restore the format flags */
		mode->flags = mode_fmt_flags;
		if ((cea_mode != 0) && (cea_mode == video_format)) {
			SDE_EDID_DEBUG("%s found match for %d ", __func__,
			video_format);
			mode->flags |= DRM_MODE_FLAG_SUPPORTS_YUV;
		}
	}
}

static void sde_edid_parse_Y420CMDB(
struct drm_connector *connector, struct sde_edid_ctrl *edid_ctrl,
const u8 *db)
{
	u32 offset = 0;
	u8 cmdb_len = 0;
	u8 svd_len = 0;
	const u8 *svd = NULL;
	u32 i = 0, j = 0;
	u32 video_format = 0;

	if (!edid_ctrl) {
		SDE_ERROR("%s: edid_ctrl is NULL\n", __func__);
		return;
	}

	if (!db) {
		SDE_ERROR("%s: invalid input\n", __func__);
		return;
	}
	SDE_EDID_DEBUG("%s +\n", __func__);
	cmdb_len = db[0] & 0x1f;

	/* Byte 3 to L+1 contain SVDs */
	offset += 2;

	svd = sde_edid_find_block(edid_ctrl->edid, VIDEO_DATA_BLOCK);

	if (svd) {
		/*moving to the next byte as vic info begins there*/
		svd_len = svd[0] & 0x1f;
		++svd;
	}

	for (i = 0; i < svd_len; i++, j++) {
		video_format = *(svd + i) & 0x7F;
		if (cmdb_len == 1) {
			/* If cmdb_len is 1, it means all SVDs support YUV */
			sde_edid_set_y420_support(connector, video_format);
		} else if (db[offset] & (1 << j)) {
			sde_edid_set_y420_support(connector, video_format);

			if (j & 0x80) {
				j = j/8;
				offset++;
				if (offset >= cmdb_len)
					break;
			}
		}
	}

	SDE_EDID_DEBUG("%s -\n", __func__);

}

static void sde_edid_parse_Y420VDB(
struct drm_connector *connector, struct sde_edid_ctrl *edid_ctrl,
const u8 *db)
{
	u8 len = db[0] & 0x1f;
	u32 i = 0;
	u32 video_format = 0;

	if (!edid_ctrl) {
		SDE_ERROR("%s: invalid input\n", __func__);
		return;
	}

	SDE_EDID_DEBUG("%s +\n", __func__);

	/* Offset to byte 3 */
	db += 2;
	for (i = 0; i < len - 1; i++) {
		video_format = *(db + i) & 0x7F;
		/*
		 * mode was already added in get_modes()
		 * only need to set the Y420 support flag
		 */
		sde_edid_set_y420_support(connector, video_format);
	}
	SDE_EDID_DEBUG("%s -", __func__);
}

#ifdef CONFIG_NUBIA_HDMI_FEATURE
static int dp_check_buffer_overflow(int rc, int *max_size, int *len)
{
	if (rc >= *max_size) {
		printk("buffer overflow\n");
		return -EINVAL;
	}
	*len += rc;
	*max_size = SZ_4K - *len;
	return 0;
}
void sde_edid_get_v_h(char *resulation, unsigned int *h, unsigned int *v)
{
	char *p;
	int i = 0;
	char v_buf[5] = {0};
	char h_buf[5] = {0};
	int lenght = sizeof(resulation);
	p = resulation;
	for(i=0; i<lenght; i++, p++){
		if(*p != 'x'){
			continue;
		}
		else
			break;
	}
	strncpy(h_buf, resulation, p - resulation);
	strncpy(v_buf, p + 1, lenght - i);
	*h = (int)simple_strtol(h_buf, NULL, 10);
	*v = (int)simple_strtol(v_buf, NULL, 10);
	//printk("h = %d, v = %d, i = %d, p - resulation = %d, lenght = %d\n", *h, *v, i, p - resulation, lenght);
	//printk(" %s, %s, resulation \n", h_buf, v_buf, resulation);
}

void abandon_space_tab_str(char *str)
{
	char buff[DRM_ELD_MONITOR_NAME_STRING + 1];
	char *p = str;
	int count = 0;
	int space_flag = 1;
	memset(buff, 0, DRM_ELD_MONITOR_NAME_STRING + 1);
	while(*p != '\0')
	{
		if (*p == ' ' && space_flag == 1)
		{
			buff[count] = *p;
			p++;
			count++;
			space_flag++;
		}
		if ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))
		{
			buff[count] = *p;
			p++;
			count++;
		}else{
			 p++;
		}
	}

	buff[count] = '\0';
	memcpy(str, buff, strlen(str));
	printk("count = %d, buff_name = %s, monitor_name = %s\n", count, buff, str);
}

static inline void sde_edid_store_best_format(
int vrefresh, int hdispay, int vdispay, int ratio)
{
	select_sde_edid_info.fps    = vrefresh;
	select_sde_edid_info.h      = hdispay;
	select_sde_edid_info.v      = vdispay;
	select_sde_edid_info.ratio  = ratio;
	select_sde_edid_info.hdmi_connected = true;
	printk("%s: fps=%d, h=%d, v=%d, ration=%d \n", __func__, vrefresh, hdispay, vdispay, ratio);
}
static void sde_edid_get_best_format(
struct drm_display_mode *mode, struct drm_connector *connector, int count)
{
	int h = 0, v = 0, v0 = 0;
	int fps = 0;
	int ratio = 0;
	int len1 = 0, len2 = 0, len3 = 0;
	bool is_big_1080p = false;
	uint8_t *eld = connector->eld;
	printk("monitor name = %s \n", &eld[DRM_ELD_MONITOR_NAME_STRING]);
	list_for_each_entry(mode, &connector->modes, head) {
		sde_edid_get_v_h(mode->name, &h, &v);
		/*
		* remove the 2048 x 1152 resulation
		*/
		if (h == 2048 && v == 1152){
			continue;
		}
		//nubia filter for Mi
		if (!strncmp(edid_device_name, monitor_white_list[4],
				strlen(monitor_white_list[4]))) {
			if (h == 1920 && v == 1440 && mode->vrefresh == FPS_120) {
				continue;
			}
		}
		/*
		* store frist resulation,
		* if there is no 16:9 && fps > 60 && v > 1080 && h > 1920,
		* we use the frist data edid info
		*/
		if(!v0){
			v0 = v;
			/*if there is no data > 1080p, we must to use the frist edid info*/
			sde_edid_store_best_format(mode->vrefresh, h, v, mode->picture_aspect_ratio);
		}
		/*select 16:9 && fps > 60 && v > 1080 && h > 1920*/
		ratio = h * 9 / 16;
		if(mode->vrefresh >= FPS_60 && (ratio == v) && (v >= V_MIN && h >= H_MIN)){
			if(mode->vrefresh > fps){ //select max fps
				fps = mode->vrefresh;
				sde_edid_store_best_format(mode->vrefresh, h, v, mode->picture_aspect_ratio);
			}else if(mode->vrefresh == fps){ //if fps is equal, select max resulation
				if(v > select_sde_edid_info.v && h > select_sde_edid_info.h){
					fps = mode->vrefresh;
					sde_edid_store_best_format(mode->vrefresh, h, v, mode->picture_aspect_ratio);
				}
			}
			/**
			***	for data card GC573
			*** if have: v==1080 && h==1920 && fps==165, we select it
			*** else if have: v==1080 && h==1920 && fps==240, we select it
			*** else if have: v==1080 && h==1920 && fps==120, we select it
			*** else select another max v & h &fps
			***/
			len1 = strlen(monitor_white_list[1]);
			len2 = strlen(monitor_white_list[0]);
		        len3 = strlen(monitor_white_list[2]);
			if(!strncmp(&eld[DRM_ELD_MONITOR_NAME_STRING], monitor_white_list[1], len1)){
				if (mode->vrefresh == FPS_165 && v == V_MIN && h == H_MIN){
					sde_edid_store_best_format(mode->vrefresh, h, v, mode->picture_aspect_ratio);
                                        break;
				}else if(mode->vrefresh == FPS_240 && v == V_MIN && h == H_MIN){
					sde_edid_store_best_format(mode->vrefresh, h, v, mode->picture_aspect_ratio);
					break;
				}else if(mode->vrefresh == FPS_120 && v == V_MIN && h == H_MIN){
					sde_edid_store_best_format(mode->vrefresh, h, v, mode->picture_aspect_ratio);
					break;
				}
			}else if(!strncmp(&eld[DRM_ELD_MONITOR_NAME_STRING], monitor_white_list[0], len2)){
			/**
			***	for data card GC553
			*** if have: v>1080 && h>1920
					if have: v==1080 && h==1920 && fps==120 we select it
			*** else
					if have: v==1080 && h==1920 && fps==144, we select it
			*** else select another max v & h &fps
			***/
            /*only select one time*/
				if(v > V_MIN && h > H_MIN)
					is_big_1080p = true;

				if(is_big_1080p || (nubia_global_cc_orientation & IVR_9)){
					if(mode->vrefresh == FPS_120 && v == V_MIN && h == H_MIN){
						sde_edid_store_best_format(mode->vrefresh, h, v, mode->picture_aspect_ratio);
						break;
					}
				}else{
					if(mode->vrefresh == FPS_144){
						sde_edid_store_best_format(mode->vrefresh, h, v, mode->picture_aspect_ratio);
						break;
					}
				}
            }else if(!strncmp(&eld[DRM_ELD_MONITOR_NAME_STRING], monitor_white_list[2], len3)){
                    if(mode->vrefresh == FPS_120 && v == V_MIN && h == H_MIN){
                            sde_edid_store_best_format(mode->vrefresh, h, v, mode->picture_aspect_ratio);
                            break;
                    }
			}else if(mode->vrefresh == FPS_144 && v == V_MIN && h == H_MIN){
				fps = mode->vrefresh;
				sde_edid_store_best_format(mode->vrefresh, h, v, mode->picture_aspect_ratio);
				break;
			}
		}
	}

	/*store the best fps and resulation to the frist space in buffer of edid_mode_info*/
	snprintf(edid_mode_best_info, 32,
			"%dx%d %d %d\n",select_sde_edid_info.h, select_sde_edid_info.v,
			select_sde_edid_info.fps, select_sde_edid_info.ratio);

#ifdef CONFIG_NUBIA_DISP_PREFERENCE
//	dsi_panel_notifier(MSM_HDMI_CONNECT_EVENT,MSM_HDMI_CONNECT_ON);
	SDE_EDID_DEBUG("====>>>>hdmi_connect_on = 1 sde_edid_get_best_format<<<<====\n");
#endif
	printk("set monitor edid info : %d x %d %d %d",
			select_sde_edid_info.h, select_sde_edid_info.v,
			select_sde_edid_info.fps, select_sde_edid_info.ratio);
}
#endif
static void sde_edid_set_mode_format(
struct drm_connector *connector, struct sde_edid_ctrl *edid_ctrl)
{
	const u8 *db = NULL;
	struct drm_display_mode *mode;
#ifdef CONFIG_NUBIA_HDMI_FEATURE
	int count = 0;
    u32 len = 0, ret = 0, max_size = SZ_4K;
	int v = 0, h = 0;
	struct _user_select_sde_edid_info *info_temp = edid_info;
	has_found = false;
#endif
	SDE_EDID_DEBUG("%s +\n", __func__);
	/* Set YUV mode support flags for YCbcr420VDB */
	db = sde_edid_find_extended_tag_block(edid_ctrl->edid,
			Y420_VIDEO_DATA_BLOCK);
	if (db)
		sde_edid_parse_Y420VDB(connector, edid_ctrl, db);
	else
		SDE_EDID_DEBUG("YCbCr420 VDB is not present\n");

	/* Set RGB supported on all modes where YUV is not set */
	list_for_each_entry(mode, &connector->probed_modes, head) {
		if (!(mode->flags & DRM_MODE_FLAG_SUPPORTS_YUV))
			mode->flags |= DRM_MODE_FLAG_SUPPORTS_RGB;
	}
#ifdef CONFIG_NUBIA_HDMI_FEATURE
	strcpy(edid_device_name, &connector->eld[DRM_ELD_MONITOR_NAME_STRING]);
	abandon_space_tab_str(edid_device_name);
	printk("edid_device_name = %s\n", edid_device_name);

	/**
	*** to get already use solution and fps before from struct _user_select_sde_edid_info list
	**/
	for(; info_temp; ){
		if(!strcmp(info_temp->device_name, edid_device_name)){
			break;
		}
		info_temp = info_temp->next;
	}

	list_for_each_entry(mode, &connector->modes, head) {
		count++;
		printk("hdmi_edid_info[%d]: %s %d %d %d %d %d 0x%x\n",count,
		mode->name, mode->vrefresh, mode->picture_aspect_ratio,
		mode->htotal, mode->vtotal, mode->clock, mode->flags);
		sde_edid_get_v_h(mode->name, &h, &v);
		//nubia add modify for philips 200 cannot touping 144 in 573
		//nubia add modify for IVR_4 cannot touping 144 in 573
		if (!strncmp(&connector->eld[DRM_ELD_MONITOR_NAME_STRING], monitor_white_list[1], strlen(monitor_white_list[1]))){
			if (mode->vrefresh == FPS_144 && h == H_MIN && v == V_MIN
				&& ((nubia_global_cc_orientation & PHILIPS_2)
				|| (nubia_global_cc_orientation & IVR_4)))
				continue;
		}
		//nubia add modify for IVR 9 cannot touping 144 in 553
		if (!strncmp(&connector->eld[DRM_ELD_MONITOR_NAME_STRING], monitor_white_list[0], strlen(monitor_white_list[0]))){
			if (mode->vrefresh == FPS_144 && h == H_MIN && v == V_MIN && (nubia_global_cc_orientation & IVR_9)){
				continue;
			}
		}
		//nubia add filter the 2048x1152 for some compatibility issue
		if (h == 2048 && v == 1152){
		        continue;
		}
		//nubia add filter 144HZ for voc mointor cannot touping 144
		if(!strncmp(&connector->eld[DRM_ELD_MONITOR_NAME_STRING], monitor_white_list[2], strlen(monitor_white_list[2]))){
			if (mode->vrefresh == FPS_144){
				continue;
			}
		}
		//nubia filter for Mi
		if (!strncmp(edid_device_name, monitor_white_list[4],
				strlen(monitor_white_list[4]))) {
			if (h == 1920 && v == 1440 && mode->vrefresh == FPS_120) {
				continue;
			}
		}
        /*store the edid info to the buffer for userspace use*/
		if(!select_sde_edid_info.edid_mode_store)
		{
			ret = snprintf(select_sde_edid_info.edid_mode_info + len, max_size,
				"%s %d %d\n", mode->name, mode->vrefresh, mode->picture_aspect_ratio);
			if (dp_check_buffer_overflow(ret, &max_size, &len))
				SDE_EDID_DEBUG("%s: buffer overflow, %d ", __func__, __LINE__);
		}

		if(info_temp){
			if((info_temp->fps == mode->vrefresh) && (info_temp->h == h) && (info_temp->v == v)){
				printk("%s: alreay selected last time \n", __func__);
				sde_edid_store_best_format(mode->vrefresh, h, v, mode->picture_aspect_ratio);
				has_found = true;
				/*store the best fps and resulation to the frist space in buffer of edid_mode_info*/
				snprintf(edid_mode_best_info, 32, "%dx%d %d %d\n",info_temp->h, info_temp->v, info_temp->fps, info_temp->ratio);
			}
		}
	}

	if(!select_sde_edid_info.edid_mode_store && !has_found)
	{
		if(count>0)
			sde_edid_get_best_format(mode, connector, count);
	}
#endif

	db = sde_edid_find_extended_tag_block(edid_ctrl->edid,
			Y420_CAPABILITY_MAP_DATA_BLOCK);
	if (db)
		sde_edid_parse_Y420CMDB(connector, edid_ctrl, db);
	else
		SDE_EDID_DEBUG("YCbCr420 CMDB is not present\n");

	SDE_EDID_DEBUG("%s -\n", __func__);
}

static void _sde_edid_update_dc_modes(
struct drm_connector *connector, struct sde_edid_ctrl *edid_ctrl)
{
	int i, start, end;
	u8 *edid_ext, *hdmi;
	struct drm_display_info *disp_info;
	u32 hdmi_dc_yuv_modes = 0;

	SDE_EDID_DEBUG("%s +\n", __func__);

	if (!connector || !edid_ctrl) {
		SDE_ERROR("invalid input\n");
		return;
	}

	disp_info = &connector->display_info;

	edid_ext = sde_find_cea_extension(edid_ctrl->edid);

	if (!edid_ext) {
		SDE_DEBUG("no cea extension\n");
		return;
	}

	if (sde_cea_db_offsets(edid_ext, &start, &end))
		return;

	sde_for_each_cea_db(edid_ext, i, start, end) {
		if (sde_cea_db_is_hdmi_hf_vsdb(&edid_ext[i])) {

			hdmi = &edid_ext[i];

			if (sde_cea_db_payload_len(hdmi) < 7)
				continue;

			if (hdmi[7] & DRM_EDID_YCBCR420_DC_30) {
				hdmi_dc_yuv_modes |= DRM_EDID_YCBCR420_DC_30;
				SDE_EDID_DEBUG("Y420 30-bit supported\n");
			}

			if (hdmi[7] & DRM_EDID_YCBCR420_DC_36) {
				hdmi_dc_yuv_modes |= DRM_EDID_YCBCR420_DC_36;
				SDE_EDID_DEBUG("Y420 36-bit supported\n");
			}

			if (hdmi[7] & DRM_EDID_YCBCR420_DC_48) {
				hdmi_dc_yuv_modes |= DRM_EDID_YCBCR420_DC_36;
				SDE_EDID_DEBUG("Y420 48-bit supported\n");
			}
		}
	}

	disp_info->edid_hdmi_dc_modes |= hdmi_dc_yuv_modes;

	SDE_EDID_DEBUG("%s -\n", __func__);
}

static void _sde_edid_extract_audio_data_blocks(
	struct sde_edid_ctrl *edid_ctrl)
{
	u8 len = 0;
	u8 adb_max = 0;
	const u8 *adb = NULL;
	u32 offset = DBC_START_OFFSET;
	u8 *cea = NULL;

	if (!edid_ctrl) {
		SDE_ERROR("invalid edid_ctrl\n");
		return;
	}
	SDE_EDID_DEBUG("%s +", __func__);
	cea = sde_find_cea_extension(edid_ctrl->edid);
	if (!cea) {
		SDE_DEBUG("CEA extension not found\n");
		return;
	}

	edid_ctrl->adb_size = 0;

	memset(edid_ctrl->audio_data_block, 0,
		sizeof(edid_ctrl->audio_data_block));

	do {
		len = 0;
		adb = _sde_edid_find_block(cea, offset, AUDIO_DATA_BLOCK,
			&len);

		if ((adb == NULL) || (len > MAX_AUDIO_DATA_BLOCK_SIZE ||
			adb_max >= MAX_NUMBER_ADB)) {
			if (!edid_ctrl->adb_size) {
				SDE_DEBUG("No/Invalid Audio Data Block\n");
				return;
			}

			continue;
		}

		memcpy(edid_ctrl->audio_data_block + edid_ctrl->adb_size,
			adb + 1, len);
		offset = (adb - cea) + 1 + len;

		edid_ctrl->adb_size += len;
		adb_max++;
	} while (adb);
	SDE_EDID_DEBUG("%s -", __func__);
}

static void sde_edid_parse_hdr_plus_info(struct drm_connector *connector,
	const u8 *db)
{
	struct sde_connector *c_conn;

	c_conn = to_sde_connector(connector);
	c_conn->hdr_plus_app_ver = db[5] & VSVDB_HDR10_PLUS_APP_VER_MASK;
}

static void sde_edid_parse_vsvdb_info(struct drm_connector *connector,
	const u8 *db)
{
	u8 db_len = 0;
	u32 ieee_code = 0;

	SDE_EDID_DEBUG("%s +\n", __func__);

	db_len = sde_cea_db_payload_len(db);

	if (db_len < 5)
		return;

	/* Bytes 2-4: IEEE 24-bit code, LSB first */
	ieee_code = db[2] | (db[3] << 8) | (db[4] << 16);

	if (ieee_code == VSVDB_HDR10_PLUS_IEEE_CODE)
		sde_edid_parse_hdr_plus_info(connector, db);

	SDE_EDID_DEBUG("%s -\n", __func__);
}

static bool sde_edid_is_luminance_value_present(u32 block_length,
	enum luminance_value value)
{
	return block_length > NO_LUMINANCE_DATA && value <= block_length;
}

/*
 * sde_edid_parse_hdr_db - Parse the HDR extended block
 * @connector: connector for the external sink
 * @db: start of the HDR extended block
 *
 * Parses the HDR extended block to extract sink info for @connector.
 */
static void
sde_edid_parse_hdr_db(struct drm_connector *connector, const u8 *db)
{

	u8 len = 0;
	struct sde_connector *c_conn;

	c_conn = to_sde_connector(connector);

	if (!db)
		return;

	len = db[0] & 0x1f;
	/* Byte 3: Electro-Optical Transfer Functions */
	c_conn->hdr_eotf = db[2] & 0x3F;

	/* Byte 4: Static Metadata Descriptor Type 1 */
	c_conn->hdr_metadata_type_one = (db[3] & BIT(0));

	/* Byte 5: Desired Content Maximum Luminance */
	if (sde_edid_is_luminance_value_present(len, MAXIMUM_LUMINANCE))
		c_conn->hdr_max_luminance = db[MAXIMUM_LUMINANCE];

	/* Byte 6: Desired Content Max Frame-average Luminance */
	if (sde_edid_is_luminance_value_present(len, FRAME_AVERAGE_LUMINANCE))
		c_conn->hdr_avg_luminance = db[FRAME_AVERAGE_LUMINANCE];

	/* Byte 7: Desired Content Min Luminance */
	if (sde_edid_is_luminance_value_present(len, MINIMUM_LUMINANCE))
		c_conn->hdr_min_luminance = db[MINIMUM_LUMINANCE];

	c_conn->hdr_supported = true;
	SDE_EDID_DEBUG("HDR electro-optical %d\n", c_conn->hdr_eotf);
	SDE_EDID_DEBUG("metadata desc 1 %d\n", c_conn->hdr_metadata_type_one);
	SDE_EDID_DEBUG("max luminance %d\n", c_conn->hdr_max_luminance);
	SDE_EDID_DEBUG("avg luminance %d\n", c_conn->hdr_avg_luminance);
	SDE_EDID_DEBUG("min luminance %d\n", c_conn->hdr_min_luminance);
}


/*
 * drm_extract_clrmetry_db - Parse the HDMI colorimetry extended block
 * @connector: connector corresponding to the HDMI sink
 * @db: start of the HDMI colorimetry extended block
 *
 * Parses the HDMI colorimetry block to extract sink info for @connector.
 */
static void
sde_parse_clrmetry_db(struct drm_connector *connector, const u8 *db)
{

	struct sde_connector *c_conn;

	c_conn = to_sde_connector(connector);

	if (!db) {
		DRM_ERROR("invalid db\n");
		return;
	}

	/* Byte 3 Bit 0: xvYCC_601 */
	if (db[2] & BIT(0))
		c_conn->color_enc_fmt |= DRM_EDID_CLRMETRY_xvYCC_601;
	/* Byte 3 Bit 1: xvYCC_709 */
	if (db[2] & BIT(1))
		c_conn->color_enc_fmt |= DRM_EDID_CLRMETRY_xvYCC_709;
	/* Byte 3 Bit 2: sYCC_601 */
	if (db[2] & BIT(2))
		c_conn->color_enc_fmt |= DRM_EDID_CLRMETRY_sYCC_601;
	/* Byte 3 Bit 3: ADOBE_YCC_601 */
	if (db[2] & BIT(3))
		c_conn->color_enc_fmt |= DRM_EDID_CLRMETRY_ADOBE_YCC_601;
	/* Byte 3 Bit 4: ADOBE_RGB */
	if (db[2] & BIT(4))
		c_conn->color_enc_fmt |= DRM_EDID_CLRMETRY_ADOBE_RGB;
	/* Byte 3 Bit 5: BT2020_CYCC */
	if (db[2] & BIT(5))
		c_conn->color_enc_fmt |= DRM_EDID_CLRMETRY_BT2020_CYCC;
	/* Byte 3 Bit 6: BT2020_YCC */
	if (db[2] & BIT(6))
		c_conn->color_enc_fmt |= DRM_EDID_CLRMETRY_BT2020_YCC;
	/* Byte 3 Bit 7: BT2020_RGB */
	if (db[2] & BIT(7))
		c_conn->color_enc_fmt |= DRM_EDID_CLRMETRY_BT2020_RGB;
	/* Byte 4 Bit 7: DCI-P3 */
	if (db[3] & BIT(7))
		c_conn->color_enc_fmt |= DRM_EDID_CLRMETRY_DCI_P3;

	DRM_DEBUG_KMS("colorimetry fmts = 0x%x\n", c_conn->color_enc_fmt);
}

/*
 * sde_edid_parse_extended_blk_info - Parse the HDMI extended tag blocks
 * @connector: connector corresponding to external sink
 * @edid: handle to the EDID structure
 * Parses the all extended tag blocks extract sink info for @connector.
 */
static void
sde_edid_parse_extended_blk_info(struct drm_connector *connector,
	struct edid *edid)
{
	const u8 *cea = sde_find_cea_extension(edid);
	const u8 *db = NULL;

	if (cea && sde_cea_revision(cea) >= 3) {
		int i, start, end;

		if (sde_cea_db_offsets(cea, &start, &end))
			return;

		sde_for_each_cea_db(cea, i, start, end) {
			db = &cea[i];

			if (sde_cea_db_tag(db) == USE_EXTENDED_TAG) {
				SDE_EDID_DEBUG("found ext tag block = %d\n",
						db[1]);
				switch (db[1]) {
				case VENDOR_SPECIFIC_VIDEO_DATA_BLOCK:
					sde_edid_parse_vsvdb_info(connector,
							db);
					break;
				case HDR_STATIC_METADATA_DATA_BLOCK:
					sde_edid_parse_hdr_db(connector, db);
					break;
				case COLORIMETRY_EXTENDED_DATA_BLOCK:
					sde_parse_clrmetry_db(connector, db);
				default:
					break;
				}
			}
		}
	}
}

static void _sde_edid_extract_speaker_allocation_data(
	struct sde_edid_ctrl *edid_ctrl)
{
	u8 len;
	const u8 *sadb = NULL;
	u8 *cea = NULL;

	if (!edid_ctrl) {
		SDE_ERROR("invalid edid_ctrl\n");
		return;
	}
	SDE_EDID_DEBUG("%s +", __func__);
	cea = sde_find_cea_extension(edid_ctrl->edid);
	if (!cea) {
		SDE_DEBUG("CEA extension not found\n");
		return;
	}

	sadb = _sde_edid_find_block(cea, DBC_START_OFFSET,
		SPEAKER_ALLOCATION_DATA_BLOCK, &len);
	if ((sadb == NULL) || (len != MAX_SPKR_ALLOC_DATA_BLOCK_SIZE)) {
		SDE_DEBUG("No/Invalid Speaker Allocation Data Block\n");
		return;
	}

	memcpy(edid_ctrl->spkr_alloc_data_block, sadb + 1, len);
	edid_ctrl->sadb_size = len;

	SDE_EDID_DEBUG("speaker alloc data SP byte = %08x %s%s%s%s%s%s%s\n",
		sadb[1],
		(sadb[1] & BIT(0)) ? "FL/FR," : "",
		(sadb[1] & BIT(1)) ? "LFE," : "",
		(sadb[1] & BIT(2)) ? "FC," : "",
		(sadb[1] & BIT(3)) ? "RL/RR," : "",
		(sadb[1] & BIT(4)) ? "RC," : "",
		(sadb[1] & BIT(5)) ? "FLC/FRC," : "",
		(sadb[1] & BIT(6)) ? "RLC/RRC," : "");
	SDE_EDID_DEBUG("%s -", __func__);
}

struct sde_edid_ctrl *sde_edid_init(void)
{
	struct sde_edid_ctrl *edid_ctrl = NULL;

	SDE_EDID_DEBUG("%s +\n", __func__);
	edid_ctrl = kzalloc(sizeof(*edid_ctrl), GFP_KERNEL);
	if (!edid_ctrl) {
		SDE_ERROR("edid_ctrl alloc failed\n");
		return NULL;
	}
	memset((edid_ctrl), 0, sizeof(*edid_ctrl));
	SDE_EDID_DEBUG("%s -\n", __func__);
#ifdef CONFIG_NUBIA_HDMI_FEATURE
	select_sde_edid_info.edid_mode_info = kzalloc(SZ_4K, GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(select_sde_edid_info.edid_mode_info)) {
		select_sde_edid_info.edid_mode_info = NULL;
		SDE_ERROR("select_sde_edid_info.edid_mode_info alloc failed\n");
	}
	select_sde_edid_info.edid_mode_store = false;
#endif
	return edid_ctrl;
}

void sde_free_edid(void **input)
{
	struct sde_edid_ctrl *edid_ctrl = (struct sde_edid_ctrl *)(*input);

	SDE_EDID_DEBUG("%s +", __func__);
	kfree(edid_ctrl->edid);
	edid_ctrl->edid = NULL;
}

void sde_edid_deinit(void **input)
{
	struct sde_edid_ctrl *edid_ctrl = (struct sde_edid_ctrl *)(*input);

	SDE_EDID_DEBUG("%s +", __func__);
	sde_free_edid((void *)&edid_ctrl);
	kfree(edid_ctrl);
	SDE_EDID_DEBUG("%s -", __func__);
}

int _sde_edid_update_modes(struct drm_connector *connector,
	void *input)
{
	int rc = 0;
	struct sde_edid_ctrl *edid_ctrl = (struct sde_edid_ctrl *)(input);

	SDE_EDID_DEBUG("%s +", __func__);
	if (edid_ctrl->edid) {
		drm_connector_update_edid_property(connector,
			edid_ctrl->edid);

		rc = drm_add_edid_modes(connector, edid_ctrl->edid);
		sde_edid_set_mode_format(connector, edid_ctrl);
		_sde_edid_update_dc_modes(connector, edid_ctrl);
		sde_edid_parse_extended_blk_info(connector,
				edid_ctrl->edid);
		SDE_EDID_DEBUG("%s -", __func__);
		return rc;
	}

	drm_connector_update_edid_property(connector, NULL);
	SDE_EDID_DEBUG("%s null edid -", __func__);
	return rc;
}

u8 sde_get_edid_checksum(void *input)
{
	struct sde_edid_ctrl *edid_ctrl = (struct sde_edid_ctrl *)(input);
	struct edid *edid = NULL, *last_block = NULL;
	u8 *raw_edid = NULL;

	if (!edid_ctrl || !edid_ctrl->edid) {
		SDE_ERROR("invalid edid input\n");
		return 0;
	}

	edid = edid_ctrl->edid;

	raw_edid = (u8 *)edid;
	raw_edid += (edid->extensions * EDID_LENGTH);
	last_block = (struct edid *)raw_edid;

	if (last_block)
		return last_block->checksum;

	SDE_ERROR("Invalid block, no checksum\n");
	return 0;
}

bool sde_detect_hdmi_monitor(void *input)
{
	struct sde_edid_ctrl *edid_ctrl = (struct sde_edid_ctrl *)(input);
#ifdef CONFIG_NUBIA_HDMI_FEATURE
    bool ret = false;
	ret = drm_detect_hdmi_monitor(edid_ctrl->edid);
	select_sde_edid_info.hdmi_connected = true;
#ifdef CONFIG_NUBIA_DISP_PREFERENCE
	SDE_EDID_DEBUG("====>>>>hdmi_connect_on = 1 sde_detect_hdmi_monitor<<<<====\n");
#endif
	return ret;
#else
	return drm_detect_hdmi_monitor(edid_ctrl->edid);
#endif
}

void sde_parse_edid(void *input)
{
	struct sde_edid_ctrl *edid_ctrl;

	if (!input) {
		SDE_ERROR("Invalid input\n");
		return;
	}

	edid_ctrl = (struct sde_edid_ctrl *)(input);

	if (edid_ctrl->edid) {
		sde_edid_extract_vendor_id(edid_ctrl);
		_sde_edid_extract_audio_data_blocks(edid_ctrl);
		_sde_edid_extract_speaker_allocation_data(edid_ctrl);
	} else {
		SDE_ERROR("edid not present\n");
	}
}

void sde_get_edid(struct drm_connector *connector,
				  struct i2c_adapter *adapter, void **input)
{
	struct sde_edid_ctrl *edid_ctrl = (struct sde_edid_ctrl *)(*input);

	edid_ctrl->edid = drm_get_edid(connector, adapter);
	SDE_EDID_DEBUG("%s +\n", __func__);

	if (!edid_ctrl->edid)
		SDE_ERROR("EDID read failed\n");

	if (edid_ctrl->edid)
		sde_parse_edid(edid_ctrl);

	SDE_EDID_DEBUG("%s -\n", __func__);
};
