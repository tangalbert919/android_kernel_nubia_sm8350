/*
 * Synaptics DSX touchscreen driver
 *
 * Copyright (C) 2012-2016 Synaptics Incorporated. All rights reserved.
 *
 * Copyright (C) 2012 Alexandra Chin <alexandra.chin@tw.synaptics.com>
 * Copyright (C) 2012 Scott Lin <scott.lin@tw.synaptics.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * INFORMATION CONTAINED IN THIS DOCUMENT IS PROVIDED "AS-IS," AND SYNAPTICS
 * EXPRESSLY DISCLAIMS ALL EXPRESS AND IMPLIED WARRANTIES, INCLUDING ANY
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE,
 * AND ANY WARRANTIES OF NON-INFRINGEMENT OF ANY INTELLECTUAL PROPERTY RIGHTS.
 * IN NO EVENT SHALL SYNAPTICS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, PUNITIVE, OR CONSEQUENTIAL DAMAGES ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OF THE INFORMATION CONTAINED IN THIS DOCUMENT, HOWEVER CAUSED
 * AND BASED ON ANY THEORY OF LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, AND EVEN IF SYNAPTICS WAS ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE. IF A TRIBUNAL OF COMPETENT JURISDICTION DOES
 * NOT PERMIT THE DISCLAIMER OF DIRECT DAMAGES OR ANY OTHER DAMAGES, SYNAPTICS'
 * TOTAL CUMULATIVE LIABILITY TO ANY PARTY SHALL NOT EXCEED ONE HUNDRED U.S.
 * DOLLARS.
 */

#ifndef _NUBIA_ESE_H_
#define _NUBIA_ESE_H_

#include <linux/version.h>

/*
#define F51_DISCRETE_FORCE
#ifdef F51_DISCRETE_FORCE
#define FORCE_LEVEL_ADDR 0x0419
#define FORCE_LEVEL_MAX 255
#define CAL_DATA_SIZE 144
#endif
#define SYNA_TDDI
*/
#define PDT_PROPS (0X00EF)
#define PDT_START (0x00E9)
#define PDT_END (0x00D0)
#define PDT_ENTRY_SIZE (0x0006)
#define PAGES_TO_SERVICE (10)
#define PAGE_SELECT_LEN (2)
#define ADDRESS_LEN (2)

#define MASK_16BIT 0xFFFF
#define MASK_8BIT 0xFF
#define MASK_7BIT 0x7F
#define MASK_6BIT 0x3F
#define MASK_5BIT 0x1F
#define MASK_4BIT 0x0F
#define MASK_3BIT 0x07
#define MASK_2BIT 0x03
#define MASK_1BIT 0x01



#endif
