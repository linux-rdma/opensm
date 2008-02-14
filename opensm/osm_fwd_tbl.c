/*
 * Copyright (c) 2004, 2005 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

/*
 * Abstract:
 *    Implementation of osm_fwd_tbl_t.
 * This object represents a unicast forwarding table.
 * This object is part of the opensm family of objects.
 *
 * Environment:
 *    Linux User Mode
 *
 * $Revision: 1.5 $
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <complib/cl_math.h>
#include <iba/ib_types.h>
#include <opensm/osm_fwd_tbl.h>

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_fwd_tbl_init(IN osm_fwd_tbl_t * const p_tbl,
		 IN const ib_switch_info_t * const p_si)
{
	uint16_t tbl_cap;
	ib_api_status_t status = IB_SUCCESS;

	/*
	   Determine the type and size of the forwarding table
	   used by this switch, then initialize accordingly.
	   The current implementation only supports switches
	   with linear forwarding tables.
	 */
	tbl_cap = cl_ntoh16(p_si->lin_cap);

	if (tbl_cap == 0) {
		/*
		   This switch does not support linear forwarding
		   tables.  Error out for now.
		 */
		status = IB_UNSUPPORTED;
		goto Exit;
	}

	p_tbl->p_rnd_tbl = NULL;

	p_tbl->p_lin_tbl = osm_lin_tbl_new(tbl_cap);

	if (p_tbl->p_lin_tbl == NULL) {
		status = IB_INSUFFICIENT_MEMORY;
		goto Exit;
	}

Exit:
	return (status);
}

/**********************************************************************
 **********************************************************************/
void osm_fwd_tbl_destroy(IN osm_fwd_tbl_t * const p_tbl)
{
	if (p_tbl->p_lin_tbl) {
		CL_ASSERT(p_tbl->p_rnd_tbl == NULL);
		osm_lin_tbl_delete(&p_tbl->p_lin_tbl);
	} else {
		osm_rand_tbl_delete(&p_tbl->p_rnd_tbl);
	}
}
