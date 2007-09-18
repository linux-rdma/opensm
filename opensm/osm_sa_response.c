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
 *    Implementation of osm_sa_resp_t.
 * This object represents the SA query responder.
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
#include <opensm/osm_sa_response.h>
#include <opensm/osm_helper.h>
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_opensm.h>
#include <opensm/osm_sa.h>

/**********************************************************************
 **********************************************************************/
void osm_sa_resp_construct(IN osm_sa_resp_t * const p_resp)
{
	memset(p_resp, 0, sizeof(*p_resp));
}

/**********************************************************************
 **********************************************************************/
void osm_sa_resp_destroy(IN osm_sa_resp_t * const p_resp)
{
	CL_ASSERT(p_resp);
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_sa_resp_init(IN osm_sa_resp_t * const p_resp,
		 IN osm_mad_pool_t * const p_pool,
		 IN osm_subn_t * const p_subn, IN osm_log_t * const p_log)
{
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(p_log, osm_sa_resp_init);

	osm_sa_resp_construct(p_resp);

	p_resp->p_subn = p_subn;
	p_resp->p_log = p_log;
	p_resp->p_pool = p_pool;

	OSM_LOG_EXIT(p_log);
	return (status);
}

/**********************************************************************
 **********************************************************************/
void
osm_sa_send_error(IN osm_sa_resp_t * const p_resp,
		  IN const osm_madw_t * const p_madw,
		  IN const ib_net16_t sa_status)
{
	osm_madw_t *p_resp_madw;
	ib_sa_mad_t *p_resp_sa_mad;
	ib_sa_mad_t *p_sa_mad;
	ib_api_status_t status;

	OSM_LOG_ENTER(p_resp->p_log, osm_sa_send_error);

	/* avoid races - if we are exiting - exit */
	if (osm_exit_flag) {
		osm_log(p_resp->p_log, OSM_LOG_DEBUG,
			"osm_sa_send_error: "
			"Ignoring requested send after exit\n");
		goto Exit;
	}

	p_resp_madw = osm_mad_pool_get(p_resp->p_pool,
				       p_madw->h_bind, MAD_BLOCK_SIZE,
				       &p_madw->mad_addr);

	if (p_resp_madw == NULL) {
		osm_log(p_resp->p_log, OSM_LOG_ERROR,
			"osm_sa_send_error: ERR 2301: "
			"Unable to acquire response MAD\n");
		goto Exit;
	}

	p_resp_sa_mad = osm_madw_get_sa_mad_ptr(p_resp_madw);
	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);

	/*  Copy the MAD header back into the response mad */
	*p_resp_sa_mad = *p_sa_mad;
	p_resp_sa_mad->status = sa_status;

	if (p_resp_sa_mad->method == IB_MAD_METHOD_SET)
		p_resp_sa_mad->method = IB_MAD_METHOD_GET;

	p_resp_sa_mad->method |= IB_MAD_METHOD_RESP_MASK;

	/*
	 * C15-0.1.5 - always return SM_Key = 0 (table 185 p 884)
	 */
	p_resp_sa_mad->sm_key = 0;

	/*
	 * o15-0.2.7 - The PathRecord Attribute ID shall be used in
	 * the response (to a SubnAdmGetMulti(MultiPathRecord)
	 */
	if (p_resp_sa_mad->attr_id == IB_MAD_ATTR_MULTIPATH_RECORD)
		p_resp_sa_mad->attr_id = IB_MAD_ATTR_PATH_RECORD;

	if (osm_log_is_active(p_resp->p_log, OSM_LOG_FRAMES))
		osm_dump_sa_mad(p_resp->p_log, p_resp_sa_mad, OSM_LOG_FRAMES);

	status = osm_sa_vendor_send(osm_madw_get_bind_handle(p_resp_madw),
				    p_resp_madw, FALSE, p_resp->p_subn);

	if (status != IB_SUCCESS) {
		osm_log(p_resp->p_log, OSM_LOG_ERROR,
			"osm_sa_send_error: ERR 2302: "
			"Error sending MAD (%s)\n", ib_get_err_str(status));
		/*  osm_mad_pool_put( p_resp->p_pool, p_resp_madw ); */
		goto Exit;
	}

      Exit:
	OSM_LOG_EXIT(p_resp->p_log);
}
