/*
 * Copyright (c) 2004-2007 Voltaire, Inc. All rights reserved.
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
 *    Implementation of osm_subn_t.
 * This object represents an IBA subnet.
 * This object is part of the opensm family of objects.
 *
 * Environment:
 *    Linux User Mode
 *
 * $Revision: 1.9 $
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <errno.h>
#include <complib/cl_debug.h>
#include <complib/cl_log.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_opensm.h>
#include <opensm/osm_log.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_port.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_remote_sm.h>
#include <opensm/osm_partition.h>
#include <opensm/osm_node.h>
#include <opensm/osm_multicast.h>
#include <opensm/osm_inform.h>
#include <opensm/osm_console.h>
#include <opensm/osm_perfmgr.h>
#include <opensm/osm_event_plugin.h>

#if defined(PATH_MAX)
#define OSM_PATH_MAX	(PATH_MAX + 1)
#elif defined (_POSIX_PATH_MAX)
#define OSM_PATH_MAX	(_POSIX_PATH_MAX + 1)
#else
#define OSM_PATH_MAX	256
#endif

/**********************************************************************
 **********************************************************************/
void
osm_subn_construct(
  IN osm_subn_t* const p_subn )
{
  memset( p_subn, 0, sizeof(*p_subn) );
  cl_ptr_vector_construct( &p_subn->port_lid_tbl );
  cl_qmap_init( &p_subn->sw_guid_tbl );
  cl_qmap_init( &p_subn->node_guid_tbl );
  cl_qmap_init( &p_subn->port_guid_tbl );
  cl_qmap_init( &p_subn->sm_guid_tbl );
  cl_qlist_init( &p_subn->sa_sr_list );
  cl_qlist_init( &p_subn->sa_infr_list );
  cl_qmap_init( &p_subn->rtr_guid_tbl );
  cl_qmap_init( &p_subn->prtn_pkey_tbl );
  cl_qmap_init( &p_subn->mgrp_mlid_tbl );
  cl_list_construct( &p_subn->new_ports_list );
  cl_list_init( &p_subn->new_ports_list, 10 );
  cl_list_construct( &p_subn->light_sweep_physp_list );
  cl_list_init( &p_subn->light_sweep_physp_list, 5 );
}

/**********************************************************************
 **********************************************************************/
void
osm_subn_destroy(
  IN osm_subn_t* const p_subn )
{
  osm_node_t      *p_node, *p_next_node;
  osm_port_t      *p_port, *p_next_port;
  osm_switch_t    *p_sw,   *p_next_sw;
  osm_remote_sm_t *p_rsm,  *p_next_rsm;  
  osm_prtn_t      *p_prtn, *p_next_prtn;
  osm_mgrp_t      *p_mgrp, *p_next_mgrp;
  osm_infr_t      *p_infr, *p_next_infr;

  /* it might be a good idea to de-allocate all known objects */
  p_next_node = (osm_node_t*)cl_qmap_head( &p_subn->node_guid_tbl );
  while( p_next_node != (osm_node_t*)cl_qmap_end( &p_subn->node_guid_tbl ) )
  {
    p_node = p_next_node;
    p_next_node = (osm_node_t*)cl_qmap_next( &p_node->map_item );
    osm_node_delete( &p_node );
  }

  p_next_port = (osm_port_t*)cl_qmap_head( &p_subn->port_guid_tbl );
  while( p_next_port != (osm_port_t*)cl_qmap_end( &p_subn->port_guid_tbl ) )
  {
    p_port = p_next_port;
    p_next_port = (osm_port_t*)cl_qmap_next( &p_port->map_item );
    osm_port_delete( &p_port );
  }

  p_next_sw = (osm_switch_t*)cl_qmap_head( &p_subn->sw_guid_tbl );
  while( p_next_sw != (osm_switch_t*)cl_qmap_end( &p_subn->sw_guid_tbl ) )
  {
    p_sw = p_next_sw;
    p_next_sw = (osm_switch_t*)cl_qmap_next( &p_sw->map_item );
    osm_switch_delete( &p_sw );
  }

  p_next_rsm = (osm_remote_sm_t*)cl_qmap_head( &p_subn->sm_guid_tbl );
  while( p_next_rsm != (osm_remote_sm_t*)cl_qmap_end( &p_subn->sm_guid_tbl ) )
  {
    p_rsm = p_next_rsm;
    p_next_rsm = (osm_remote_sm_t*)cl_qmap_next( &p_rsm->map_item );
    free( p_rsm );
  }

  p_next_prtn = (osm_prtn_t*)cl_qmap_head( &p_subn->prtn_pkey_tbl );
  while( p_next_prtn != (osm_prtn_t*)cl_qmap_end( &p_subn->prtn_pkey_tbl ) )
  {
    p_prtn = p_next_prtn;
    p_next_prtn = (osm_prtn_t*)cl_qmap_next( &p_prtn->map_item );
    osm_prtn_delete( &p_prtn );
  }

  p_next_mgrp = (osm_mgrp_t*)cl_qmap_head( &p_subn->mgrp_mlid_tbl );
  while( p_next_mgrp != (osm_mgrp_t*)cl_qmap_end( &p_subn->mgrp_mlid_tbl ) )
  {
    p_mgrp = p_next_mgrp;
    p_next_mgrp = (osm_mgrp_t*)cl_qmap_next( &p_mgrp->map_item );
    osm_mgrp_delete( p_mgrp );
  }

  p_next_infr = (osm_infr_t*)cl_qlist_head( &p_subn->sa_infr_list );
  while (p_next_infr != (osm_infr_t*)cl_qlist_end( &p_subn->sa_infr_list ) )
  {
    p_infr = p_next_infr;
    p_next_infr = (osm_infr_t*)cl_qlist_next( &p_infr->list_item );
    osm_infr_delete( p_infr );
  }

  cl_list_remove_all( &p_subn->new_ports_list );
  cl_list_destroy( &p_subn->new_ports_list );

  cl_list_remove_all( &p_subn->light_sweep_physp_list );
  cl_list_destroy( &p_subn->light_sweep_physp_list );

  cl_ptr_vector_destroy( &p_subn->port_lid_tbl );
  cl_map_remove_all(&(p_subn->opt.port_prof_ignore_guids));
  cl_map_destroy(&(p_subn->opt.port_prof_ignore_guids));
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_subn_init(
  IN osm_subn_t* const p_subn,
  IN osm_opensm_t * const p_osm,
  IN const osm_subn_opt_t* const p_opt )
{
  cl_status_t status;

  p_subn->p_osm = p_osm;

  status = cl_ptr_vector_init( &p_subn->port_lid_tbl,
                               OSM_SUBNET_VECTOR_MIN_SIZE,
                               OSM_SUBNET_VECTOR_GROW_SIZE );
  if( status != CL_SUCCESS )
    return( status );

  status = cl_ptr_vector_set_capacity( &p_subn->port_lid_tbl,
                                       OSM_SUBNET_VECTOR_CAPACITY );
  if( status != CL_SUCCESS )
    return( status );

  /*
    LID zero is not valid.  NULL out this entry for the
    convenience of other code.
  */
  cl_ptr_vector_set( &p_subn->port_lid_tbl, 0, NULL );

  p_subn->opt = *p_opt;
  p_subn->max_unicast_lid_ho = IB_LID_UCAST_END_HO;
  p_subn->max_multicast_lid_ho = IB_LID_MCAST_END_HO;
  p_subn->min_ca_mtu = IB_MAX_MTU;
  p_subn->min_ca_rate = IB_MAX_RATE;

  /* note that insert and remove are part of the port_profile thing */
  cl_map_init(&(p_subn->opt.port_prof_ignore_guids), 10);

  /* ignore_existing_lfts follows reassign_lfts on first sweep */
  p_subn->ignore_existing_lfts = p_subn->opt.reassign_lfts;

  /* we assume master by default - so we only need to set it true if STANDBY */
  p_subn->coming_out_of_standby = FALSE;

  return( IB_SUCCESS );
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_get_gid_by_mad_addr(
  IN osm_log_t*            p_log,
  IN const osm_subn_t     *p_subn,
  IN const osm_mad_addr_t *p_mad_addr,
  OUT ib_gid_t            *p_gid)
{
  const cl_ptr_vector_t*  p_tbl;
  const osm_port_t*       p_port = NULL;

  if ( p_gid == NULL ) 
  {
    osm_log( p_log, OSM_LOG_ERROR,
             "osm_get_gid_by_mad_addr: ERR 7505: "
             "Provided output GID is NULL\n");
    return(IB_INVALID_PARAMETER);
  }

  /* Find the port gid of the request in the subnet */
  p_tbl = &p_subn->port_lid_tbl;

  CL_ASSERT( cl_ptr_vector_get_size(p_tbl) < 0x10000 );

  if ((uint16_t)cl_ptr_vector_get_size(p_tbl) > 
      cl_ntoh16(p_mad_addr->dest_lid))
  {
    p_port = cl_ptr_vector_get( p_tbl, cl_ntoh16(p_mad_addr->dest_lid) );
    if ( p_port == NULL )
    {
      osm_log( p_log, OSM_LOG_DEBUG,
               "osm_get_gid_by_mad_addr: "
               "Did not find any port with LID: 0x%X\n",
               cl_ntoh16(p_mad_addr->dest_lid)
               );
      return(IB_INVALID_PARAMETER);
    }
    p_gid->unicast.interface_id = p_port->p_physp->port_guid;
    p_gid->unicast.prefix = p_subn->opt.subnet_prefix;
  }
  else
  {
    /* The dest_lid is not in the subnet table - this is an error */
    osm_log( p_log, OSM_LOG_ERROR,
             "osm_get_gid_by_mad_addr: ERR 7501: "
             "LID is out of range: 0x%X\n",
             cl_ntoh16(p_mad_addr->dest_lid)
             );
    return(IB_INVALID_PARAMETER);
  }

  return( IB_SUCCESS );
}

/**********************************************************************
 **********************************************************************/
osm_physp_t*
osm_get_physp_by_mad_addr(
  IN osm_log_t*            p_log,
  IN const osm_subn_t     *p_subn,
  IN osm_mad_addr_t       *p_mad_addr )
{
  const cl_ptr_vector_t*  p_port_lid_tbl;
  osm_port_t*             p_port = NULL;
  osm_physp_t*            p_physp = NULL;

  /* Find the port gid of the request in the subnet */
  p_port_lid_tbl = &p_subn->port_lid_tbl;

  CL_ASSERT( cl_ptr_vector_get_size(p_port_lid_tbl) < 0x10000 );

  if ((uint16_t)cl_ptr_vector_get_size(p_port_lid_tbl) >
      cl_ntoh16(p_mad_addr->dest_lid))
  {
    p_port = cl_ptr_vector_get( p_port_lid_tbl, cl_ntoh16(p_mad_addr->dest_lid) );
    if (p_port == NULL)
    {
      /* The port is not in the port_lid table - this is an error */
      osm_log( p_log, OSM_LOG_ERROR,
               "osm_get_physp_by_mad_addr: ERR 7502: "
               "Cannot locate port object by lid: 0x%X\n",
               cl_ntoh16(p_mad_addr->dest_lid)
               );
    
      goto Exit;
    }
    p_physp = p_port->p_physp;
  }
  else
  {
    /* The dest_lid is not in the subnet table - this is an error */
    osm_log( p_log, OSM_LOG_ERROR,
             "osm_get_physp_by_mad_addr: ERR 7503: "
             "Lid is out of range: 0x%X\n",
             cl_ntoh16(p_mad_addr->dest_lid)
             );
  }

 Exit:
  return p_physp;
}

/**********************************************************************
 **********************************************************************/
osm_port_t*
osm_get_port_by_mad_addr(
  IN osm_log_t*            p_log,
  IN const osm_subn_t     *p_subn,
  IN osm_mad_addr_t       *p_mad_addr )
{
  const cl_ptr_vector_t*  p_port_lid_tbl;
  osm_port_t*             p_port = NULL;

  /* Find the port gid of the request in the subnet */
  p_port_lid_tbl = &p_subn->port_lid_tbl;

  CL_ASSERT( cl_ptr_vector_get_size(p_port_lid_tbl) < 0x10000 );

  if ((uint16_t)cl_ptr_vector_get_size(p_port_lid_tbl) > 
      cl_ntoh16(p_mad_addr->dest_lid))
  {
    p_port = 
      cl_ptr_vector_get( p_port_lid_tbl, cl_ntoh16(p_mad_addr->dest_lid) );
  }
  else
  {
    /* The dest_lid is not in the subnet table - this is an error */
    osm_log( p_log, OSM_LOG_ERROR,
             "osm_get_port_by_mad_addr: ERR 7504: "
             "Lid is out of range: 0x%X\n",
             cl_ntoh16(p_mad_addr->dest_lid)
             );
  }

  return p_port;
}

/**********************************************************************
 **********************************************************************/
osm_switch_t *
osm_get_switch_by_guid(
  IN const osm_subn_t *p_subn,
  IN uint64_t          guid)
{
  osm_switch_t *p_switch;

  p_switch = (osm_switch_t*)cl_qmap_get( &(p_subn->sw_guid_tbl), guid );
  if( p_switch == (osm_switch_t*)cl_qmap_end( &(p_subn->sw_guid_tbl)) )
    p_switch = NULL;
  return p_switch;
}

/**********************************************************************
 **********************************************************************/
osm_node_t *
osm_get_node_by_guid(
  IN osm_subn_t const *p_subn,
  IN uint64_t          guid)
{
  osm_node_t *p_node;

  p_node = (osm_node_t*)cl_qmap_get( &(p_subn->node_guid_tbl), guid );
  if( p_node == (osm_node_t*)cl_qmap_end( &(p_subn->node_guid_tbl)) )
    p_node = NULL;
  return p_node;
}

/**********************************************************************
 **********************************************************************/
osm_port_t *
osm_get_port_by_guid(
   IN osm_subn_t const *p_subn,
   IN uint64_t          guid)
{
  osm_port_t *p_port;

  p_port = (osm_port_t*)cl_qmap_get( &(p_subn->port_guid_tbl), guid );
  if( p_port == (osm_port_t*)cl_qmap_end( &(p_subn->port_guid_tbl)) )
    p_port = NULL;
  return p_port;
}

/**********************************************************************
 **********************************************************************/
static void
subn_set_default_qos_options(
  IN osm_qos_options_t *opt)
{
  opt->max_vls = OSM_DEFAULT_QOS_MAX_VLS;
  opt->high_limit = OSM_DEFAULT_QOS_HIGH_LIMIT;
  opt->vlarb_high = OSM_DEFAULT_QOS_VLARB_HIGH;
  opt->vlarb_low = OSM_DEFAULT_QOS_VLARB_LOW;
  opt->sl2vl = OSM_DEFAULT_QOS_SL2VL;
}

/**********************************************************************
 **********************************************************************/
void
osm_subn_set_default_opt(
  IN osm_subn_opt_t* const p_opt )
{
  memset(p_opt, 0, sizeof(osm_subn_opt_t));
  p_opt->guid = 0;
  p_opt->m_key = OSM_DEFAULT_M_KEY;
  p_opt->sm_key = OSM_DEFAULT_SM_KEY;
  p_opt->subnet_prefix = IB_DEFAULT_SUBNET_PREFIX;
  p_opt->m_key_lease_period = 0;
  p_opt->sweep_interval = OSM_DEFAULT_SWEEP_INTERVAL_SECS;
  p_opt->max_wire_smps = OSM_DEFAULT_SMP_MAX_ON_WIRE;
  p_opt->console = OSM_DEFAULT_CONSOLE;
  p_opt->console_port = OSM_DEFAULT_CONSOLE_PORT;
  p_opt->transaction_timeout = OSM_DEFAULT_TRANS_TIMEOUT_MILLISEC;
  /* by default we will consider waiting for 50x transaction timeout normal */
  p_opt->max_msg_fifo_timeout = 50*OSM_DEFAULT_TRANS_TIMEOUT_MILLISEC;
  p_opt->sm_priority = OSM_DEFAULT_SM_PRIORITY;
  p_opt->lmc = OSM_DEFAULT_LMC;
  p_opt->lmc_esp0 = FALSE;
  p_opt->max_op_vls = OSM_DEFAULT_MAX_OP_VLS;
  p_opt->force_link_speed = 0;
  p_opt->reassign_lids = FALSE;
  p_opt->reassign_lfts = TRUE;
  p_opt->ignore_other_sm = FALSE;
  p_opt->single_thread = FALSE;
  p_opt->no_multicast_option = FALSE;
  p_opt->disable_multicast = FALSE;
  p_opt->force_log_flush = FALSE;
  p_opt->subnet_timeout = OSM_DEFAULT_SUBNET_TIMEOUT;
  p_opt->packet_life_time = OSM_DEFAULT_SWITCH_PACKET_LIFE;
  p_opt->vl_stall_count = OSM_DEFAULT_VL_STALL_COUNT;
  p_opt->leaf_vl_stall_count = OSM_DEFAULT_LEAF_VL_STALL_COUNT;
  p_opt->head_of_queue_lifetime = OSM_DEFAULT_HEAD_OF_QUEUE_LIFE;
  p_opt->leaf_head_of_queue_lifetime = OSM_DEFAULT_LEAF_HEAD_OF_QUEUE_LIFE;
  p_opt->local_phy_errors_threshold = OSM_DEFAULT_ERROR_THRESHOLD;
  p_opt->overrun_errors_threshold = OSM_DEFAULT_ERROR_THRESHOLD;
  p_opt->sminfo_polling_timeout = OSM_SM_DEFAULT_POLLING_TIMEOUT_MILLISECS;
  p_opt->polling_retry_number = OSM_SM_DEFAULT_POLLING_RETRY_NUMBER;
  p_opt->force_heavy_sweep = FALSE;
  p_opt->log_flags = OSM_LOG_DEFAULT_LEVEL;
  p_opt->honor_guid2lid_file = FALSE;
  p_opt->daemon = FALSE;
  p_opt->sm_inactive = FALSE;
#ifdef ENABLE_OSM_PERF_MGR
  p_opt->perfmgr = FALSE;
  p_opt->perfmgr_sweep_time_s = OSM_PERFMGR_DEFAULT_SWEEP_TIME_S;
  p_opt->perfmgr_max_outstanding_queries =
	  OSM_PERFMGR_DEFAULT_MAX_OUTSTANDING_QUERIES;
  p_opt->event_db_dump_file = OSM_PERFMGR_DEFAULT_DUMP_FILE;
#endif /* ENABLE_OSM_PERF_MGR */

  p_opt->event_plugin_name = OSM_DEFAULT_EVENT_PLUGIN_NAME;

  p_opt->dump_files_dir = getenv("OSM_TMP_DIR");
  if (!p_opt->dump_files_dir || !(*p_opt->dump_files_dir))
    p_opt->dump_files_dir = OSM_DEFAULT_TMP_DIR;

  p_opt->log_file = OSM_DEFAULT_LOG_FILE;
  p_opt->log_max_size = 0;
  p_opt->partition_config_file = OSM_DEFAULT_PARTITION_CONFIG_FILE;
  p_opt->no_partition_enforcement = FALSE;
  p_opt->no_qos = TRUE;
  p_opt->accum_log_file = TRUE;
  p_opt->port_profile_switch_nodes = FALSE;
  p_opt->pfn_ui_pre_lid_assign = NULL;
  p_opt->ui_pre_lid_assign_ctx = NULL;
  p_opt->pfn_ui_mcast_fdb_assign = NULL;
  p_opt->ui_mcast_fdb_assign_ctx = NULL;
  p_opt->sweep_on_trap = TRUE;
  p_opt->testability_mode = OSM_TEST_MODE_NONE;
  p_opt->routing_engine_name = NULL;
  p_opt->connect_roots = FALSE;
  p_opt->lid_matrix_dump_file = NULL;
  p_opt->ucast_dump_file = NULL;
  p_opt->root_guid_file = NULL;
  p_opt->cn_guid_file = NULL;
  p_opt->sa_db_file = NULL;
  p_opt->exit_on_fatal = TRUE;
  p_opt->enable_quirks = FALSE;
  p_opt->no_clients_rereg = FALSE;
  subn_set_default_qos_options(&p_opt->qos_options);
  subn_set_default_qos_options(&p_opt->qos_ca_options);
  subn_set_default_qos_options(&p_opt->qos_sw0_options);
  subn_set_default_qos_options(&p_opt->qos_swe_options);
  subn_set_default_qos_options(&p_opt->qos_rtr_options);
}

/**********************************************************************
 **********************************************************************/
static inline void
__osm_subn_opts_unpack_net64(
  IN char *p_req_key,
  IN char *p_key,
  IN char *p_val_str,
  IN uint64_t *p_val)
{
  uint64_t val;

  if (!strcmp(p_req_key, p_key))
  {
#if __WORDSIZE == 64
    val = strtoul(p_val_str, NULL, 0);
#else
    val = strtoull(p_val_str, NULL, 0);
#endif
    if (cl_hton64(val) != *p_val)
    {
      char buff[128];
      sprintf(buff, " Loading Cached Option:%s = 0x%016" PRIx64 "\n",
              p_key, val);
      printf(buff);
      cl_log_event("OpenSM", CL_LOG_INFO, buff, NULL, 0);
      *p_val = cl_ntoh64(val);
    }
  }
}

/**********************************************************************
 **********************************************************************/
static inline void
__osm_subn_opts_unpack_uint32(
  IN char *p_req_key,
  IN char *p_key,
  IN char *p_val_str,
  IN uint32_t *p_val)
{
  uint32_t val;

  if (!strcmp(p_req_key, p_key))
  {
    val = strtoul(p_val_str, NULL, 0);
    if (val != *p_val)
    {
      char buff[128];
      sprintf(buff, " Loading Cached Option:%s = %u\n",
              p_key, val);
      printf(buff);
      cl_log_event("OpenSM", CL_LOG_INFO, buff, NULL, 0);
      *p_val = val;
    }
  }
}

/**********************************************************************
 **********************************************************************/
static inline void
__osm_subn_opts_unpack_uint16(
  IN char *p_req_key,
  IN char *p_key,
  IN char *p_val_str,
  IN uint16_t *p_val)
{
  uint16_t val;

  if (!strcmp(p_req_key, p_key))
  {
    val = (uint16_t)strtoul(p_val_str, NULL, 0);
    if (val != *p_val)
    {
      char buff[128];
      sprintf(buff, " Loading Cached Option:%s = %u\n",
              p_key, val);
      printf(buff);
      cl_log_event("OpenSM", CL_LOG_INFO, buff, NULL, 0);
      *p_val = val;
    }
  }
}

/**********************************************************************
 **********************************************************************/
static inline void
__osm_subn_opts_unpack_net16(
  IN char *p_req_key,
  IN char *p_key,
  IN char *p_val_str,
  IN uint16_t *p_val)
{
  if (!strcmp(p_req_key, p_key))
  {
    uint32_t val;
    val = strtoul(p_val_str, NULL, 0);
    CL_ASSERT( val < 0x10000 );
    if (cl_hton32(val) != *p_val) 
    {
      char buff[128];
      sprintf(buff, " Loading Cached Option:%s = 0x%04x\n",
              p_key, val);
      printf(buff);
      cl_log_event("OpenSM", CL_LOG_INFO, buff, NULL, 0);
      *p_val = cl_hton16((uint16_t)val);
    }
  }
}

/**********************************************************************
 **********************************************************************/
static inline void
__osm_subn_opts_unpack_uint8(
  IN char *p_req_key,
  IN char *p_key,
  IN char *p_val_str,
  IN uint8_t *p_val)
{
  if (!strcmp(p_req_key, p_key))
  {
    uint32_t val;
    val = strtoul(p_val_str, NULL, 0);
    CL_ASSERT( val < 0x100 );
    if (val != *p_val) 
    {
      char buff[128];
      sprintf(buff, " Loading Cached Option:%s = %u\n",
              p_key, val);
      printf(buff);
      cl_log_event("OpenSM", CL_LOG_INFO, buff, NULL, 0);
      *p_val = (uint8_t)val;
    }
  }
}

/**********************************************************************
 **********************************************************************/
static inline void
__osm_subn_opts_unpack_boolean(
  IN char *p_req_key,
  IN char *p_key,
  IN char *p_val_str,
  IN boolean_t *p_val)
{
  if (!strcmp(p_req_key, p_key) && p_val_str)
  {
    boolean_t val;
    if (strcmp("TRUE", p_val_str))
      val = FALSE;
    else
      val = TRUE;
    
    if (val != *p_val) {
      char buff[128];
      sprintf(buff, " Loading Cached Option:%s = %s\n",
              p_key, p_val_str);
      printf(buff);
      cl_log_event("OpenSM", CL_LOG_INFO, buff, NULL, 0);
      *p_val = val;
    }
  }
}

/**********************************************************************
 **********************************************************************/
static inline void
__osm_subn_opts_unpack_charp(
  IN char *p_req_key,
  IN char *p_key,
  IN char *p_val_str,  
  IN char **p_val)
{
  if (!strcmp(p_req_key, p_key) && p_val_str)
  {
    if ((*p_val == NULL) || strcmp(p_val_str, *p_val))
    {
      char buff[128];
      sprintf(buff, " Loading Cached Option:%s = %s\n",
              p_key, p_val_str);
      printf(buff);
      cl_log_event("OpenSM", CL_LOG_INFO, buff, NULL, 0);

      /*
        Ignore the possible memory leak here;
        the pointer may be to a static default.
      */
      *p_val = (char *)malloc( strlen(p_val_str) +1 );
      strcpy( *p_val, p_val_str);
    }
  }
}

/**********************************************************************
 **********************************************************************/
static void
subn_parse_qos_options(
  IN const char *prefix,
  IN char *p_key,
  IN char *p_val_str,
  IN osm_qos_options_t *opt)
{
  char name[256];

  snprintf(name, sizeof(name), "%s_max_vls", prefix);
  __osm_subn_opts_unpack_uint32(name, p_key, p_val_str, &opt->max_vls);
  snprintf(name, sizeof(name), "%s_high_limit", prefix);
  __osm_subn_opts_unpack_uint32(name, p_key, p_val_str, &opt->high_limit);
  snprintf(name, sizeof(name), "%s_vlarb_high", prefix);
  __osm_subn_opts_unpack_charp(name, p_key, p_val_str, &opt->vlarb_high);
  snprintf(name, sizeof(name), "%s_vlarb_low", prefix);
  __osm_subn_opts_unpack_charp(name, p_key, p_val_str, &opt->vlarb_low);
  snprintf(name, sizeof(name), "%s_sl2vl", prefix);
  __osm_subn_opts_unpack_charp(name, p_key, p_val_str, &opt->sl2vl);
}

static int
subn_dump_qos_options(
  FILE *file,
  const char *set_name,
  const char *prefix,
  osm_qos_options_t *opt)
{
	return fprintf(file, "# %s\n"
		"%s_max_vls %u\n"
		"%s_high_limit %u\n"
		"%s_vlarb_high %s\n"
		"%s_vlarb_low %s\n"
		"%s_sl2vl %s\n",
		set_name,
		prefix, opt->max_vls,
		prefix, opt->high_limit,
		prefix, opt->vlarb_high,
		prefix, opt->vlarb_low,
		prefix, opt->sl2vl);
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_subn_rescan_conf_file(
  IN osm_subn_opt_t* const p_opts )
{
  char *p_cache_dir = getenv("OSM_CACHE_DIR");
  char file_name[OSM_PATH_MAX];
  FILE *opts_file;
  char line[1024];
  char *p_key, *p_val ,*p_last;
  
  /* try to open the options file from the cache dir */
  if (!p_cache_dir || !(*p_cache_dir))
    p_cache_dir = OSM_DEFAULT_CACHE_DIR;

  strcpy(file_name, p_cache_dir);
  strcat(file_name, "/opensm.opts");
  
  opts_file = fopen(file_name, "r");
  if (!opts_file)
    return (errno == ENOENT) ? IB_SUCCESS : IB_ERROR;

  while (fgets(line, 1023, opts_file) != NULL)
  {
    /* get the first token */
    p_key = strtok_r(line, " \t\n", &p_last);
    if (p_key)
    {
      p_val = strtok_r(NULL, " \t\n", &p_last);

      subn_parse_qos_options("qos",
        p_key, p_val, &p_opts->qos_options);

      subn_parse_qos_options("qos_ca",
        p_key, p_val, &p_opts->qos_ca_options);

      subn_parse_qos_options("qos_sw0",
        p_key, p_val, &p_opts->qos_sw0_options);

      subn_parse_qos_options("qos_swe",
        p_key, p_val, &p_opts->qos_swe_options);

      subn_parse_qos_options("qos_rtr",
        p_key, p_val, &p_opts->qos_rtr_options);

    }
  }
  fclose(opts_file);

  return IB_SUCCESS;
}

/**********************************************************************
 **********************************************************************/

static void
osm_subn_verify_max_vls(
  IN unsigned *max_vls,
  IN char *key)
{
  char buff[128];

  if (*max_vls > 15) 
  {
    sprintf(buff, " Invalid Cached Option:%s=%u:"
            "Using Default:%u\n",
            key, *max_vls, OSM_DEFAULT_QOS_MAX_VLS);
    printf(buff);
    cl_log_event("OpenSM", CL_LOG_INFO, buff, NULL, 0);
    *max_vls = OSM_DEFAULT_QOS_MAX_VLS;
  }
}

static void
osm_subn_verify_high_limit(
  IN unsigned *high_limit,
  IN char *key)
{
  char buff[128];

  if (*high_limit > 255) 
  {
    sprintf(buff, " Invalid Cached Option:%s=%u:" 
            "Using Default:%u\n",
            key, *high_limit, OSM_DEFAULT_QOS_HIGH_LIMIT);
    printf(buff);
    cl_log_event("OpenSM", CL_LOG_INFO, buff, NULL, 0);
    *high_limit = OSM_DEFAULT_QOS_HIGH_LIMIT;
  }
}

static void
osm_subn_verify_vlarb(
  IN char *vlarb,
  IN char *key)
{
  if (vlarb) 
  {
    char buff[128];
    char *str, *tok, *end, *ptr;
    int count = 0;

    str = (char *)malloc(strlen(vlarb)+1);
    strcpy(str, vlarb);
    
    tok = strtok_r(str, ",\n", &ptr);
    while (tok) 
    {
      char *vl_str, *weight_str;

      vl_str = tok;
      weight_str = strchr(tok, ':');

      if (weight_str)
      {
        long vl, weight;

        *weight_str = '\0';
        weight_str++;

        vl = strtol(vl_str, &end, 0);

        if (*end)
        {
          sprintf(buff, " Warning: Cached Option %s:vl=%s improperly formatted\n", 
                  key, vl_str);
          printf(buff);
          cl_log_event("OpenSM", CL_LOG_INFO, buff, NULL, 0);
        }
        else if (vl < 0 || vl > 14) 
        {
          sprintf(buff, " Warning: Cached Option %s:vl=%ld out of range\n", 
                  key, vl);
          printf(buff);
          cl_log_event("OpenSM", CL_LOG_INFO, buff, NULL, 0);
        }

        weight = strtol(weight_str, &end, 0);

        if (*end)
        {
          sprintf(buff, " Warning: Cached Option %s:weight=%s improperly formatted\n", 
                  key, weight_str);
          printf(buff);
          cl_log_event("OpenSM", CL_LOG_INFO, buff, NULL, 0);
        }
        else if (weight < 0 || weight > 255) 
        {
          sprintf(buff, " Warning: Cached Option %s:weight=%ld out of range\n", 
                  key, weight);
          printf(buff);
          cl_log_event("OpenSM", CL_LOG_INFO, buff, NULL, 0);
        }
      }
      else
      {
        sprintf(buff, " Warning: Cached Option %s:vl:weight=%s improperly formatted\n", 
                key, tok);
        printf(buff);
        cl_log_event("OpenSM", CL_LOG_INFO, buff, NULL, 0);
      }

      count++;
      tok = strtok_r(NULL, ",\n", &ptr);
    }

    if (count > 64)
    {
      sprintf(buff, " Warning: Cached Option %s: > 64 listed: "
              "excess vl:weight pairs will be dropped\n", 
              key);
      printf(buff);
      cl_log_event("OpenSM", CL_LOG_INFO, buff, NULL, 0);
    }

    free(str);
  }
}

static void
osm_subn_verify_sl2vl(
  IN char *sl2vl,
  IN char *key)
{
  if (sl2vl)
  {
    char buff[128];
    char *str, *tok, *end, *ptr;
    int count = 0;

    str = (char *)malloc(strlen(sl2vl)+1);
    strcpy(str, sl2vl);
    
    tok = strtok_r(str, ",\n", &ptr);
    while (tok) 
    {
      long vl = strtol(tok, &end, 0);
      
      if (*end)
      {
        sprintf(buff, " Warning: Cached Option %s:vl=%s improperly formatted\n", 
                key, tok);
        printf(buff);
        cl_log_event("OpenSM", CL_LOG_INFO, buff, NULL, 0);
      }
      else if (vl < 0 || vl > 15) 
      {
        sprintf(buff, " Warning: Cached Option %s:vl=%ld out of range\n", 
                key, vl);
        printf(buff);
        cl_log_event("OpenSM", CL_LOG_INFO, buff, NULL, 0);
      }

      count++;
      tok = strtok_r(NULL, ",\n", &ptr);
    }

    if (count < 16) 
    {
      sprintf(buff, " Warning: Cached Option %s: < 16 VLs listed\n", 
              key);
      printf(buff);
      cl_log_event("OpenSM", CL_LOG_INFO, buff, NULL, 0);
    }
    if (count > 16)
    {
      sprintf(buff, " Warning: Cached Option %s: > 16 listed: "
              "excess VLs will be dropped\n", 
              key);
      printf(buff);
      cl_log_event("OpenSM", CL_LOG_INFO, buff, NULL, 0);
    }

    free(str);
  }
}

static void
osm_subn_verify_conf_file(
  IN osm_subn_opt_t* const p_opts )
{
  char buff[128];

  if (p_opts->lmc > 7)
  {
    sprintf(buff, " Invalid Cached Option Value:lmc = %u:"
            "Using Default:%u\n",
            p_opts->lmc, OSM_DEFAULT_LMC);
    printf(buff);
    cl_log_event("OpenSM", CL_LOG_INFO, buff, NULL, 0);
    p_opts->lmc = OSM_DEFAULT_LMC;
  }

  if (15 < p_opts->sm_priority)
  {
    sprintf(buff, " Invalid Cached Option Value:sm_priority = %u:"
            "Using Default:%u\n",
            p_opts->sm_priority, OSM_DEFAULT_SM_PRIORITY);
    printf(buff);
    cl_log_event("OpenSM", CL_LOG_INFO, buff, NULL, 0);
    p_opts->sm_priority = OSM_DEFAULT_SM_PRIORITY;
  }

  if (strcmp(p_opts->console, "off")
      && strcmp(p_opts->console, "local")
#ifdef ENABLE_OSM_CONSOLE_SOCKET
      && strcmp(p_opts->console, "socket")
#endif
      )
  {
    sprintf(buff, " Invalid Cached Option Value:console = %s"
            "Using Default:%s\n",
            p_opts->console, OSM_DEFAULT_CONSOLE);
    printf(buff);
    cl_log_event("OpenSM", CL_LOG_INFO, buff, NULL, 0);
    p_opts->console = OSM_DEFAULT_CONSOLE;
  }

  if (p_opts->no_qos == FALSE) 
  {
    osm_subn_verify_max_vls(&(p_opts->qos_options.max_vls), 
                            "qos_max_vls"); 
    osm_subn_verify_max_vls(&(p_opts->qos_ca_options.max_vls), 
                            "qos_ca_max_vls");
    osm_subn_verify_max_vls(&(p_opts->qos_sw0_options.max_vls), 
                            "qos_sw0_max_vls");
    osm_subn_verify_max_vls(&(p_opts->qos_swe_options.max_vls), 
                            "qos_swe_max_vls");
    osm_subn_verify_max_vls(&(p_opts->qos_rtr_options.max_vls), 
                            "qos_rtr_max_vls");

    osm_subn_verify_high_limit(&(p_opts->qos_options.high_limit), 
                               "qos_high_limit"); 
    osm_subn_verify_high_limit(&(p_opts->qos_ca_options.high_limit), 
                               "qos_ca_high_limit");
    osm_subn_verify_high_limit(&(p_opts->qos_sw0_options.high_limit), 
                               "qos_sw0_high_limit");
    osm_subn_verify_high_limit(&(p_opts->qos_swe_options.high_limit), 
                               "qos_swe_high_limit");
    osm_subn_verify_high_limit(&(p_opts->qos_rtr_options.high_limit), 
                               "qos_rtr_high_limit");
    
    osm_subn_verify_vlarb(p_opts->qos_options.vlarb_low, 
                          "qos_vlarb_low"); 
    osm_subn_verify_vlarb(p_opts->qos_ca_options.vlarb_low, 
                          "qos_ca_vlarb_low");
    osm_subn_verify_vlarb(p_opts->qos_sw0_options.vlarb_low, 
                          "qos_sw0_vlarb_low");
    osm_subn_verify_vlarb(p_opts->qos_swe_options.vlarb_low, 
                          "qos_swe_vlarb_low");
    osm_subn_verify_vlarb(p_opts->qos_rtr_options.vlarb_low, 
                          "qos_rtr_vlarb_low");

    osm_subn_verify_vlarb(p_opts->qos_options.vlarb_high, 
                          "qos_vlarb_high"); 
    osm_subn_verify_vlarb(p_opts->qos_ca_options.vlarb_high, 
                          "qos_ca_vlarb_high");
    osm_subn_verify_vlarb(p_opts->qos_sw0_options.vlarb_high, 
                          "qos_sw0_vlarb_high");
    osm_subn_verify_vlarb(p_opts->qos_swe_options.vlarb_high, 
                          "qos_swe_vlarb_high");
    osm_subn_verify_vlarb(p_opts->qos_rtr_options.vlarb_high, 
                          "qos_rtr_vlarb_high");

    osm_subn_verify_sl2vl(p_opts->qos_options.sl2vl, 
                          "qos_sl2vl"); 
    osm_subn_verify_sl2vl(p_opts->qos_ca_options.sl2vl, 
                          "qos_ca_sl2vl");
    osm_subn_verify_sl2vl(p_opts->qos_sw0_options.sl2vl, 
                          "qos_sw0_sl2vl");
    osm_subn_verify_sl2vl(p_opts->qos_swe_options.sl2vl, 
                          "qos_swe_sl2vl");
    osm_subn_verify_sl2vl(p_opts->qos_rtr_options.sl2vl, 
                          "qos_rtr_sl2vl");
  }
#ifdef ENABLE_OSM_PERF_MGR
  if (p_opts->perfmgr_sweep_time_s < 1)
  {
    sprintf(buff, " Invalid Cached Option Value:perfmgr_sweep_time_s = %u"
            "Using Default:%u\n",
            p_opts->perfmgr_sweep_time_s, OSM_PERFMGR_DEFAULT_SWEEP_TIME_S);
    printf(buff);
    cl_log_event("OpenSM", CL_LOG_INFO, buff, NULL, 0);
    p_opts->perfmgr_sweep_time_s = OSM_PERFMGR_DEFAULT_SWEEP_TIME_S;
  }
  if (p_opts->perfmgr_max_outstanding_queries < 1)
  {
    sprintf(buff, " Invalid Cached Option Value:perfmgr_max_outstanding_queries = %u"
            "Using Default:%u\n",
            p_opts->perfmgr_max_outstanding_queries,
	    OSM_PERFMGR_DEFAULT_MAX_OUTSTANDING_QUERIES);
    printf(buff);
    cl_log_event("OpenSM", CL_LOG_INFO, buff, NULL, 0);
    p_opts->perfmgr_max_outstanding_queries =
	    OSM_PERFMGR_DEFAULT_MAX_OUTSTANDING_QUERIES;
  }
#endif
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_subn_parse_conf_file(
  IN osm_subn_opt_t* const p_opts )
{
  char *p_cache_dir = getenv("OSM_CACHE_DIR");
  char file_name[OSM_PATH_MAX];
  FILE *opts_file;
  char line[1024];
  char *p_key, *p_val ,*p_last;
  
  /* try to open the options file from the cache dir */
  if (!p_cache_dir || !(*p_cache_dir))
    p_cache_dir = OSM_DEFAULT_CACHE_DIR;

  strcpy(file_name, p_cache_dir);
  strcat(file_name, "/opensm.opts");
  
  opts_file = fopen(file_name, "r");
  if (!opts_file)
    return (errno == ENOENT) ? IB_SUCCESS : IB_ERROR;

  while (fgets(line, 1023, opts_file) != NULL)
  {
    /* get the first token */
    p_key = strtok_r(line, " \t\n", &p_last);
    if (p_key)
    {
      p_val = strtok_r(NULL, " \t\n", &p_last);
      
      __osm_subn_opts_unpack_net64(
        "guid", p_key, p_val, &p_opts->guid);

      __osm_subn_opts_unpack_net64(
        "m_key", p_key, p_val, &p_opts->m_key);
  
      __osm_subn_opts_unpack_net64(
        "sm_key", p_key, p_val, &p_opts->sm_key);

      __osm_subn_opts_unpack_net64(
        "subnet_prefix",
        p_key, p_val, &p_opts->subnet_prefix);

      __osm_subn_opts_unpack_net16(
        "m_key_lease_period",
        p_key, p_val, &p_opts->m_key_lease_period);

      __osm_subn_opts_unpack_uint32(
        "sweep_interval",
        p_key, p_val, &p_opts->sweep_interval);

      __osm_subn_opts_unpack_uint32( 
        "max_wire_smps",
        p_key, p_val, &p_opts->max_wire_smps);

      __osm_subn_opts_unpack_charp(
        "console",
        p_key, p_val, &p_opts->console);

      __osm_subn_opts_unpack_uint16(
        "console_port",
        p_key, p_val, &p_opts->console_port);

      __osm_subn_opts_unpack_uint32(
        "transaction_timeout",
        p_key, p_val, &p_opts->transaction_timeout);

      __osm_subn_opts_unpack_uint32(
        "max_msg_fifo_timeout",
        p_key, p_val, &p_opts->max_msg_fifo_timeout);

      __osm_subn_opts_unpack_uint8( 
        "sm_priority",
        p_key, p_val, &p_opts->sm_priority);

      __osm_subn_opts_unpack_uint8(
        "lmc",
        p_key, p_val, &p_opts->lmc);

      __osm_subn_opts_unpack_boolean(
        "lmc_esp0",
        p_key, p_val, &p_opts->lmc_esp0);

      __osm_subn_opts_unpack_uint8(
        "max_op_vls",
        p_key, p_val, &p_opts->max_op_vls);

      __osm_subn_opts_unpack_uint8(
        "force_link_speed",
        p_key, p_val, &p_opts->force_link_speed);

      __osm_subn_opts_unpack_boolean(
        "reassign_lids",
        p_key, p_val, &p_opts->reassign_lids);

      __osm_subn_opts_unpack_boolean(
        "reassign_lfts",
        p_key, p_val, &p_opts->reassign_lfts);
      
      __osm_subn_opts_unpack_boolean( 
        "ignore_other_sm",
        p_key, p_val, &p_opts->ignore_other_sm);
      
      __osm_subn_opts_unpack_boolean( 
        "single_thread",
        p_key, p_val, &p_opts->single_thread);

      __osm_subn_opts_unpack_boolean( 
        "no_multicast_option",
        p_key, p_val, &p_opts->no_multicast_option);
      
      __osm_subn_opts_unpack_boolean( 
        "disable_multicast",
        p_key, p_val, &p_opts->disable_multicast);
      
      __osm_subn_opts_unpack_boolean( 
        "force_log_flush",
        p_key, p_val, &p_opts->force_log_flush);

      __osm_subn_opts_unpack_uint8( 
        "subnet_timeout",
        p_key, p_val, &p_opts->subnet_timeout);
      
      __osm_subn_opts_unpack_uint8(
        "packet_life_time",
        p_key, p_val, &p_opts->packet_life_time);
      
      __osm_subn_opts_unpack_uint8(
        "vl_stall_count",
        p_key, p_val, &p_opts->vl_stall_count);
      
      __osm_subn_opts_unpack_uint8(
        "leaf_vl_stall_count",
        p_key, p_val, &p_opts->leaf_vl_stall_count);
      
      __osm_subn_opts_unpack_uint8(
        "head_of_queue_lifetime",
        p_key, p_val, &p_opts->head_of_queue_lifetime);
      
      __osm_subn_opts_unpack_uint8(
        "leaf_head_of_queue_lifetime",
        p_key, p_val, &p_opts->leaf_head_of_queue_lifetime);

      __osm_subn_opts_unpack_uint8(
        "local_phy_errors_threshold",
        p_key, p_val, &p_opts->local_phy_errors_threshold);

      __osm_subn_opts_unpack_uint8(
        "overrun_errors_threshold",
        p_key, p_val, &p_opts->overrun_errors_threshold);
      
      __osm_subn_opts_unpack_uint32(
        "sminfo_polling_timeout",
        p_key, p_val, &p_opts->sminfo_polling_timeout);
      
      __osm_subn_opts_unpack_uint32( 
        "polling_retry_number",
        p_key, p_val, &p_opts->polling_retry_number);
      
      __osm_subn_opts_unpack_boolean(
        "force_heavy_sweep",
        p_key, p_val, &p_opts->force_heavy_sweep);

      __osm_subn_opts_unpack_uint8( 
        "log_flags",
        p_key, p_val, &p_opts->log_flags);

      __osm_subn_opts_unpack_boolean(
        "port_profile_switch_nodes",
        p_key, p_val, &p_opts->port_profile_switch_nodes);
      
      __osm_subn_opts_unpack_boolean(
        "sweep_on_trap",
        p_key, p_val, &p_opts->sweep_on_trap);

      __osm_subn_opts_unpack_charp(
        "routing_engine",
        p_key, p_val, &p_opts->routing_engine_name);

      __osm_subn_opts_unpack_boolean(
        "connect_roots",
        p_key, p_val, &p_opts->connect_roots);

      __osm_subn_opts_unpack_charp(
        "log_file", p_key, p_val, &p_opts->log_file);

      __osm_subn_opts_unpack_uint32(
        "log_max_size",
        p_key, p_val, (uint32_t *)&p_opts->log_max_size);
      
      __osm_subn_opts_unpack_charp(
        "partition_config_file",
	p_key, p_val, &p_opts->partition_config_file);

      __osm_subn_opts_unpack_boolean(
        "no_partition_enforcement",
        p_key, p_val, &p_opts->no_partition_enforcement);

      __osm_subn_opts_unpack_boolean(
        "no_qos",
        p_key, p_val, &p_opts->no_qos);

      __osm_subn_opts_unpack_boolean(
        "accum_log_file",
        p_key, p_val, &p_opts->accum_log_file);

      __osm_subn_opts_unpack_charp(
        "dump_files_dir",
        p_key, p_val, &p_opts->dump_files_dir);

      __osm_subn_opts_unpack_charp(
        "lid_matrix_dump_file",
        p_key, p_val, &p_opts->lid_matrix_dump_file);

      __osm_subn_opts_unpack_charp( 
        "ucast_dump_file",
        p_key, p_val, &p_opts->ucast_dump_file);

      __osm_subn_opts_unpack_charp( 
        "root_guid_file",
        p_key, p_val, &p_opts->root_guid_file);

      __osm_subn_opts_unpack_charp(
        "cn_guid_file",
        p_key, p_val, &p_opts->cn_guid_file);

      __osm_subn_opts_unpack_charp(
        "sa_db_file",
        p_key, p_val, &p_opts->sa_db_file);

      __osm_subn_opts_unpack_boolean(
        "exit_on_fatal",
        p_key, p_val, &p_opts->exit_on_fatal);

      __osm_subn_opts_unpack_boolean( 
        "honor_guid2lid_file",
        p_key, p_val, &p_opts->honor_guid2lid_file);

      __osm_subn_opts_unpack_boolean(
        "daemon",
        p_key, p_val, &p_opts->daemon);

      __osm_subn_opts_unpack_boolean(
        "sm_inactive",
        p_key, p_val, &p_opts->sm_inactive);

#ifdef ENABLE_OSM_PERF_MGR
      __osm_subn_opts_unpack_boolean(
        "perfmgr",
        p_key, p_val, &p_opts->perfmgr);

      __osm_subn_opts_unpack_uint16(
        "perfmgr_sweep_time_s",
        p_key, p_val, &p_opts->perfmgr_sweep_time_s);

      __osm_subn_opts_unpack_uint32(
        "perfmgr_max_outstanding_queries",
        p_key, p_val, &p_opts->perfmgr_max_outstanding_queries);

      __osm_subn_opts_unpack_charp(
        "event_db_dump_file",
        p_key, p_val, &p_opts->event_db_dump_file);
#endif /* ENABLE_OSM_PERF_MGR */

      __osm_subn_opts_unpack_charp(
        "event_plugin_name",
        p_key, p_val, &p_opts->event_plugin_name);

      subn_parse_qos_options("qos",
        p_key, p_val, &p_opts->qos_options);

      subn_parse_qos_options("qos_ca",
        p_key, p_val, &p_opts->qos_ca_options);

      subn_parse_qos_options("qos_sw0",
        p_key, p_val, &p_opts->qos_sw0_options);

      subn_parse_qos_options("qos_swe",
        p_key, p_val, &p_opts->qos_swe_options);

      subn_parse_qos_options("qos_rtr",
        p_key, p_val, &p_opts->qos_rtr_options);

      __osm_subn_opts_unpack_boolean(
        "enable_quirks",
        p_key, p_val, &p_opts->enable_quirks);

      __osm_subn_opts_unpack_boolean(
       "no_clients_rereg",
       p_key, p_val, &p_opts->no_clients_rereg);

    }
  }
  fclose(opts_file);

  osm_subn_verify_conf_file(p_opts);

  return IB_SUCCESS;
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_subn_write_conf_file(
  IN osm_subn_opt_t* const p_opts )
{
  char *p_cache_dir = getenv("OSM_CACHE_DIR");
  char file_name[OSM_PATH_MAX];
  FILE *opts_file;
  
  /* try to open the options file from the cache dir */
  if (!p_cache_dir || !(*p_cache_dir))
    p_cache_dir = OSM_DEFAULT_CACHE_DIR;

  strcpy(file_name, p_cache_dir);
  strcat(file_name, "/opensm.opts");
  
  opts_file = fopen(file_name, "w");
  if (!opts_file)
    return IB_ERROR;

  fprintf( 
    opts_file,
    "#\n# DEVICE ATTRIBUTES OPTIONS\n#\n"
    "# The port GUID on which the OpenSM is running\n"
    "guid 0x%016" PRIx64 "\n\n"
    "# M_Key value sent to all ports qualifying all Set(PortInfo)\n"
    "m_key 0x%016" PRIx64 "\n\n"
    "# The lease period used for the M_Key on this subnet in [msec]\n"
    "m_key_lease_period %u\n\n"
    "# SM_Key value of the SM to qualify rcv SA queries as 'trusted'\n"
    "sm_key 0x%016" PRIx64 "\n\n"
    "# Subnet prefix used on this subnet\n"
    "subnet_prefix 0x%016" PRIx64 "\n\n"
    "# The LMC value used on this subnet\n"
    "lmc %u\n\n"
    "# lmc_esp0 determines whether LMC value used on subnet is used for\n"
    "#enhanced switch port 0. If TRUE, LMC value for subnet is used for\n"
    "#ESP0. Otherwise, LMC value for ESP0s is 0.\n"
    "lmc_esp0 %s\n\n"
    "# The code of maximal time a packet can live in a switch\n"
    "# The actual time is 4.096usec * 2^<packet_life_time>\n"
    "# The value 0x14 disables this mechanism\n"
    "packet_life_time 0x%02x\n\n"
    "# The number of sequential packets dropped that cause the port\n"
    "# to enter the VLStalled state. The result of setting this value to\n"
    "# zero is undefined.\n"
    "vl_stall_count 0x%02x\n\n"
    "# The number of sequential packets dropped that cause the port\n"
    "# to enter the VLStalled state. This value is for switch ports\n"
    "# driving a CA or router port. The result of setting this value\n"
    "# to zero is undefined.\n"
    "leaf_vl_stall_count 0x%02x\n\n"
    "# The code of maximal time a packet can wait at the head of\n"
    "# transmission queue. \n"
    "# The actual time is 4.096usec * 2^<head_of_queue_lifetime>\n"
    "# The value 0x14 disables this mechanism\n"          
    "head_of_queue_lifetime 0x%02x\n\n"
    "# The maximal time a packet can wait at the head of queue on \n"
    "# switch port connected to a CA or router port\n"
    "leaf_head_of_queue_lifetime 0x%02x\n\n"
    "# Limit the maximal operational VLs\n"
    "max_op_vls %u\n\n"
    "# Force switch links which are more than SDR capable to \n"
    "# operate at SDR speed\n\n"
    "force_link_speed %u\n\n"
    "# The subnet_timeout code that will be set for all the ports\n"
    "# The actual timeout is 4.096usec * 2^<subnet_timeout>\n"
    "subnet_timeout %u\n\n"
    "# Threshold of local phy errors for sending Trap 129\n"
    "local_phy_errors_threshold 0x%02x\n\n"
    "# Threshold of credits overrun errors for sending Trap 130\n"
    "overrun_errors_threshold 0x%02x\n\n",
    cl_ntoh64(p_opts->guid),
    cl_ntoh64(p_opts->m_key),
    cl_ntoh16(p_opts->m_key_lease_period),
    cl_ntoh64(p_opts->sm_key),
    cl_ntoh64(p_opts->subnet_prefix),
    p_opts->lmc,
    p_opts->lmc_esp0 ? "TRUE" : "FALSE",
    p_opts->packet_life_time,
    p_opts->vl_stall_count,
    p_opts->leaf_vl_stall_count,
    p_opts->head_of_queue_lifetime,
    p_opts->leaf_head_of_queue_lifetime,
    p_opts->max_op_vls,
    p_opts->force_link_speed,
    p_opts->subnet_timeout,
    p_opts->local_phy_errors_threshold,
    p_opts->overrun_errors_threshold
    );

  fprintf( 
    opts_file,
    "#\n# PARTITIONING OPTIONS\n#\n"
    "# Partition configuration file to be used\n"
    "partition_config_file %s\n\n" 
    "# Disable partition enforcement by switches\n"
    "no_partition_enforcement %s\n\n",
    p_opts->partition_config_file,
    p_opts->no_partition_enforcement ? "TRUE" : "FALSE");

  fprintf( 
    opts_file,
    "#\n# SWEEP OPTIONS\n#\n"
    "# The number of seconds between subnet sweeps (0 disables it)\n"
    "sweep_interval %u\n\n"
    "# If TRUE cause all lids to be reassigned\n"
    "reassign_lids %s\n\n"
    "# If TRUE ignore existing LFT entries on first sweep (default).\n"
    "# Otherwise only non minimal hop cases are modified.\n"
    "# NOTE: A standby SM clears its first sweep flag - since the\n"
    "# master SM already sweeps...\n"
    "reassign_lfts %s\n\n"
    "# If TRUE forces every sweep to be a heavy sweep\n"
    "force_heavy_sweep %s\n\n"
    "# If TRUE every trap will cause a heavy sweep.\n"
    "# NOTE: successive identical traps (>10) are suppressed\n"
    "sweep_on_trap %s\n\n",
    p_opts->sweep_interval,
    p_opts->reassign_lids ? "TRUE" : "FALSE",
    p_opts->reassign_lfts ? "TRUE" : "FALSE",
    p_opts->force_heavy_sweep ? "TRUE" : "FALSE",
    p_opts->sweep_on_trap ? "TRUE" : "FALSE"    
    );
  
  fprintf( 
    opts_file,
    "#\n# ROUTING OPTIONS\n#\n"
    "# If TRUE count switches as link subscriptions\n"
    "port_profile_switch_nodes %s\n\n",
    p_opts->port_profile_switch_nodes ? "TRUE" : "FALSE");

  if (p_opts->routing_engine_name)
    fprintf( opts_file,
             "# Routing engine\n"
             "routing_engine %s\n\n",
             p_opts->routing_engine_name);
  if (p_opts->connect_roots)
    fprintf( opts_file,
             "# Connect roots (use FALSE if unsure)\n"
             "connect_roots %s\n\n",
             p_opts->connect_roots ? "TRUE" : "FALSE");
  if (p_opts->lid_matrix_dump_file)
    fprintf( opts_file,
             "# Lid matrix dump file name\n"
             "lid_matrix_dump_file %s\n\n",
             p_opts->lid_matrix_dump_file);
  if (p_opts->ucast_dump_file)
    fprintf( opts_file,
             "# Ucast dump file name\n"
             "ucast_dump_file %s\n\n",
             p_opts->ucast_dump_file);
  if (p_opts->root_guid_file)
    fprintf( opts_file,
             "# The file holding the root node guids (for fat-tree or Up/Down)\n"
             "# One guid in each line\n"
             "root_guid_file %s\n\n",
             p_opts->root_guid_file);
  if (p_opts->cn_guid_file)
    fprintf( opts_file,
             "# The file holding the fat-tree compute node guids\n"
             "# One guid in each line\n"
             "cn_guid_file %s\n\n",
             p_opts->cn_guid_file);
  if (p_opts->sa_db_file)
    fprintf( opts_file,
             "# SA database file name\n"
             "sa_db_file %s\n\n",
             p_opts->sa_db_file);
  
  fprintf( 
    opts_file,    
    "#\n# HANDOVER - MULTIPLE SMs OPTIONS\n#\n"
    "# SM priority used for deciding who is the master\n"
    "# Range goes from 0 (lowest priority) to 15 (highest).\n"
    "sm_priority %u\n\n"
    "# If TRUE other SMs on the subnet should be ignored\n"
    "ignore_other_sm %s\n\n"
    "# Timeout in [msec] between two polls of active master SM\n"
    "sminfo_polling_timeout %u\n\n"
    "# Number of failing polls of remote SM that declares it dead\n"
    "polling_retry_number %u\n\n"
    "# If TRUE honor the guid2lid file when coming out of standby\n"
    "# state, if such file exists and is valid\n"
    "honor_guid2lid_file %s\n\n",
    p_opts->sm_priority,
    p_opts->ignore_other_sm ? "TRUE" : "FALSE",
    p_opts->sminfo_polling_timeout,
    p_opts->polling_retry_number,
    p_opts->honor_guid2lid_file ? "TRUE" : "FALSE"
    );
    
  fprintf(
    opts_file,
    "#\n# TIMING AND THREADING OPTIONS\n#\n"
    "# Number of MADs sent in parallel\n"
    "max_wire_smps %u\n\n"
    "# The time taken to a transaction to finish in [msec]\n"
    "transaction_timeout %u\n\n"
    "# Maximal time in [msec] a message can stay in the incoming message queue.\n"
    "# If there is more than one message in the queue and the last message\n"
    "# stayed in the queue more than this value, any SA request will be\n"
    "# immediately returned with a BUSY status.\n"
    "max_msg_fifo_timeout %u\n\n"
    "# Use a single thread for handling SA queries\n"
    "single_thread %s\n\n",
    p_opts->max_wire_smps,
    p_opts->transaction_timeout,
    p_opts->max_msg_fifo_timeout,
    p_opts->single_thread ? "TRUE" : "FALSE"
    );
  
  fprintf(
    opts_file,
    "#\n# MISC OPTIONS\n#\n"
    "# Daemon mode\n"
    "daemon %s\n\n"
    "# SM Inactive\n"
    "sm_inactive %s\n\n",
    p_opts->daemon ? "TRUE" : "FALSE",
    p_opts->sm_inactive ? "TRUE" : "FALSE"
    );

#ifdef ENABLE_OSM_PERF_MGR
  fprintf(
    opts_file,
    "#\n# Performance Manager Options\n#\n"
    "# perfmgr enable\n"
    "perfmgr %s\n\n"
    "# sweep time in seconds\n"
    "perfmgr_sweep_time_s %u\n\n"
    "# Max outstanding queries\n"
    "perfmgr_max_outstanding_queries %u\n\n"
    ,
    p_opts->perfmgr ? "TRUE" : "FALSE",
    p_opts->perfmgr_sweep_time_s,
    p_opts->perfmgr_max_outstanding_queries
    );

  fprintf(
    opts_file,
    "#\n# Event DB Options\n#\n"
    "# Dump file to dump the events to\n"
    "event_db_dump_file %s\n\n"
    ,
    p_opts->event_db_dump_file
    );
#endif /* ENABLE_OSM_PERF_MGR */

  fprintf(
    opts_file,
    "#\n# Event Plugin Options\n#\n"
    "event_plugin_name %s\n\n"
    ,
    p_opts->event_plugin_name
    );

  fprintf( 
    opts_file,
    "#\n# DEBUG FEATURES\n#\n"
    "# The log flags used\n"
    "log_flags 0x%02x\n\n"
    "# Force flush of the log file after each log message\n"
    "force_log_flush %s\n\n"
    "# Log file to be used\n"
    "log_file %s\n\n"
    "# Limit the size of the log file. If overrun, log is restarted\n"
    "log_max_size %lu\n\n"
    "# If TRUE will accumulate the log over multiple OpenSM sessions\n"
    "accum_log_file %s\n\n"
    "# The directory to hold the file OpenSM dumps\n"
    "dump_files_dir %s\n\n"
    "# If TRUE enables new high risk options and hardware specific quirks\n"
    "enable_quirks %s\n\n"
    "# If TRUE disables client reregistration\n"
    "no_clients_rereg %s\n\n"
    "# If TRUE OpenSM should disable multicast support\n"  
    "no_multicast_option %s\n\n"
    "# No multicast routing is performed if TRUE\n"
    "disable_multicast %s\n\n"
    "# If TRUE opensm will exit on fatal initialization issues\n"
    "exit_on_fatal %s\n\n"
    "# console [off|local"
#ifdef ENABLE_OSM_CONSOLE_SOCKET
    "|socket]\n"
#else
    "]\n"
#endif
    "console %s\n\n"
    "# Telnet port for console (default %d)\n"
    "console_port %d\n\n", 
    p_opts->log_flags,
    p_opts->force_log_flush ? "TRUE" : "FALSE",
    p_opts->log_file,
    p_opts->log_max_size,
    p_opts->accum_log_file ? "TRUE" : "FALSE",
    p_opts->dump_files_dir,
    p_opts->enable_quirks ? "TRUE" : "FALSE",
    p_opts->no_clients_rereg ? "TRUE" : "FALSE",
    p_opts->no_multicast_option ? "TRUE" : "FALSE",
    p_opts->disable_multicast ? "TRUE" : "FALSE",
    p_opts->exit_on_fatal ? "TRUE" : "FALSE",
    p_opts->console,
    OSM_DEFAULT_CONSOLE_PORT, p_opts->console_port
    );
  
  fprintf(
    opts_file,
    "#\n# QoS OPTIONS\n#\n"
    "# Disable QoS setup\n"
    "no_qos %s\n\n",
    p_opts->no_qos ? "TRUE" : "FALSE");

  subn_dump_qos_options(opts_file,
    "QoS default options", "qos", &p_opts->qos_options);
  fprintf(opts_file, "\n");
  subn_dump_qos_options(opts_file,
    "QoS CA options", "qos_ca", &p_opts->qos_ca_options);
  fprintf(opts_file, "\n");
  subn_dump_qos_options(opts_file,
    "QoS Switch Port 0 options", "qos_sw0", &p_opts->qos_sw0_options);
  fprintf(opts_file, "\n");
  subn_dump_qos_options(opts_file,
    "QoS Switch external ports options", "qos_swe", &p_opts->qos_swe_options);
  fprintf(opts_file, "\n");
  subn_dump_qos_options(opts_file,
    "QoS Router ports options", "qos_rtr", &p_opts->qos_rtr_options);

  /* optional string attributes ... */
  
  fclose(opts_file);

  return IB_SUCCESS;
}
