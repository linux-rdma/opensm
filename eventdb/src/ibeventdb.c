/*
 * Copyright (c) 2007 The Regents of the University of California.
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

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <dlfcn.h>
#include <stdint.h>
#include <opensm/osm_event_db.h>
#include <complib/cl_qmap.h>
#include <complib/cl_passivelock.h>

/**
 * Port counter object.
 * Store all the port counters for a single port.
 */
typedef struct _db_port {
	perfmgr_edb_err_reading_t err_total;
	perfmgr_edb_err_reading_t err_previous;
	perfmgr_edb_data_cnt_reading_t dc_total;
	perfmgr_edb_data_cnt_reading_t dc_previous;
	time_t   last_reset;
} _db_port_t;

/**
 * group port counters for ports into the nodes
 */
#define NODE_NAME_SIZE (128)
typedef struct _db_node {
	cl_map_item_t   map_item; /* must be first */
	uint64_t        node_guid;
	_db_port_t     *ports;
	uint8_t         num_ports;
	char            node_name[NODE_NAME_SIZE];
} _db_node_t;

/**
 * all nodes in the system.
 */
typedef struct _db {
	cl_qmap_t   pc_data; /* stores type (_db_node_t *) */
	cl_plock_t  lock;
	osm_log_t  *osm_log;
} _db_t;


/** =========================================================================
 */
static void *
db_construct(osm_log_t *osm_log)
{
	_db_t *db = malloc(sizeof(*db));
	if (!db)
		return (NULL);

	cl_plock_construct(&(db->lock));
	cl_plock_init(&(db->lock));
	cl_qmap_init(&(db->pc_data));
	db->osm_log = osm_log;
	return ((void *)db);
}

/** =========================================================================
 */
static void
db_destroy(void *_db)
{
	_db_t *db = (_db_t *)_db;

	cl_plock_excl_acquire(&(db->lock));
	/* remove all the items in the qmap */
	while (!cl_is_qmap_empty(&(db->pc_data))) {
		cl_map_item_t *rc = cl_qmap_head(&(db->pc_data));
		cl_qmap_remove_item(&(db->pc_data), rc);
	}
	cl_plock_release(&(db->lock));
	cl_plock_destroy(&(db->lock));
	free(db);
}

/** =========================================================================
 */
static _db_node_t *
__malloc_node(void *_db, uint64_t guid, uint8_t num_ports,
		char *name)
{
	int         i = 0;
	time_t      cur_time = 0;
	_db_node_t *rc = malloc(sizeof(*rc));
	if (!rc)
		return (NULL);

	rc->ports = calloc(num_ports, sizeof(_db_port_t));
	if (!rc->ports)
		goto free_rc;
	rc->num_ports = num_ports;
	rc->node_guid = guid;

	cur_time = time(NULL);
	for (i = 0; i < num_ports; i++) {
		rc->ports[i].last_reset = cur_time;
		rc->ports[i].err_previous.time = cur_time;
		rc->ports[i].dc_previous.time = cur_time;
	}
	snprintf(rc->node_name, NODE_NAME_SIZE, "%s", name);

	return (rc);

free_rc:
	free(rc);
	return (NULL);
}

/** =========================================================================
 */
static void
__free_node(_db_node_t *node)
{
	if (!node)
		return;
	if (node->ports)
		free(node->ports);
	free(node);
}

/* insert nodes to the database */
static perfmgr_edb_err_t
__insert(void *_db, _db_node_t *node)
{
	_db_t         *db = (_db_t *)_db;
	cl_map_item_t *rc = cl_qmap_insert(&(db->pc_data), node->node_guid, (cl_map_item_t *)node);

	if ((void *)rc != (void *)node)
		return (PERFMGR_EVENT_DB_FAIL);
	return (PERFMGR_EVENT_DB_SUCCESS);
}

/**********************************************************************
 * Internal call db->lock should be held when calling
 **********************************************************************/
static inline _db_node_t *
_get(void *_db, uint64_t guid)
{
	_db_t               *db = (_db_t *)_db;
	cl_map_item_t       *rc = cl_qmap_get(&(db->pc_data), guid);
	const cl_map_item_t *end = cl_qmap_end(&(db->pc_data));

	if (rc == end)
		return (NULL);
	return ((_db_node_t *)rc);
}

static inline perfmgr_edb_err_t
bad_node_port(_db_node_t *node, uint8_t port)
{
	if (!node)
		return (PERFMGR_EVENT_DB_GUIDNOTFOUND);
	if (port == 0 || port >= node->num_ports)
		return (PERFMGR_EVENT_DB_PORTNOTFOUND);
	return (PERFMGR_EVENT_DB_SUCCESS);
}

/** =========================================================================
 */
static perfmgr_edb_err_t
db_create_entry(void *_db, uint64_t guid, uint8_t num_ports, char *name)
{
	_db_t              *db = (_db_t *)_db;
	perfmgr_edb_err_t   rc = PERFMGR_EVENT_DB_SUCCESS;

	cl_plock_excl_acquire(&(db->lock));
	if (!_get(db, guid)) {
		_db_node_t *pc_node = __malloc_node(db, guid, num_ports, name);
		if (!pc_node) {
			rc = PERFMGR_EVENT_DB_NOMEM;
			goto Exit;
		}
		if (__insert(db, pc_node)) {
			__free_node(pc_node);
			rc = PERFMGR_EVENT_DB_FAIL;
			goto Exit;
		}
	}
Exit:
	cl_plock_release(&(db->lock));
	return (rc);
}

/**********************************************************************
 **********************************************************************/
static perfmgr_edb_err_t
db_get_prev_err(void *_db, uint64_t guid,
		uint8_t port, perfmgr_edb_err_reading_t *reading)
{
	_db_t               *db = (_db_t *)_db;
	_db_node_t          *node = NULL;
	perfmgr_edb_err_t    rc = PERFMGR_EVENT_DB_SUCCESS;

	cl_plock_acquire(&(db->lock));

	node = _get(db, guid);
	if ((rc = bad_node_port(node, port)) != PERFMGR_EVENT_DB_SUCCESS)
		goto Exit;

	*reading = node->ports[port].err_previous;

Exit:
	cl_plock_release(&(db->lock));
	return (rc);
}

static perfmgr_edb_err_t
db_clear_prev_err(void *_db, uint64_t guid, uint8_t port)
{
	_db_t                      *db = (_db_t *)_db;
	_db_node_t                 *node = NULL;
	perfmgr_edb_err_reading_t  *previous = NULL;
	perfmgr_edb_err_t           rc = PERFMGR_EVENT_DB_SUCCESS;

	cl_plock_excl_acquire(&(db->lock));
	node = _get(db, guid);
	if ((rc = bad_node_port(node, port)) != PERFMGR_EVENT_DB_SUCCESS)
		goto Exit;

	previous = &(node->ports[port].err_previous);

	memset(previous, 0, sizeof(*previous));
	node->ports[port].err_previous.time = time(NULL);

Exit:
	cl_plock_release(&(db->lock));
	return (rc);
}

#if 0
/**********************************************************************
 * Dump a reading vs the previous reading to stdout
 **********************************************************************/
static void
dump_reading(uint64_t guid, uint8_t port_num, _db_port_t *port,
		perfmgr_edb_err_reading_t *cur)
{
	printf("\nGUID %" PRIx64 " Port %u :\n", guid, port_num);
	printf("sym %u <-- %u (%" PRIx64 ")\n", cur->symbol_err_cnt,
			port->previous.symbol_err_cnt, port->totals.symbol_err_cnt);
	printf("ler %u <-- %u (%" PRIx64 ")\n", cur->link_err_recover,
		port->previous.link_err_recover, port->totals.link_err_recover);
	printf("ld %u <-- %u (%" PRIx64 ")\n", cur->link_downed,
		port->previous.link_downed, port->totals.link_downed);
	printf("re %u <-- %u (%" PRIx64 ")\n", cur->rcv_err,
		port->previous.rcv_err, port->totals.rcv_err);
	printf("rrp %u <-- %u (%" PRIx64 ")\n", cur->rcv_rem_phys_err,
		port->previous.rcv_rem_phys_err, port->totals.rcv_rem_phys_err);
	printf("rsr %u <-- %u (%" PRIx64 ")\n", cur->rcv_switch_relay_err,
		port->previous.rcv_switch_relay_err, port->totals.rcv_switch_relay_err);
	printf("xd %u <-- %u (%" PRIx64 ")\n", cur->xmit_discards,
		port->previous.xmit_discards, port->totals.xmit_discards);
	printf("xce %u <-- %u (%" PRIx64 ")\n", cur->xmit_constraint_err,
		port->previous.xmit_constraint_err, port->totals.xmit_constraint_err);
	printf("rce %u <-- %u (%" PRIx64 ")\n", cur->rcv_constraint_err,
		port->previous.rcv_constraint_err, port->totals.rcv_constraint_err);
	printf("li %x <-- %x (%" PRIx64 ")\n", cur->link_integrity,
		port->previous.link_integrity, port->totals.link_int_err);
	printf("bo %x <-- %x (%" PRIx64 ")\n", cur->buffer_overrun,
		port->previous.buffer_overrun, port->totals.buffer_overrun_err);
	printf("vld %u <-- %u (%" PRIx64 ")\n", cur->vl15_dropped,
		port->previous.vl15_dropped, port->totals.vl15_dropped);

	printf("xd %u <-- %u (%" PRIx64 ")\n", cur->xmit_data,
		port->previous.xmit_data, port->totals.xmit_data);
	printf("rd %u <-- %u (%" PRIx64 ")\n", cur->rcv_data,
		port->previous.rcv_data, port->totals.rcv_data);
	printf("xp %u <-- %u (%" PRIx64 ")\n", cur->xmit_pkts,
		port->previous.xmit_pkts, port->totals.xmit_pkts);
	printf("rp %u <-- %u (%" PRIx64 ")\n", cur->rcv_pkts,
		port->previous.rcv_pkts, port->totals.rcv_pkts);
}
#endif

static perfmgr_edb_err_t
db_add_err_reading(void *_db, uint64_t guid,
                   uint8_t port, perfmgr_edb_err_reading_t *reading)
{
	_db_t                      *db = (_db_t *)_db;
	_db_port_t                 *p_port = NULL;
	_db_node_t                 *node = NULL;
	perfmgr_edb_err_reading_t  *previous = NULL;
	perfmgr_edb_err_t           rc = PERFMGR_EVENT_DB_SUCCESS;

	cl_plock_excl_acquire(&(db->lock));
	node = _get(db, guid);
	if ((rc = bad_node_port(node, port)) != PERFMGR_EVENT_DB_SUCCESS)
		goto Exit;

	p_port = &(node->ports[port]);
	previous = &(node->ports[port].err_previous);

#if 0
	dump_reading(guid, port, p_port, reading);
#endif

	/* calculate changes from previous reading */
	p_port->err_total.symbol_err_cnt       += (reading->symbol_err_cnt - previous->symbol_err_cnt);
	p_port->err_total.link_err_recover     += (reading->link_err_recover - previous->link_err_recover);
	p_port->err_total.link_downed          += (reading->link_downed - previous->link_downed);
	p_port->err_total.rcv_err              += (reading->rcv_err - previous->rcv_err);
	p_port->err_total.rcv_rem_phys_err     += (reading->rcv_rem_phys_err - previous->rcv_rem_phys_err);
	p_port->err_total.rcv_switch_relay_err += (reading->rcv_switch_relay_err - previous->rcv_switch_relay_err);
	p_port->err_total.xmit_discards        += (reading->xmit_discards - previous->xmit_discards);
	p_port->err_total.xmit_constraint_err  += (reading->xmit_constraint_err - previous->xmit_constraint_err);
	p_port->err_total.rcv_constraint_err   += (reading->rcv_constraint_err - previous->rcv_constraint_err);
	p_port->err_total.link_integrity       += reading->link_integrity - previous->link_integrity;
	p_port->err_total.buffer_overrun       += reading->buffer_overrun - previous->buffer_overrun;
	p_port->err_total.vl15_dropped         += (reading->vl15_dropped - previous->vl15_dropped);

	p_port->err_previous = *reading;

Exit:
	cl_plock_release(&(db->lock));
	return (rc);
}

static perfmgr_edb_err_t
db_add_dc_reading(void *_db, uint64_t guid,
			uint8_t port, perfmgr_edb_data_cnt_reading_t *reading)
{
	_db_t                           *db = (_db_t *)_db;
	_db_port_t                      *p_port = NULL;
	_db_node_t                      *node = NULL;
	perfmgr_edb_data_cnt_reading_t  *previous = NULL;
	perfmgr_edb_err_t                rc = PERFMGR_EVENT_DB_SUCCESS;

	cl_plock_excl_acquire(&(db->lock));
	node = _get(db, guid);
	if ((rc = bad_node_port(node, port)) != PERFMGR_EVENT_DB_SUCCESS)
		goto Exit;

	p_port = &(node->ports[port]);
	previous = &(node->ports[port].dc_previous);

	/* calculate changes from previous reading */
	p_port->dc_total.xmit_data           += (reading->xmit_data - previous->xmit_data);
	p_port->dc_total.rcv_data            += (reading->rcv_data - previous->rcv_data);
	p_port->dc_total.xmit_pkts           += (reading->xmit_pkts - previous->xmit_pkts);
	p_port->dc_total.rcv_pkts            += (reading->rcv_pkts - previous->rcv_pkts);
	p_port->dc_total.unicast_xmit_pkts   += (reading->unicast_xmit_pkts - previous->unicast_xmit_pkts);
	p_port->dc_total.unicast_rcv_pkts    += (reading->unicast_rcv_pkts - previous->unicast_rcv_pkts);
	p_port->dc_total.multicast_xmit_pkts += (reading->multicast_xmit_pkts - previous->multicast_xmit_pkts);
	p_port->dc_total.multicast_rcv_pkts  += (reading->multicast_rcv_pkts - previous->multicast_rcv_pkts);

	p_port->dc_previous = *reading;

Exit:
	cl_plock_release(&(db->lock));
	return (rc);
}

static perfmgr_edb_err_t
db_clear_prev_dc(void *_db, uint64_t guid, uint8_t port)
{
	_db_t                           *db = (_db_t *)_db;
	_db_node_t                      *node = NULL;
	perfmgr_edb_data_cnt_reading_t  *previous = NULL;
	perfmgr_edb_err_t                rc = PERFMGR_EVENT_DB_SUCCESS;

	cl_plock_excl_acquire(&(db->lock));
	node = _get(db, guid);
	if ((rc = bad_node_port(node, port)) != PERFMGR_EVENT_DB_SUCCESS)
		goto Exit;

	previous = &(node->ports[port].dc_previous);

	memset(previous, 0, sizeof(*previous));
	node->ports[port].dc_previous.time = time(NULL);

Exit:
	cl_plock_release(&(db->lock));
	return (rc);
}

static perfmgr_edb_err_t
db_get_prev_dc(void *_db, uint64_t guid,
		uint8_t port, perfmgr_edb_data_cnt_reading_t *reading)
{
	_db_t               *db = (_db_t *)_db;
	_db_node_t          *node = NULL;
	perfmgr_edb_err_t    rc = PERFMGR_EVENT_DB_SUCCESS;

	cl_plock_acquire(&(db->lock));

	node = _get(db, guid);
	if ((rc = bad_node_port(node, port)) != PERFMGR_EVENT_DB_SUCCESS)
		goto Exit;

	*reading = node->ports[port].dc_previous;

Exit:
	cl_plock_release(&(db->lock));
	return (rc);
}

/**********************************************************************
 * Output a tab deliminated output of the port counters
 **********************************************************************/
static void
__dump_node_mr(_db_node_t *node, FILE *fp)
{
	int i = 0;

	fprintf(fp, "\nName\tGUID\tPort\tLast Reset\t"
			"%s\t%s\t"
			"%s\t%s\t%s\t%s\t%s\t%s\t%s\t"
			"%s\t%s\t%s\t%s\t%s\t%s\t%s\t"
			"%s\t%s\t%s\t%s\n"
			,
			"symbol_err_cnt",
			"link_err_recover",
			"link_downed",
			"rcv_err",
			"rcv_rem_phys_err",
			"rcv_switch_relay_err",
			"xmit_discards",
			"xmit_constraint_err",
			"rcv_constraint_err",
			"link_int_err",
			"buf_overrun_err",
			"vl15_dropped",
			"xmit_data",
			"rcv_data",
			"xmit_pkts",
			"rcv_pkts",
			"unicast_xmit_pkts",
			"unicast_rcv_pkts",
			"multicast_xmit_pkts",
			"multicast_rcv_pkts"
			);
	for (i = 1; i < node->num_ports; i++)
	{
		char *since = ctime(&(node->ports[i].last_reset));
		since[strlen(since)-1] = '\0'; /* remove \n */

		fprintf(fp, "%s\t0x%" PRIx64 "\t%d\t%s\t%"PRIu64"\t%"PRIu64"\t"
			"%"PRIu64"\t%"PRIu64"\t%"PRIu64"\t%"PRIu64"\t"
			"%"PRIu64"\t%"PRIu64"\t%"PRIu64"\t"
			"%"PRIu64"\t%"PRIu64"\t%"PRIu64"\t"
			"%"PRIu64"\t%"PRIu64"\t%"PRIu64"\t%"PRIu64"\t"
			"%"PRIu64"\t%"PRIu64"\t%"PRIu64"\t%"PRIu64"\n"
			,
			node->node_name,
			node->node_guid,
			i,
			since,
			node->ports[i].err_total.symbol_err_cnt,
			node->ports[i].err_total.link_err_recover,
			node->ports[i].err_total.link_downed,
			node->ports[i].err_total.rcv_err,
			node->ports[i].err_total.rcv_rem_phys_err,
			node->ports[i].err_total.rcv_switch_relay_err,
			node->ports[i].err_total.xmit_discards,
			node->ports[i].err_total.xmit_constraint_err,
			node->ports[i].err_total.rcv_constraint_err,
			node->ports[i].err_total.link_integrity,
			node->ports[i].err_total.buffer_overrun,
			node->ports[i].err_total.vl15_dropped,

			node->ports[i].dc_total.xmit_data,
			node->ports[i].dc_total.rcv_data,
			node->ports[i].dc_total.xmit_pkts,
			node->ports[i].dc_total.rcv_pkts,
			node->ports[i].dc_total.unicast_xmit_pkts,
			node->ports[i].dc_total.unicast_rcv_pkts,
			node->ports[i].dc_total.multicast_xmit_pkts,
			node->ports[i].dc_total.multicast_rcv_pkts
			);
	}
}

/**********************************************************************
 * Output a human readable output of the port counters
 **********************************************************************/
static void
__dump_node_hr(_db_node_t *node, FILE *fp)
{
	int i = 0;

	fprintf(fp, "\n");
	for (i = 1; i < node->num_ports; i++)
	{
		char *since = ctime(&(node->ports[i].last_reset));
		since[strlen(since)-1] = '\0'; /* remove \n */

		fprintf(fp, "\"%s\" 0x%"PRIx64" port %d (Since %s)\n"
			"     symbol_err_cnt       : %"PRIu64"\n"
			"     link_err_recover     : %"PRIu64"\n"
			"     link_downed          : %"PRIu64"\n"
			"     rcv_err              : %"PRIu64"\n"
			"     rcv_rem_phys_err     : %"PRIu64"\n"
			"     rcv_switch_relay_err : %"PRIu64"\n"
			"     xmit_discards        : %"PRIu64"\n"
			"     xmit_constraint_err  : %"PRIu64"\n"
			"     rcv_constraint_err   : %"PRIu64"\n"
			"     link_integrity_err   : %"PRIu64"\n"
			"     buf_overrun_err      : %"PRIu64"\n"
			"     vl15_dropped         : %"PRIu64"\n"
			"     xmit_data            : %"PRIu64"\n"
			"     rcv_data             : %"PRIu64"\n"
			"     xmit_pkts            : %"PRIu64"\n"
			"     rcv_pkts             : %"PRIu64"\n"
			"     unicast_xmit_pkts    : %"PRIu64"\n"
			"     unicast_rcv_pkts     : %"PRIu64"\n"
			"     multicast_xmit_pkts  : %"PRIu64"\n"
			"     multicast_rcv_pkts   : %"PRIu64"\n"
			,
			node->node_name,
			node->node_guid,
			i,
			since,
			node->ports[i].err_total.symbol_err_cnt,
			node->ports[i].err_total.link_err_recover,
			node->ports[i].err_total.link_downed,
			node->ports[i].err_total.rcv_err,
			node->ports[i].err_total.rcv_rem_phys_err,
			node->ports[i].err_total.rcv_switch_relay_err,
			node->ports[i].err_total.xmit_discards,
			node->ports[i].err_total.xmit_constraint_err,
			node->ports[i].err_total.rcv_constraint_err,
			node->ports[i].err_total.link_integrity,
			node->ports[i].err_total.buffer_overrun,
			node->ports[i].err_total.vl15_dropped,

			node->ports[i].dc_total.xmit_data,
			node->ports[i].dc_total.rcv_data,
			node->ports[i].dc_total.xmit_pkts,
			node->ports[i].dc_total.rcv_pkts,
			node->ports[i].dc_total.unicast_xmit_pkts,
			node->ports[i].dc_total.unicast_rcv_pkts,
			node->ports[i].dc_total.multicast_xmit_pkts,
			node->ports[i].dc_total.multicast_rcv_pkts
			);
	}
}

/* Define a context for the __db_dump callback */
typedef struct {
	FILE                *fp;
	perfmgr_edb_dump_t   dump_type;
} dump_context_t;

/**********************************************************************
 **********************************************************************/
static void
__db_dump(cl_map_item_t * const p_map_item, void *context )
{
	_db_node_t     *node = (_db_node_t *)p_map_item;
	dump_context_t *c = (dump_context_t *)context;
	FILE           *fp = c->fp;

	switch (c->dump_type)
	{
		case PERFMGR_EVENT_DB_DUMP_MR:
			__dump_node_mr(node, fp);
			break;
		case PERFMGR_EVENT_DB_DUMP_HR:
		default:
			__dump_node_hr(node, fp);
			break;
	}
}

/**********************************************************************
 * dump the data to the file "file"
 **********************************************************************/
static perfmgr_edb_err_t
db_dump(void *_db, char *file, perfmgr_edb_dump_t dump_type)
{
	_db_t    *db = (_db_t *)_db;
	dump_context_t  context;

	context.fp = fopen(file, "w+");
	if (!context.fp)
		return (PERFMGR_EVENT_DB_FAIL);
	context.dump_type = dump_type;

	cl_plock_acquire(&(db->lock));
        cl_qmap_apply_func(&(db->pc_data), __db_dump, (void *)&context);
	cl_plock_release(&(db->lock));
	fclose(context.fp);
	return (PERFMGR_EVENT_DB_SUCCESS);
}

/**********************************************************************
 * call back to support the below
 **********************************************************************/
static void
__clear_counters(cl_map_item_t * const p_map_item, void *context )
{
	_db_node_t *node = (_db_node_t *)p_map_item;
	int         i = 0;
	time_t      ts = time(NULL);

	for (i = 0; i < node->num_ports; i++) {
		node->ports[i].err_total.symbol_err_cnt = 0;
		node->ports[i].err_total.link_err_recover = 0;
		node->ports[i].err_total.link_downed = 0;
		node->ports[i].err_total.rcv_err = 0;
		node->ports[i].err_total.rcv_rem_phys_err = 0;
		node->ports[i].err_total.rcv_switch_relay_err = 0;
		node->ports[i].err_total.xmit_discards = 0;
		node->ports[i].err_total.xmit_constraint_err = 0;
		node->ports[i].err_total.rcv_constraint_err = 0;
		node->ports[i].err_total.link_integrity = 0;
		node->ports[i].err_total.buffer_overrun = 0;
		node->ports[i].err_total.vl15_dropped = 0;
		node->ports[i].err_total.time = ts;

		node->ports[i].dc_total.xmit_data = 0;
		node->ports[i].dc_total.rcv_data = 0;
		node->ports[i].dc_total.xmit_pkts = 0;
		node->ports[i].dc_total.rcv_pkts = 0;
		node->ports[i].dc_total.unicast_xmit_pkts = 0;
		node->ports[i].dc_total.unicast_rcv_pkts = 0;
		node->ports[i].dc_total.multicast_xmit_pkts = 0;
		node->ports[i].dc_total.multicast_rcv_pkts = 0;
		node->ports[i].dc_total.time = ts;

		node->ports[i].last_reset = ts;
	}
}

/**********************************************************************
 * Clear the counters from the db
 **********************************************************************/
static void
db_clear_counters(void *_db)
{
	_db_t *db = (_db_t *)_db;

	cl_plock_excl_acquire(&(db->lock));
	cl_qmap_apply_func(&(db->pc_data), __clear_counters, (void *)db);
	cl_plock_release(&(db->lock));
}


/** =========================================================================
 * Define the object symbol for loading
 */
__perfmgr_event_db_t perfmgr_event_db =
{
interface_version: PERFMGR_EVENT_DB_INTERFACE_VER,
construct : db_construct,
destroy : db_destroy,
create_entry : db_create_entry,

add_err_reading : db_add_err_reading,
get_prev_err_reading : db_get_prev_err,
clear_prev_err : db_clear_prev_err,

add_dc_reading : db_add_dc_reading,
get_prev_dc_reading : db_get_prev_dc,
clear_prev_dc : db_clear_prev_dc,

clear_counters : db_clear_counters,
dump : db_dump
};

