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
 * 	Include file that defines which vendor files to compile.
 */

#ifndef _OSM_VENDOR_SELECT_H_
#define _OSM_VENDOR_SELECT_H_

/////////////////////////////////////////////////////
//
// MAD INTERFACE SELECTION
//
/////////////////////////////////////////////////////

/*
	TEST and UMADT must be specified in the 'make' command line,
	with VENDOR=test or VENDOR=umadt.
*/
#ifndef OSM_VENDOR_INTF_OPENIB
#ifndef OSM_VENDOR_INTF_TEST
#ifndef OSM_VENDOR_INTF_UMADT
#ifndef OSM_VENDOR_INTF_MTL
#ifndef OSM_VENDOR_INTF_TS
#ifndef OSM_VENDOR_INTF_SIM
#ifndef OSM_VENDOR_INTF_AL
#define OSM_VENDOR_INTF_OPENIB
#endif				/* AL */
#endif				/* TS */
#endif				/* SIM */
#endif				/* MTL */
#endif				/* UMADT */
#endif				/* TEST */
#endif				/* OPENIB */

#endif				/* _OSM_VENDOR_SELECT_H_ */
