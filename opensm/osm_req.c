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
 *    Implementation of osm_req_t.
 * This object represents the generic attribute requester.
 * This object is part of the opensm family of objects.
 *
 * Environment:
 *    Linux User Mode
 *
 * $Revision: 1.6 $
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_debug.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_attrib_req.h>
#include <opensm/osm_log.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_mad_pool.h>
#include <opensm/osm_vl15intf.h>
#include <opensm/osm_msgdef.h>
#include <opensm/osm_opensm.h>

/**********************************************************************
  The plock MAY or MAY NOT be held before calling this function.
**********************************************************************/
ib_api_status_t
osm_req_get(IN osm_sm_t * sm,
	    IN const osm_dr_path_t * const p_path,
	    IN const uint16_t attr_id,
	    IN const uint32_t attr_mod,
	    IN const cl_disp_msgid_t err_msg,
	    IN const osm_madw_context_t * const p_context)
{
	osm_madw_t *p_madw;
	ib_api_status_t status = IB_SUCCESS;
	ib_net64_t tid;

	CL_ASSERT(sm);

	OSM_LOG_ENTER(sm->p_log);

	CL_ASSERT(p_path);
	CL_ASSERT(attr_id);

	/* do nothing if we are exiting ... */
	if (osm_exit_flag)
		goto Exit;

	/* p_context may be NULL. */

	p_madw = osm_mad_pool_get(sm->p_mad_pool,
				  p_path->h_bind, MAD_BLOCK_SIZE, NULL);

	if (p_madw == NULL) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR,
			"ERR 1101: Unable to acquire MAD\n");
		status = IB_INSUFFICIENT_RESOURCES;
		goto Exit;
	}

	tid = cl_hton64((uint64_t) cl_atomic_inc(&sm->sm_trans_id));

	if (osm_log_is_active(sm->p_log, OSM_LOG_DEBUG)) {
		OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
			"Getting %s (0x%X), modifier 0x%X, TID 0x%" PRIx64 "\n",
			ib_get_sm_attr_str(attr_id),
			cl_ntoh16(attr_id),
			cl_ntoh32(attr_mod), cl_ntoh64(tid));
	}

	ib_smp_init_new(osm_madw_get_smp_ptr(p_madw),
			IB_MAD_METHOD_GET,
			tid,
			attr_id,
			attr_mod,
			p_path->hop_count,
			sm->p_subn->opt.m_key,
			p_path->path, IB_LID_PERMISSIVE, IB_LID_PERMISSIVE);

	p_madw->mad_addr.dest_lid = IB_LID_PERMISSIVE;
	p_madw->mad_addr.addr_type.smi.source_lid = IB_LID_PERMISSIVE;
	p_madw->resp_expected = TRUE;
	p_madw->fail_msg = err_msg;

	/*
	   Fill in the mad wrapper context for the recipient.
	   In this case, the only thing the recipient needs is the
	   guid value.
	 */

	if (p_context)
		p_madw->context = *p_context;

	osm_vl15_post(sm->p_vl15, p_madw);

Exit:
	OSM_LOG_EXIT(sm->p_log);
	return (status);
}

/**********************************************************************
  The plock MAY or MAY NOT be held before calling this function.
**********************************************************************/
ib_api_status_t
osm_req_set(IN osm_sm_t * sm,
	    IN const osm_dr_path_t * const p_path,
	    IN const uint8_t * const p_payload,
	    IN const size_t payload_size,
	    IN const uint16_t attr_id,
	    IN const uint32_t attr_mod,
	    IN const cl_disp_msgid_t err_msg,
	    IN const osm_madw_context_t * const p_context)
{
	osm_madw_t *p_madw;
	ib_api_status_t status = IB_SUCCESS;
	ib_net64_t tid;

	CL_ASSERT(sm);

	OSM_LOG_ENTER(sm->p_log);

	CL_ASSERT(p_path);
	CL_ASSERT(attr_id);
	CL_ASSERT(p_payload);

	/* do nothing if we are exiting ... */
	if (osm_exit_flag)
		goto Exit;

	/* p_context may be NULL. */

	p_madw = osm_mad_pool_get(sm->p_mad_pool,
				  p_path->h_bind, MAD_BLOCK_SIZE, NULL);

	if (p_madw == NULL) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR,
			"ERR 1102: Unable to acquire MAD\n");
		status = IB_INSUFFICIENT_RESOURCES;
		goto Exit;
	}

	tid = cl_hton64((uint64_t) cl_atomic_inc(&sm->sm_trans_id));

	if (osm_log_is_active(sm->p_log, OSM_LOG_DEBUG)) {
		OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
			"Setting %s (0x%X), modifier 0x%X, TID 0x%" PRIx64 "\n",
			ib_get_sm_attr_str(attr_id),
			cl_ntoh16(attr_id),
			cl_ntoh32(attr_mod), cl_ntoh64(tid));
	}

	ib_smp_init_new(osm_madw_get_smp_ptr(p_madw),
			IB_MAD_METHOD_SET,
			tid,
			attr_id,
			attr_mod,
			p_path->hop_count,
			sm->p_subn->opt.m_key,
			p_path->path, IB_LID_PERMISSIVE, IB_LID_PERMISSIVE);

	p_madw->mad_addr.dest_lid = IB_LID_PERMISSIVE;
	p_madw->mad_addr.addr_type.smi.source_lid = IB_LID_PERMISSIVE;
	p_madw->resp_expected = TRUE;
	p_madw->fail_msg = err_msg;

	/*
	   Fill in the mad wrapper context for the recipient.
	   In this case, the only thing the recipient needs is the
	   guid value.
	 */

	if (p_context)
		p_madw->context = *p_context;

	memcpy(osm_madw_get_smp_ptr(p_madw)->data, p_payload, payload_size);

	osm_vl15_post(sm->p_vl15, p_madw);

Exit:
	OSM_LOG_EXIT(sm->p_log);
	return (status);
}
