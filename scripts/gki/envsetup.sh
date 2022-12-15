#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2019, The Linux Foundation. All rights reserved.

SCRIPT_DIR=$(readlink -f $(dirname $0)/)
cd ${SCRIPT_DIR}
cd ../../
KERN_SRC=`pwd`

: ${ARCH:=arm64}
: ${CROSS_COMPILE:=aarch64-linux-gnu-}
: ${CLANG_TRIPLE:=aarch64-linux-gnu-}
: ${REAL_CC:=clang}
: ${HOSTCC:=gcc}
: ${HOSTLD:=ld}
: ${HOSTAR:=ar}
: ${KERN_OUT:=}

CONFIGS_DIR=${KERN_SRC}/arch/${ARCH}/configs/vendor

PLATFORM_NAME=$1

BASE_DEFCONFIG=${KERN_SRC}/arch/${ARCH}/configs/${2:-gki_defconfig}

# Fragements that are available for the platform
QCOM_GKI_FRAG=${CONFIGS_DIR}/${PLATFORM_NAME}_GKI.config
QCOM_QGKI_FRAG=${CONFIGS_DIR}/${PLATFORM_NAME}_QGKI.config
QCOM_DEBUG_FRAG=${CONFIGS_DIR}/${PLATFORM_NAME}_debug.config

if [ "${TARGET_NUBIA_SSD}" == "true" ]; then
	NUBIA_DIFF=${CONFIGS_DIR}/lahaina-${NUBIA_TARGET_PRODUCT}-SSD_diff
else
	if [ "${PRODUCT_BUILD_NX669J_S}" == "true" ];then
		NUBIA_DIFF=${CONFIGS_DIR}/lahaina-NX669S-perf_diff
		NUBIA_GKI_DIFF=${CONFIGS_DIR}/lahaina-NX669S-GKI_diff
	else
        if [ "${PRODUCT_BUILD_NX669J_N}" == "true" ];then
                NUBIA_DIFF=${CONFIGS_DIR}/lahaina-NX669N-perf_diff
                NUBIA_GKI_DIFF=${CONFIGS_DIR}/lahaina-NX669N-GKI_diff
        else
		NUBIA_DIFF=${CONFIGS_DIR}/lahaina-${NUBIA_TARGET_PRODUCT}-perf_diff
		NUBIA_GKI_DIFF=${CONFIGS_DIR}/lahaina-${NUBIA_TARGET_PRODUCT}-GKI_diff
	fi
	fi
fi
NUBIA_GKI_DIFF=${CONFIGS_DIR}/lahaina-${NUBIA_TARGET_PRODUCT}-GKI_diff

# For user variant build merge debugfs.config fragment.
if [ ${TARGET_BUILD_VARIANT} == "user" ]; then
	QCOM_DEBUG_FS_FRAG=`ls ${CONFIGS_DIR}/debugfs.config 2> /dev/null`
else
	QCOM_DEBUG_FS_FRAG=" "
fi

# Consolidate fragment may not be present for all platforms.
QCOM_CONSOLIDATE_FRAG=`ls ${CONFIGS_DIR}/${PLATFORM_NAME}_consolidate.config 2> /dev/null`

QCOM_GENERIC_PERF_FRAG=${CONFIGS_DIR}/${PLATFORM_NAME}.config
QCOM_GENERIC_DEBUG_FRAG=${CONFIGS_DIR}/${PLATFORM_NAME}-debug.config

export ARCH CROSS_COMPILE REAL_CC HOSTCC HOSTLD HOSTAR KERN_SRC KERN_OUT \
	CONFIGS_DIR BASE_DEFCONFIG QCOM_GKI_FRAG QCOM_QGKI_FRAG QCOM_DEBUG_FRAG NUBIA_DIFF NUBIA_GKI_DIFF
