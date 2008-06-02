/*
 * Copyright (c) 2004-2006 Voltaire, Inc. All rights reserved.
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
 *    Implementation of osm_lin_fwd_tbl_t.
 * This object represents an linear forwarding table.
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

#include <stdlib.h>
#include <string.h>
#include <complib/cl_math.h>
#include <iba/ib_types.h>
#include <opensm/osm_lin_fwd_tbl.h>

static inline size_t __osm_lin_tbl_compute_obj_size(IN const uint32_t num_ports)
{
	return (sizeof(osm_lin_fwd_tbl_t) + (num_ports - 1));
}

/**********************************************************************
 **********************************************************************/
osm_lin_fwd_tbl_t *osm_lin_tbl_new(IN uint16_t const size)
{
	osm_lin_fwd_tbl_t *p_tbl;

	/*
	   The capacity reported by the switch includes LID 0,
	   so add 1 to the end of the range here for this assert.
	 */
	CL_ASSERT(size <= IB_LID_UCAST_END_HO + 1);
	p_tbl =
	    (osm_lin_fwd_tbl_t *) malloc(__osm_lin_tbl_compute_obj_size(size));

	/*
	   Initialize the table to OSM_NO_PATH, which means "invalid port"
	 */
	if (p_tbl != NULL) {
		memset(p_tbl, OSM_NO_PATH, __osm_lin_tbl_compute_obj_size(size));
		p_tbl->size = (uint16_t) size;
	}
	return (p_tbl);
}

/**********************************************************************
 **********************************************************************/
void osm_lin_tbl_delete(IN osm_lin_fwd_tbl_t ** const pp_tbl)
{
	free(*pp_tbl);
	*pp_tbl = NULL;
}
