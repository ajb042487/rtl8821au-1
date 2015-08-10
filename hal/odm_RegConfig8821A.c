/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/

#include <odm_precomp.h>

void odm_ConfigBB_AGC_8821A(struct rtl_priv *rtlpriv, uint32_t Addr,
	uint32_t Bitmask, uint32_t Data)
{
	rtl_set_bbreg(rtlpriv, Addr, Bitmask, Data);
	/* Add 1us delay between BB/RF register setting. */
	udelay(1);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, "===> ODM_ConfigBBWithHeaderFile: [AGC_TAB] %08X %08X\n", Addr, Data);
}

