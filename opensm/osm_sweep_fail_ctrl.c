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
 *    Implementation of osm_sweep_fail_ctrl_t.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <opensm/osm_sweep_fail_ctrl.h>
#include <opensm/osm_msgdef.h>
#include <opensm/osm_sm.h>

/**********************************************************************
 **********************************************************************/
static void __osm_sweep_fail_ctrl_disp_callback(IN void *context,
						IN void *p_data)
{
	osm_sweep_fail_ctrl_t *const p_ctrl = (osm_sweep_fail_ctrl_t *) context;

	OSM_LOG_ENTER(p_ctrl->sm->p_log);

	UNUSED_PARAM(p_data);
	/*
	   Notify the state manager that we had a light sweep failure.
	 */
	p_ctrl->sm->p_subn->force_heavy_sweep = 1;

	OSM_LOG_EXIT(p_ctrl->sm->p_log);
}

/**********************************************************************
 **********************************************************************/
void osm_sweep_fail_ctrl_construct(IN osm_sweep_fail_ctrl_t * const p_ctrl)
{
	memset(p_ctrl, 0, sizeof(*p_ctrl));
	p_ctrl->h_disp = CL_DISP_INVALID_HANDLE;
}

/**********************************************************************
 **********************************************************************/
void osm_sweep_fail_ctrl_destroy(IN osm_sweep_fail_ctrl_t * const p_ctrl)
{
	CL_ASSERT(p_ctrl);
	cl_disp_unregister(p_ctrl->h_disp);
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_sweep_fail_ctrl_init(IN osm_sweep_fail_ctrl_t * const p_ctrl,
			 IN osm_sm_t * const sm)
{
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(sm->p_log);

	osm_sweep_fail_ctrl_construct(p_ctrl);
	p_ctrl->sm = sm;

	p_ctrl->h_disp = cl_disp_register(sm->p_disp,
					  OSM_MSG_LIGHT_SWEEP_FAIL,
					  __osm_sweep_fail_ctrl_disp_callback,
					  p_ctrl);

	if (p_ctrl->h_disp == CL_DISP_INVALID_HANDLE) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3501: "
			"Dispatcher registration failed\n");
		status = IB_INSUFFICIENT_RESOURCES;
		goto Exit;
	}

Exit:
	OSM_LOG_EXIT(sm->p_log);
	return (status);
}
