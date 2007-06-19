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

#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <dlfcn.h>
#include <sys/stat.h>

#include <opensm/osm_perfmgr_db.h>

/** =========================================================================
 */
perfmgr_event_db_t *
perfmgr_edb_construct(osm_log_t *p_log, char *type)
{
	char                lib_name[PATH_MAX];
	perfmgr_event_db_t *rc = NULL;

	if (!type)
		return (NULL);

	/* find the plugin */
	snprintf(lib_name, PATH_MAX, "lib%s.so", type);

	rc = malloc(sizeof(*rc));
	if (!rc)
		return (NULL);

	rc->handle = dlopen(lib_name, RTLD_LAZY);
	if (!rc->handle)
	{
		osm_log(p_log, OSM_LOG_ERROR,
			"Failed to open PM Database \"%s\" : \"%s\"\n",
			lib_name, dlerror());
		goto DLOPENFAIL;
	}

	rc->db_impl = (__perfmgr_event_db_t *)dlsym(rc->handle, "perfmgr_event_db");
	if (!rc->db_impl)
	{
		osm_log(p_log, OSM_LOG_ERROR,
			"Failed to find perfmgr_event_db symbol in \"%s\" : \"%s\"\n",
			lib_name, dlerror());
		goto Exit;
	}

	/* Check the version to make sure this module will work with us */
	if (rc->db_impl->interface_version != PERFMGR_EVENT_DB_INTERFACE_VER)
	{
		osm_log(p_log, OSM_LOG_ERROR,
			"perfmgr_event_db symbol is the wrong version %d != %d\n",
			rc->db_impl->interface_version,
			PERFMGR_EVENT_DB_INTERFACE_VER);
		goto Exit;
	}

	if (!rc->db_impl->construct)
	{
		osm_log(p_log, OSM_LOG_ERROR,
			"perfmgr_event_db symbol has no construct function\n");
		goto Exit;
	}

	rc->db_data = rc->db_impl->construct(p_log);

	if (!rc->db_data)
		goto Exit;

	rc->p_log = p_log;
	return (rc);

Exit:
	dlclose(rc->handle);
DLOPENFAIL:
	free(rc);
	return (NULL);
}

/** =========================================================================
 */
void
perfmgr_edb_destroy(perfmgr_event_db_t *db)
{
	if (db)
	{
		if (db->db_impl->destroy)
			db->db_impl->destroy(db->db_data);
		free(db);
	}
}

#define CHECK_FUNC(func) \
	if (!func) { return (PERFMGR_EVENT_DB_NOT_IMPL); }

/**********************************************************************
 **********************************************************************/
perfmgr_edb_err_t
perfmgr_edb_create_entry(perfmgr_event_db_t *db, uint64_t guid,
		uint8_t num_ports, char *name)
{
	CHECK_FUNC (db->db_impl->create_entry);
	return(db->db_impl->create_entry(db->db_data, guid, num_ports, name));
}

/**********************************************************************
 * perfmgr_edb_err_reading_t functions
 **********************************************************************/
perfmgr_edb_err_t
perfmgr_edb_add_err_reading(perfmgr_event_db_t *db, uint64_t guid,
                   uint8_t port, perfmgr_edb_err_reading_t *reading)
{
	CHECK_FUNC (db->db_impl->add_err_reading);
	return (db->db_impl->add_err_reading(db->db_data, guid,
				port, reading));
}

perfmgr_edb_err_t perfmgr_edb_get_prev_err(perfmgr_event_db_t *db, uint64_t guid,
		uint8_t port, perfmgr_edb_err_reading_t *reading)
{
	CHECK_FUNC (db->db_impl->get_prev_err_reading);
	return (db->db_impl->get_prev_err_reading(db->db_data, guid, port, reading));
}

perfmgr_edb_err_t
perfmgr_edb_clear_prev_err(perfmgr_event_db_t *db, uint64_t guid, uint8_t port)
{
	CHECK_FUNC (db->db_impl->clear_prev_err);
	return (db->db_impl->clear_prev_err(db->db_data, guid, port));
}

/**********************************************************************
 * perfmgr_edb_data_cnt_reading_t functions
 **********************************************************************/
perfmgr_edb_err_t
perfmgr_edb_add_dc_reading(perfmgr_event_db_t *db, uint64_t guid,
                   uint8_t port, perfmgr_edb_data_cnt_reading_t *reading)
{
	CHECK_FUNC (db->db_impl->add_dc_reading);
	return (db->db_impl->add_dc_reading(db->db_data, guid, port, reading));
}

perfmgr_edb_err_t perfmgr_edb_get_prev_dc(perfmgr_event_db_t *db, uint64_t guid,
		uint8_t port, perfmgr_edb_data_cnt_reading_t *reading)
{
	CHECK_FUNC (db->db_impl->get_prev_dc_reading);
	return (db->db_impl->get_prev_dc_reading(db->db_data, guid, port, reading));
}

perfmgr_edb_err_t
perfmgr_edb_clear_prev_dc(perfmgr_event_db_t *db, uint64_t guid, uint8_t port)
{
	CHECK_FUNC (db->db_impl->clear_prev_dc);
	return (db->db_impl->clear_prev_dc(db->db_data, guid, port));
}

/**********************************************************************
 * Clear all the counters from the db
 **********************************************************************/
void perfmgr_edb_clear_counters(perfmgr_event_db_t *db)
{
	if (db->db_impl->clear_counters)
		db->db_impl->clear_counters(db->db_data);
}

/**********************************************************************
 * dump the data to the file "file"
 **********************************************************************/
perfmgr_edb_err_t
perfmgr_edb_dump(perfmgr_event_db_t *db, char *file, perfmgr_edb_dump_t dump_type)
{
	CHECK_FUNC (db->db_impl->dump);
	return (db->db_impl->dump(db->db_data, file, dump_type));
}

/**********************************************************************
 * Fill in the various DB objects from their wire counter parts
 **********************************************************************/
void
perfmgr_edb_fill_err_read(ib_port_counters_t *wire_read,
			perfmgr_edb_err_reading_t *reading)
{
	reading->symbol_err_cnt = cl_ntoh16(wire_read->symbol_err_cnt);
	reading->link_err_recover = cl_ntoh16(wire_read->link_err_recover);
	reading->link_downed = wire_read->link_downed;
	reading->rcv_err = wire_read->rcv_err;
	reading->rcv_rem_phys_err = cl_ntoh16(wire_read->rcv_rem_phys_err);
	reading->rcv_switch_relay_err = cl_ntoh16(wire_read->rcv_switch_relay_err);
	reading->xmit_discards = cl_ntoh16(wire_read->xmit_discards);
	reading->xmit_constraint_err = cl_ntoh16(wire_read->xmit_constraint_err);
	reading->rcv_constraint_err = wire_read->rcv_constraint_err;
	reading->link_integrity = PC_LINK_INT(wire_read->link_int_buffer_overrun);
	reading->buffer_overrun = PC_BUF_OVERRUN(wire_read->link_int_buffer_overrun);
	reading->vl15_dropped = cl_ntoh16(wire_read->vl15_dropped);
	reading->time = time(NULL);
}

void
perfmgr_edb_fill_data_cnt_read_pc(ib_port_counters_t *wire_read,
				perfmgr_edb_data_cnt_reading_t *reading)
{
	reading->xmit_data = cl_ntoh32(wire_read->xmit_data);
	reading->rcv_data = cl_ntoh32(wire_read->rcv_data);
	reading->xmit_pkts = cl_ntoh32(wire_read->xmit_pkts);
	reading->rcv_pkts = cl_ntoh32(wire_read->rcv_pkts);
	reading->unicast_xmit_pkts = 0;
	reading->unicast_rcv_pkts = 0;
	reading->multicast_xmit_pkts = 0;
	reading->multicast_rcv_pkts = 0;
	reading->time = time(NULL);
}

void
perfmgr_edb_fill_data_cnt_read_epc(ib_port_counters_ext_t *wire_read,
				perfmgr_edb_data_cnt_reading_t *reading)
{
	reading->xmit_data = cl_ntoh64(wire_read->xmit_data);
	reading->rcv_data = cl_ntoh64(wire_read->rcv_data);
	reading->xmit_pkts = cl_ntoh64(wire_read->xmit_pkts);
	reading->rcv_pkts = cl_ntoh64(wire_read->rcv_pkts);
	reading->unicast_xmit_pkts = cl_ntoh64(wire_read->unicast_xmit_pkts);
	reading->unicast_rcv_pkts = cl_ntoh64(wire_read->unicast_rcv_pkts);
	reading->multicast_xmit_pkts = cl_ntoh64(wire_read->multicast_xmit_pkts);
	reading->multicast_rcv_pkts = cl_ntoh64(wire_read->multicast_rcv_pkts);
	reading->time = time(NULL);
}

