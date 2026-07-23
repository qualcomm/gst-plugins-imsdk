/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#if defined(ANDROID) && defined(AFR_LOGGING)

#include <utils/Log.h>

#undef LOG_TAG
#define LOG_TAG "auto-framing-stabilization"

#define AFR_LOG_INFO(fmt, args...)  ALOGI(fmt, ##args)
#define AFR_LOG_DEBUG(fmt, args...) ALOGD(fmt, ##args)
#define AFR_LOG_ERROR(fmt, args...) ALOGE(fmt, ##args)

#else

#define AFR_LOG_INFO(fmt, args...) {}
#define AFR_LOG_DEBUG(fmt, args...) {}
#define AFR_LOG_ERROR(fmt, args...) {}

#endif
