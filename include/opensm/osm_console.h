/*
 * Copyright (c) 2005-2007 Voltaire, Inc. All rights reserved.
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

#ifndef _OSM_CONSOLE_H_
#define _OSM_CONSOLE_H_

#include <opensm/osm_base.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_opensm.h>

#define OSM_DISABLE_CONSOLE      "off"
#define OSM_LOCAL_CONSOLE        "local"
#define OSM_REMOTE_CONSOLE       "socket"
#define OSM_LOOPBACK_CONSOLE     "loopback"
#define OSM_CONSOLE_NAME         "OSM Console"

#define OSM_COMMAND_LINE_LEN	 120
#define OSM_COMMAND_PROMPT	 "$ "
#define OSM_DEFAULT_CONSOLE      OSM_DISABLE_CONSOLE
#define OSM_DEFAULT_CONSOLE_PORT 10000
#define OSM_DAEMON_NAME          "opensm"

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
void osm_console_init(osm_subn_opt_t * opt, osm_opensm_t * p_osm);
void osm_console(osm_opensm_t * p_osm);
void osm_console_exit(osm_opensm_t * p_osm);
int is_console_enabled(osm_subn_opt_t *p_opt);
END_C_DECLS
#endif				/* _OSM_CONSOLE_H_ */
