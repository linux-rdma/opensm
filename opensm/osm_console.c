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

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#define _GNU_SOURCE		/* for getline */
#include <stdio.h>
#include <stdlib.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#ifdef ENABLE_OSM_CONSOLE_SOCKET
#include <tcpd.h>
#endif
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/time.h>
#include <signal.h>
#include <opensm/osm_console.h>
#include <opensm/osm_version.h>
#include <complib/cl_passivelock.h>
#include <opensm/osm_perfmgr.h>

struct command {
	char *name;
	void (*help_function) (FILE * out, int detail);
	void (*parse_function) (char **p_last, osm_opensm_t * p_osm,
				FILE * out);
};

static struct {
	int on;
	int delay_s;
	time_t previous;
	void (*loop_function) (osm_opensm_t * p_osm, FILE * out);
} loop_command = {
on: 0, delay_s: 2, loop_function:NULL};

static const struct command console_cmds[];

static inline char *next_token(char **p_last)
{
	return strtok_r(NULL, " \t\n\r", p_last);
}

static void help_command(FILE * out, int detail)
{
	int i;

	fprintf(out, "Supported commands and syntax:\n");
	fprintf(out, "help [<command>]\n");
	/* skip help command */
	for (i = 1; console_cmds[i].name; i++)
		console_cmds[i].help_function(out, 0);
}

static void help_quit(FILE * out, int detail)
{
	fprintf(out, "quit (not valid in local mode; use ctl-c)\n");
}

static void help_loglevel(FILE * out, int detail)
{
	fprintf(out, "loglevel [<log-level>]\n");
	if (detail) {
		fprintf(out, "   log-level is OR'ed from the following\n");
		fprintf(out, "   OSM_LOG_NONE             0x%02X\n",
			OSM_LOG_NONE);
		fprintf(out, "   OSM_LOG_ERROR            0x%02X\n",
			OSM_LOG_ERROR);
		fprintf(out, "   OSM_LOG_INFO             0x%02X\n",
			OSM_LOG_INFO);
		fprintf(out, "   OSM_LOG_VERBOSE          0x%02X\n",
			OSM_LOG_VERBOSE);
		fprintf(out, "   OSM_LOG_DEBUG            0x%02X\n",
			OSM_LOG_DEBUG);
		fprintf(out, "   OSM_LOG_FUNCS            0x%02X\n",
			OSM_LOG_FUNCS);
		fprintf(out, "   OSM_LOG_FRAMES           0x%02X\n",
			OSM_LOG_FRAMES);
		fprintf(out, "   OSM_LOG_ROUTING          0x%02X\n",
			OSM_LOG_ROUTING);
		fprintf(out, "   OSM_LOG_SYS              0x%02X\n",
			OSM_LOG_SYS);
		fprintf(out, "\n");
		fprintf(out, "   OSM_LOG_DEFAULT_LEVEL    0x%02X\n",
			OSM_LOG_DEFAULT_LEVEL);
	}
}

static void help_priority(FILE * out, int detail)
{
	fprintf(out, "priority [<sm-priority>]\n");
}

static void help_resweep(FILE * out, int detail)
{
	fprintf(out, "resweep [heavy|light]\n");
}

static void help_status(FILE * out, int detail)
{
	fprintf(out, "status [loop]\n");
	if (detail) {
		fprintf(out, "   loop -- type \"q<ret>\" to quit\n");
	}
}

static void help_logflush(FILE * out, int detail)
{
	fprintf(out, "logflush -- flush the opensm.log file\n");
}

static void help_querylid(FILE * out, int detail)
{
	fprintf(out,
		"querylid lid -- print internal information about the lid specified\n");
}

static void help_portstatus(FILE * out, int detail)
{
	fprintf(out, "portstatus [ca|switch|router]\n");
	if (detail) {
		fprintf(out, "summarize port status\n");
		fprintf(out,
			"   [ca|switch|router] -- limit the results to the node type specified\n");
	}

}

#ifdef ENABLE_OSM_PERF_MGR
static void help_perfmgr(FILE * out, int detail)
{
	fprintf(out,
		"perfmgr [enable|disable|clear_counters|dump_counters|sweep_time[seconds]]\n");
	if (detail) {
		fprintf(out,
			"perfmgr -- print the performance manager state\n");
		fprintf(out,
			"   [enable|disable] -- change the perfmgr state\n");
		fprintf(out,
			"   [sweep_time] -- change the perfmgr sweep time (requires [seconds] option)\n");
		fprintf(out,
			"   [clear_counters] -- clear the counters stored\n");
		fprintf(out,
			"   [dump_counters [mach]] -- dump the counters (optionally in [mach]ine readable format)\n");
		fprintf(out,
			"   [print_counters <nodename|nodeguid>] -- print the counters for the specified node\n");
	}
}
#endif				/* ENABLE_OSM_PERF_MGR */

/* more help routines go here */

static void help_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	char *p_cmd;
	int i, found = 0;

	p_cmd = next_token(p_last);
	if (!p_cmd)
		help_command(out, 0);
	else {
		for (i = 1; console_cmds[i].name; i++) {
			if (!strcmp(p_cmd, console_cmds[i].name)) {
				found = 1;
				console_cmds[i].help_function(out, 1);
				break;
			}
		}
		if (!found) {
			fprintf(out, "%s : Command not found\n\n", p_cmd);
			help_command(out, 0);
		}
	}
}

static void loglevel_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	char *p_cmd;
	int level;

	p_cmd = next_token(p_last);
	if (!p_cmd)
		fprintf(out, "Current log level is 0x%x\n",
			osm_log_get_level(&p_osm->log));
	else {
		/* Handle x, 0x, and decimal specification of log level */
		if (!strncmp(p_cmd, "x", 1)) {
			p_cmd++;
			level = strtoul(p_cmd, NULL, 16);
		} else {
			if (!strncmp(p_cmd, "0x", 2)) {
				p_cmd += 2;
				level = strtoul(p_cmd, NULL, 16);
			} else
				level = strtol(p_cmd, NULL, 10);
		}
		if ((level >= 0) && (level < 256)) {
			fprintf(out, "Setting log level to 0x%x\n", level);
			osm_log_set_level(&p_osm->log, level);
		} else
			fprintf(out, "Invalid log level 0x%x\n", level);
	}
}

static void priority_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	char *p_cmd;
	int priority;

	p_cmd = next_token(p_last);
	if (!p_cmd)
		fprintf(out, "Current sm-priority is %d\n",
			p_osm->subn.opt.sm_priority);
	else {
		priority = strtol(p_cmd, NULL, 0);
		if (0 > priority || 15 < priority)
			fprintf(out,
				"Invalid sm-priority %d; must be between 0 and 15\n",
				priority);
		else {
			fprintf(out, "Setting sm-priority to %d\n", priority);
			p_osm->subn.opt.sm_priority = (uint8_t) priority;
			/* Does the SM state machine need a kick now ? */
		}
	}
}

static char *sm_state_str(int state)
{
	switch (state) {
	case IB_SMINFO_STATE_INIT:
		return ("Init");
	case IB_SMINFO_STATE_DISCOVERING:
		return ("Discovering");
	case IB_SMINFO_STATE_STANDBY:
		return ("Standby");
	case IB_SMINFO_STATE_NOTACTIVE:
		return ("Not Active");
	case IB_SMINFO_STATE_MASTER:
		return ("Master");
	}
	return ("UNKNOWN");
}

static char *sa_state_str(osm_sa_state_t state)
{
	switch (state) {
	case OSM_SA_STATE_INIT:
		return ("Init");
	case OSM_SA_STATE_READY:
		return ("Ready");
	}
	return ("UNKNOWN");
}

static char *sm_state_mgr_str(osm_sm_state_t state)
{
	switch (state) {
	case OSM_SM_STATE_NO_STATE:
		return ("No State");
	case OSM_SM_STATE_INIT:
		return ("Init");
	case OSM_SM_STATE_IDLE:
		return ("Idle");
	case OSM_SM_STATE_SWEEP_LIGHT:
		return ("Sweep Light");
	case OSM_SM_STATE_SWEEP_LIGHT_WAIT:
		return ("Sweep Light Wait");
	case OSM_SM_STATE_SWEEP_HEAVY_SELF:
		return ("Sweep Heavy Self");
	case OSM_SM_STATE_SWEEP_HEAVY_SUBNET:
		return ("Sweep Heavy Subnet");
	case OSM_SM_STATE_SET_SM_UCAST_LID:
		return ("Set SM UCAST LID");
	case OSM_SM_STATE_SET_SM_UCAST_LID_WAIT:
		return ("Set SM UCAST LID Wait");
	case OSM_SM_STATE_SET_SM_UCAST_LID_DONE:
		return ("Set SM UCAST LID Done");
	case OSM_SM_STATE_SET_SUBNET_UCAST_LIDS:
		return ("Set Subnet UCAST LIDS");
	case OSM_SM_STATE_SET_SUBNET_UCAST_LIDS_WAIT:
		return ("Set Subnet UCAST LIDS Wait");
	case OSM_SM_STATE_SET_SUBNET_UCAST_LIDS_DONE:
		return ("Set Subnet UCAST LIDS Done");
	case OSM_SM_STATE_SET_UCAST_TABLES:
		return ("Set UCAST Tables");
	case OSM_SM_STATE_SET_UCAST_TABLES_WAIT:
		return ("Set UCAST Tables Wait");
	case OSM_SM_STATE_SET_UCAST_TABLES_DONE:
		return ("Set UCAST Tables Done");
	case OSM_SM_STATE_SET_MCAST_TABLES:
		return ("Set MCAST Tables");
	case OSM_SM_STATE_SET_MCAST_TABLES_WAIT:
		return ("Set MCAST Tables Wait");
	case OSM_SM_STATE_SET_MCAST_TABLES_DONE:
		return ("Set MCAST Tables Done");
	case OSM_SM_STATE_SET_LINK_PORTS:
		return ("Set Link Ports");
	case OSM_SM_STATE_SET_LINK_PORTS_WAIT:
		return ("Set Link Ports Wait");
	case OSM_SM_STATE_SET_LINK_PORTS_DONE:
		return ("Set Link Ports Done");
	case OSM_SM_STATE_SET_ARMED:
		return ("Set Armed");
	case OSM_SM_STATE_SET_ARMED_WAIT:
		return ("Set Armed Wait");
	case OSM_SM_STATE_SET_ARMED_DONE:
		return ("Set Armed Done");
	case OSM_SM_STATE_SET_ACTIVE:
		return ("Set Active");
	case OSM_SM_STATE_SET_ACTIVE_WAIT:
		return ("Set Active Wait");
	case OSM_SM_STATE_LOST_NEGOTIATION:
		return ("Lost Negotiation");
	case OSM_SM_STATE_STANDBY:
		return ("Standby");
	case OSM_SM_STATE_SUBNET_UP:
		return ("Subnet Up");
	case OSM_SM_STATE_PROCESS_REQUEST:
		return ("Process Request");
	case OSM_SM_STATE_PROCESS_REQUEST_WAIT:
		return ("Process Request Wait");
	case OSM_SM_STATE_PROCESS_REQUEST_DONE:
		return ("Process Request Done");
	case OSM_SM_STATE_MASTER_OR_HIGHER_SM_DETECTED:
		return ("Master or Higher SM Detected");
	case OSM_SM_STATE_SET_PKEY:
		return ("Set PKey");
	case OSM_SM_STATE_SET_PKEY_WAIT:
		return ("Set PKey Wait");
	case OSM_SM_STATE_SET_PKEY_DONE:
		return ("Set PKey Done");
	default:
		return ("Unknown State");
	}
}

static void print_status(osm_opensm_t * p_osm, FILE * out)
{
	if (out) {
		cl_plock_acquire(&p_osm->lock);
		fprintf(out, "   OpenSM Version     : %s\n", OSM_VERSION);
		fprintf(out, "   SM State/Mgr State : %s/%s\n",
			sm_state_str(p_osm->subn.sm_state),
			sm_state_mgr_str(p_osm->sm.state_mgr.state));
		fprintf(out, "   SA State           : %s\n",
			sa_state_str(p_osm->sa.state));
		fprintf(out, "   Routing Engine     : %s\n",
			osm_routing_engine_type_str(p_osm->
						    routing_engine_used));
#ifdef ENABLE_OSM_PERF_MGR
		fprintf(out, "\n   PerfMgr state/sweep state : %s/%s\n",
			osm_perfmgr_get_state_str(&(p_osm->perfmgr)),
			osm_perfmgr_get_sweep_state_str(&(p_osm->perfmgr)));
#endif
		fprintf(out, "\n   MAD stats\n"
			"   ---------\n"
			"   QP0 MADs outstanding           : %d\n"
			"   QP0 MADs outstanding (on wire) : %d\n"
			"   QP0 MADs rcvd                  : %d\n"
			"   QP0 MADs sent                  : %d\n"
			"   QP0 unicasts sent              : %d\n"
			"   QP0 unknown MADs rcvd          : %d\n"
			"   SA MADs outstanding            : %d\n"
			"   SA MADs rcvd                   : %d\n"
			"   SA MADs sent                   : %d\n"
			"   SA unknown MADs rcvd           : %d\n"
			"   SA MADs ignored                : %d\n",
			p_osm->stats.qp0_mads_outstanding,
			p_osm->stats.qp0_mads_outstanding_on_wire,
			p_osm->stats.qp0_mads_rcvd,
			p_osm->stats.qp0_mads_sent,
			p_osm->stats.qp0_unicasts_sent,
			p_osm->stats.qp0_mads_rcvd_unknown,
			p_osm->stats.sa_mads_outstanding,
			p_osm->stats.sa_mads_rcvd,
			p_osm->stats.sa_mads_sent,
			p_osm->stats.sa_mads_rcvd_unknown,
			p_osm->stats.sa_mads_ignored);
		fprintf(out, "\n   Subnet flags\n"
			"   ------------\n"
			"   Ignore existing lfts           : %d\n"
			"   Subnet Init errors             : %d\n"
			"   In sweep hop 0                 : %d\n"
			"   Moved to master state          : %d\n"
			"   First time master sweep        : %d\n"
			"   Coming out of standby          : %d\n",
			p_osm->subn.ignore_existing_lfts,
			p_osm->subn.subnet_initialization_error,
			p_osm->subn.in_sweep_hop_0,
			p_osm->subn.moved_to_master_state,
			p_osm->subn.first_time_master_sweep,
			p_osm->subn.coming_out_of_standby);
		fprintf(out, "\n");
		cl_plock_release(&p_osm->lock);
	}
}

static int loop_command_check_time(void)
{
	time_t cur = time(NULL);
	if ((loop_command.previous + loop_command.delay_s) < cur) {
		loop_command.previous = cur;
		return (1);
	}
	return (0);
}

static void status_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	char *p_cmd;

	p_cmd = next_token(p_last);
	if (p_cmd) {
		if (strcmp(p_cmd, "loop") == 0) {
			fprintf(out, "Looping on status command...\n");
			fflush(out);
			loop_command.on = 1;
			loop_command.previous = time(NULL);
			loop_command.loop_function = print_status;
		} else {
			help_status(out, 1);
			return;
		}
	}
	print_status(p_osm, out);
}

static void resweep_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	char *p_cmd;

	p_cmd = next_token(p_last);
	if (!p_cmd ||
	    (strcmp(p_cmd, "heavy") != 0 && strcmp(p_cmd, "light") != 0)) {
		fprintf(out, "Invalid resweep command\n");
		help_resweep(out, 1);
	} else {
		if (strcmp(p_cmd, "heavy") == 0)
			p_osm->subn.force_heavy_sweep = TRUE;
		osm_opensm_sweep(p_osm);
	}
}

static void logflush_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	fflush(p_osm->log.out_port);
}

static void querylid_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	int p = 0;
	uint16_t lid = 0;
	osm_port_t *p_port = NULL;
	char *p_cmd = next_token(p_last);

	if (!p_cmd) {
		fprintf(out, "no LID specified\n");
		help_querylid(out, 1);
		return;
	}

	lid = (uint16_t) strtoul(p_cmd, NULL, 0);
	cl_plock_acquire(&p_osm->lock);
	if (lid > cl_ptr_vector_get_capacity(&(p_osm->subn.port_lid_tbl)))
		goto invalid_lid;
	p_port = cl_ptr_vector_get(&(p_osm->subn.port_lid_tbl), lid);
	if (!p_port)
		goto invalid_lid;

	fprintf(out, "Query results for LID %d\n", lid);
	fprintf(out,
		"   GUID                : 0x%016" PRIx64 "\n"
		"   Node Desc           : %s\n"
		"   Node Type           : %s\n"
		"   Num Ports           : %d\n",
		cl_ntoh64(p_port->guid),
		p_port->p_node->print_desc,
		ib_get_node_type_str(osm_node_get_type(p_port->p_node)),
		p_port->p_node->node_info.num_ports);

	if (p_port->p_node->sw)
		p = 0;
	else
		p = 1;
	for ( /* see above */ ; p < p_port->p_node->physp_tbl_size; p++) {
		fprintf(out,
			"   Port %d health       : %s\n",
			p,
			p_port->p_node->physp_table[p].
			healthy ? "OK" : "ERROR");
	}

	cl_plock_release(&p_osm->lock);
	return;

      invalid_lid:
	cl_plock_release(&p_osm->lock);
	fprintf(out, "Invalid lid %d\n", lid);
	return;
}

/**
 * Data structures for the portstatus command
 */
typedef struct _port_report {
	struct _port_report *next;
	uint64_t node_guid;
	uint8_t port_num;
	char print_desc[IB_NODE_DESCRIPTION_SIZE + 1];
} port_report_t;

static void
__tag_port_report(port_report_t ** head, uint64_t node_guid,
		  uint8_t port_num, char *print_desc)
{
	port_report_t *rep = malloc(sizeof(*rep));
	if (!rep)
		return;

	rep->node_guid = node_guid;
	rep->port_num = port_num;
	memcpy(rep->print_desc, print_desc, IB_NODE_DESCRIPTION_SIZE + 1);
	rep->next = NULL;
	if (*head) {
		rep->next = *head;
		*head = rep;
	} else
		*head = rep;
}

static void __print_port_report(FILE * out, port_report_t * head)
{
	port_report_t *item = head;
	while (item != NULL) {
		fprintf(out, "      0x%016" PRIx64 " %d (%s)\n",
			item->node_guid, item->port_num, item->print_desc);
		port_report_t *next = item->next;
		free(item);
		item = next;
	}
}

typedef struct {
	uint8_t node_type_lim;	/* limit the results; 0 == ALL */
	uint64_t total_nodes;
	uint64_t total_ports;
	uint64_t ports_down;
	uint64_t ports_active;
	uint64_t ports_disabled;
	port_report_t *disabled_ports;
	uint64_t ports_1X;
	uint64_t ports_4X;
	uint64_t ports_8X;
	uint64_t ports_12X;
	uint64_t ports_unknown_width;
	uint64_t ports_reduced_width;
	port_report_t *reduced_width_ports;
	uint64_t ports_sdr;
	uint64_t ports_ddr;
	uint64_t ports_qdr;
	uint64_t ports_unknown_speed;
	uint64_t ports_reduced_speed;
	port_report_t *reduced_speed_ports;
} fabric_stats_t;

/**
 * iterator function to get portstatus on each node
 */
static void __get_stats(cl_map_item_t * const p_map_item, void *context)
{
	fabric_stats_t *fs = (fabric_stats_t *) context;
	osm_node_t *node = (osm_node_t *) p_map_item;
	uint8_t num_ports = osm_node_get_num_physp(node);
	uint8_t port = 0;

	/* Skip nodes we are not interested in */
	if (fs->node_type_lim != 0
	    && fs->node_type_lim != node->node_info.node_type)
		return;

	fs->total_nodes++;

	for (port = 1; port < num_ports; port++) {
		osm_physp_t *phys = osm_node_get_physp_ptr(node, port);
		ib_port_info_t *pi = &(phys->port_info);
		uint8_t active_speed = ib_port_info_get_link_speed_active(pi);
		uint8_t enabled_speed = ib_port_info_get_link_speed_enabled(pi);
		uint8_t active_width = pi->link_width_active;
		uint8_t enabled_width = pi->link_width_enabled;
		uint8_t port_state = ib_port_info_get_port_state(pi);
		uint8_t port_phys_state = ib_port_info_get_port_phys_state(pi);

		if (!phys)
			continue;

		if ((enabled_width ^ active_width) > active_width) {
			__tag_port_report(&(fs->reduced_width_ports),
					  cl_ntoh64(node->node_info.node_guid),
					  port, node->print_desc);
			fs->ports_reduced_width++;
		}

		if ((enabled_speed ^ active_speed) > active_speed) {
			__tag_port_report(&(fs->reduced_speed_ports),
					  cl_ntoh64(node->node_info.node_guid),
					  port, node->print_desc);
			fs->ports_reduced_speed++;
		}

		switch (active_speed) {
		case IB_LINK_SPEED_ACTIVE_2_5:
			fs->ports_sdr++;
			break;
		case IB_LINK_SPEED_ACTIVE_5:
			fs->ports_ddr++;
			break;
		case IB_LINK_SPEED_ACTIVE_10:
			fs->ports_qdr++;
			break;
		default:
			fs->ports_unknown_speed++;
			break;
		}
		switch (active_width) {
		case IB_LINK_WIDTH_ACTIVE_1X:
			fs->ports_1X++;
			break;
		case IB_LINK_WIDTH_ACTIVE_4X:
			fs->ports_4X++;
			break;
		case IB_LINK_WIDTH_ACTIVE_8X:
			fs->ports_8X++;
			break;
		case IB_LINK_WIDTH_ACTIVE_12X:
			fs->ports_12X++;
			break;
		default:
			fs->ports_unknown_width++;
			break;
		}
		if (port_state == IB_LINK_DOWN)
			fs->ports_down++;
		else if (port_state == IB_LINK_ACTIVE)
			fs->ports_active++;
		if (port_phys_state == IB_PORT_PHYS_STATE_DISABLED) {
			__tag_port_report(&(fs->disabled_ports),
					  cl_ntoh64(node->node_info.node_guid),
					  port, node->print_desc);
			fs->ports_disabled++;
		}

		fs->total_ports++;
	}
}

static void portstatus_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	fabric_stats_t fs;
	struct timeval before, after;
	char *p_cmd;

	memset(&fs, 0, sizeof(fs));

	p_cmd = next_token(p_last);
	if (p_cmd) {
		if (strcmp(p_cmd, "ca") == 0) {
			fs.node_type_lim = IB_NODE_TYPE_CA;
		} else if (strcmp(p_cmd, "switch") == 0) {
			fs.node_type_lim = IB_NODE_TYPE_SWITCH;
		} else if (strcmp(p_cmd, "router") == 0) {
			fs.node_type_lim = IB_NODE_TYPE_ROUTER;
		} else {
			fprintf(out, "Node type not understood\n");
			help_portstatus(out, 1);
			return;
		}
	}

	gettimeofday(&before, NULL);

	/* for each node in the system gather the stats */
	cl_plock_acquire(&p_osm->lock);
	cl_qmap_apply_func(&(p_osm->subn.node_guid_tbl), __get_stats,
			   (void *)&fs);
	cl_plock_release(&p_osm->lock);

	gettimeofday(&after, NULL);

	/* report the stats */
	fprintf(out, "\"%s\" port status:\n",
		fs.node_type_lim ? ib_get_node_type_str(fs.
							node_type_lim) : "ALL");
	fprintf(out,
		"   %" PRIu64 " port(s) scanned on %" PRIu64
		" nodes in %lu us\n", fs.total_ports, fs.total_nodes,
		after.tv_usec - before.tv_usec);

	if (fs.ports_down)
		fprintf(out, "   %" PRIu64 " down\n", fs.ports_down);
	if (fs.ports_active)
		fprintf(out, "   %" PRIu64 " active\n", fs.ports_active);
	if (fs.ports_1X)
		fprintf(out, "   %" PRIu64 " at 1X\n", fs.ports_1X);
	if (fs.ports_4X)
		fprintf(out, "   %" PRIu64 " at 4X\n", fs.ports_4X);
	if (fs.ports_8X)
		fprintf(out, "   %" PRIu64 " at 8X\n", fs.ports_8X);
	if (fs.ports_12X)
		fprintf(out, "   %" PRIu64 " at 12X\n", fs.ports_12X);

	if (fs.ports_sdr)
		fprintf(out, "   %" PRIu64 " at 2.5 Gbps\n", fs.ports_sdr);
	if (fs.ports_ddr)
		fprintf(out, "   %" PRIu64 " at 5.0 Gbps\n", fs.ports_ddr);
	if (fs.ports_qdr)
		fprintf(out, "   %" PRIu64 " at 10.0 Gbps\n", fs.ports_qdr);

	if (fs.ports_disabled + fs.ports_reduced_speed + fs.ports_reduced_width
	    > 0) {
		fprintf(out, "\nPossible issues:\n");
	}
	if (fs.ports_disabled) {
		fprintf(out, "   %" PRIu64 " disabled\n", fs.ports_disabled);
		__print_port_report(out, fs.disabled_ports);
	}
	if (fs.ports_reduced_speed) {
		fprintf(out, "   %" PRIu64 " with reduced speed\n",
			fs.ports_reduced_speed);
		__print_port_report(out, fs.reduced_speed_ports);
	}
	if (fs.ports_reduced_width) {
		fprintf(out, "   %" PRIu64 " with reduced width\n",
			fs.ports_reduced_width);
		__print_port_report(out, fs.reduced_width_ports);
	}
	fprintf(out, "\n");
}

#ifdef ENABLE_OSM_PERF_MGR
static void perfmgr_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	char *p_cmd;

	p_cmd = next_token(p_last);
	if (p_cmd) {
		if (strcmp(p_cmd, "enable") == 0) {
			osm_perfmgr_set_state(&(p_osm->perfmgr),
					      PERFMGR_STATE_ENABLED);
		} else if (strcmp(p_cmd, "disable") == 0) {
			osm_perfmgr_set_state(&(p_osm->perfmgr),
					      PERFMGR_STATE_DISABLE);
		} else if (strcmp(p_cmd, "clear_counters") == 0) {
			osm_perfmgr_clear_counters(&(p_osm->perfmgr));
		} else if (strcmp(p_cmd, "dump_counters") == 0) {
			p_cmd = next_token(p_last);
			if (p_cmd && (strcmp(p_cmd, "mach") == 0)) {
				osm_perfmgr_dump_counters(&(p_osm->perfmgr),
							  PERFMGR_EVENT_DB_DUMP_MR);
			} else {
				osm_perfmgr_dump_counters(&(p_osm->perfmgr),
							  PERFMGR_EVENT_DB_DUMP_HR);
			}
		} else if (strcmp(p_cmd, "print_counters") == 0) {
			p_cmd = next_token(p_last);
			if (p_cmd) {
				osm_perfmgr_print_counters(&(p_osm->perfmgr), p_cmd, out);
			} else {
				fprintf(out,
					"print_counters requires a node name to be specified\n");
			}
		} else if (strcmp(p_cmd, "sweep_time") == 0) {
			p_cmd = next_token(p_last);
			if (p_cmd) {
				uint16_t time_s = atoi(p_cmd);
				osm_perfmgr_set_sweep_time_s(&(p_osm->perfmgr),
							     time_s);
			} else {
				fprintf(out,
					"sweep_time requires a time period (in seconds) to be specified\n");
			}
		} else {
			fprintf(out, "\"%s\" option not found\n", p_cmd);
		}
	} else {
		fprintf(out, "Performance Manager status:\n"
			"state                   : %s\n"
			"sweep state             : %s\n"
			"sweep time              : %us\n"
			"outstanding queries/max : %d/%u\n"
			"loaded event plugin     : %s\n",
			osm_perfmgr_get_state_str(&(p_osm->perfmgr)),
			osm_perfmgr_get_sweep_state_str(&(p_osm->perfmgr)),
			osm_perfmgr_get_sweep_time_s(&(p_osm->perfmgr)),
			p_osm->perfmgr.outstanding_queries,
			p_osm->perfmgr.max_outstanding_queries,
			p_osm->perfmgr.event_plugin ?
			p_osm->perfmgr.event_plugin->plugin_name : "NONE");
	}
}
#endif				/* ENABLE_OSM_PERF_MGR */

/* This is public to be able to close it on exit */
void osm_console_close_socket(osm_opensm_t * p_osm)
{
	if (p_osm->console.socket > 0) {
		close(p_osm->console.in_fd);
		p_osm->console.in_fd = -1;
		p_osm->console.out_fd = -1;
		p_osm->console.in = NULL;
		p_osm->console.out = NULL;
	}
}

static void quit_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	osm_console_close_socket(p_osm);
}

static void help_version(FILE * out, int detail)
{
	fprintf(out, "version -- print the OSM version\n");
}

static void version_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	fprintf(out, "%s build %s %s\n", OSM_VERSION, __DATE__, __TIME__);
}

/* more parse routines go here */

static const struct command console_cmds[] = {
	{"help", &help_command, &help_parse},
	{"quit", &help_quit, &quit_parse},
	{"loglevel", &help_loglevel, &loglevel_parse},
	{"priority", &help_priority, &priority_parse},
	{"resweep", &help_resweep, &resweep_parse},
	{"status", &help_status, &status_parse},
	{"logflush", &help_logflush, &logflush_parse},
	{"querylid", &help_querylid, &querylid_parse},
	{"portstatus", &help_portstatus, &portstatus_parse},
	{"version", &help_version, &version_parse},
#ifdef ENABLE_OSM_PERF_MGR
	{"perfmgr", &help_perfmgr, &perfmgr_parse},
#endif				/* ENABLE_OSM_PERF_MGR */
	{NULL, NULL, NULL}	/* end of array */
};

static void parse_cmd_line(char *line, osm_opensm_t * p_osm)
{
	char *p_cmd, *p_last;
	int i, found = 0;
	FILE *out = p_osm->console.out;

	while (isspace(*line))
		line++;
	if (!*line)
		return;

	/* find first token which is the command */
	p_cmd = strtok_r(line, " \t\n\r", &p_last);
	if (p_cmd) {
		for (i = 0; console_cmds[i].name; i++) {
			if (loop_command.on) {
				if (!strcmp(p_cmd, "q")) {
					loop_command.on = 0;
				}
				found = 1;
				break;
			}
			if (!strcmp(p_cmd, console_cmds[i].name)) {
				found = 1;
				console_cmds[i].parse_function(&p_last, p_osm,
							       out);
				break;
			}
		}
		if (!found) {
			fprintf(out, "%s : Command not found\n\n", p_cmd);
			help_command(out, 0);
		}
	} else {
		fprintf(out, "Error parsing command line: `%s'\n", line);
	}
	if (loop_command.on) {
		fprintf(out, "use \"q<ret>\" to quit loop\n");
		fflush(out);
	}
}

void osm_console_prompt(FILE * out)
{
	if (out) {
		fprintf(out, "OpenSM %s", OSM_COMMAND_PROMPT);
		fflush(out);
	}
}

void osm_console_init(osm_subn_opt_t * opt, osm_opensm_t * p_osm)
{
	p_osm->console.socket = -1;
	/* set up the file descriptors for the console */
	if (strcmp(opt->console, OSM_LOCAL_CONSOLE) == 0) {
		p_osm->console.in = stdin;
		p_osm->console.out = stdout;
		p_osm->console.in_fd = fileno(stdin);
		p_osm->console.out_fd = fileno(stdout);

		osm_console_prompt(p_osm->console.out);
#ifdef ENABLE_OSM_CONSOLE_SOCKET
	} else if (strcmp(opt->console, OSM_REMOTE_CONSOLE) == 0
		   || strcmp(opt->console, OSM_LOOPBACK_CONSOLE) == 0) {
		struct sockaddr_in sin;
		int optval = 1;

		if ((p_osm->console.socket =
		     socket(AF_INET, SOCK_STREAM, 0)) < 0) {
			osm_log(&(p_osm->log), OSM_LOG_ERROR,
				"osm_console_init: ERR 4B01: Failed to open console socket: %s\n",
				strerror(errno));
			return;
		}
		setsockopt(p_osm->console.socket, SOL_SOCKET, SO_REUSEADDR,
			   &optval, sizeof(optval));
		sin.sin_family = AF_INET;
		sin.sin_port = htons(opt->console_port);
		if (strcmp(opt->console, OSM_REMOTE_CONSOLE) == 0)
			sin.sin_addr.s_addr = htonl(INADDR_ANY);
		else
			sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		if (bind(p_osm->console.socket, &sin, sizeof(sin)) < 0) {
			osm_log(&(p_osm->log), OSM_LOG_ERROR,
				"osm_console_init: ERR 4B02: Failed to bind console socket: %s\n",
				strerror(errno));
			return;
		}
		if (listen(p_osm->console.socket, 1) < 0) {
			osm_log(&(p_osm->log), OSM_LOG_ERROR,
				"osm_console_init: ERR 4B03: Failed to listen on socket: %s\n",
				strerror(errno));
			return;
		}

		signal(SIGPIPE, SIG_IGN);	/* protect ourselves from closed pipes */
		p_osm->console.in = NULL;
		p_osm->console.out = NULL;
		p_osm->console.in_fd = -1;
		p_osm->console.out_fd = -1;
		osm_log(&(p_osm->log), OSM_LOG_INFO,
			"osm_console_init: Console listening on port %d\n",
			opt->console_port);
#endif
	}
}

#ifdef ENABLE_OSM_CONSOLE_SOCKET
static void handle_osm_connection(osm_opensm_t * p_osm, int new_fd,
				  char *client_ip, char *client_hn)
{
	char *p_line;
	size_t len;
	ssize_t n;

	if (p_osm->console.in_fd >= 0) {
		FILE *file = fdopen(new_fd, "w+");

		fprintf(file, "OpenSM Console connection already in use\n"
			"   kill other session (y/n)? ");
		fflush(file);
		p_line = NULL;
		n = getline(&p_line, &len, file);
		if (n > 0 && (p_line[0] == 'y' || p_line[0] == 'Y')) {
			osm_console_close_socket(p_osm);
		} else {
			close(new_fd);
			return;
		}
	}
	p_osm->console.in_fd = new_fd;
	p_osm->console.out_fd = p_osm->console.in_fd;
	p_osm->console.in = fdopen(p_osm->console.in_fd, "w+");
	p_osm->console.out = p_osm->console.in;
	osm_console_prompt(p_osm->console.out);
	osm_log(&(p_osm->log), OSM_LOG_INFO,
		"osm_console_init: Console connection accepted: %s (%s)\n",
		client_hn, client_ip);
}

static int connection_ok(char *client_ip, char *client_hn)
{
	return (hosts_ctl
		(OSM_DAEMON_NAME, client_hn, client_ip, "STRING_UNKNOWN"));
}
#endif

void osm_console(osm_opensm_t * p_osm)
{
	struct pollfd pollfd[2];
	char *p_line;
	size_t len;
	ssize_t n;
	struct pollfd *fds;
	nfds_t nfds;

	pollfd[0].fd = p_osm->console.socket;
	pollfd[0].events = POLLIN;
	pollfd[0].revents = 0;

	pollfd[1].fd = p_osm->console.in_fd;
	pollfd[1].events = POLLIN;
	pollfd[1].revents = 0;

	fds = p_osm->console.socket < 0 ? &pollfd[1] : pollfd;
	nfds = p_osm->console.socket < 0 || pollfd[1].fd < 0 ? 1 : 2;

	if (loop_command.on && loop_command_check_time() &&
	    loop_command.loop_function) {
		if (p_osm->console.out) {
			loop_command.loop_function(p_osm, p_osm->console.out);
			fflush(p_osm->console.out);
		} else {
			loop_command.on = 0;
		}
	}

	if (poll(fds, nfds, 1000) <= 0)
		return;

#ifdef ENABLE_OSM_CONSOLE_SOCKET
	if (pollfd[0].revents & POLLIN) {
		int new_fd = 0;
		struct sockaddr_in sin;
		socklen_t len = sizeof(sin);
		char client_ip[64];
		char client_hn[128];
		struct hostent *hent;
		if ((new_fd = accept(p_osm->console.socket, &sin, &len)) < 0) {
			osm_log(&(p_osm->log), OSM_LOG_ERROR,
				"osm_console: ERR 4B04: Failed to accept console socket: %s\n",
				strerror(errno));
			p_osm->console.in_fd = -1;
			return;
		}
		if (inet_ntop
		    (AF_INET, &sin.sin_addr, client_ip,
		     sizeof(client_ip)) == NULL) {
			snprintf(client_ip, 64, "STRING_UNKNOWN");
		}
		if ((hent = gethostbyaddr((const char *)&sin.sin_addr,
					  sizeof(struct in_addr),
					  AF_INET)) == NULL) {
			snprintf(client_hn, 128, "STRING_UNKNOWN");
		} else {
			snprintf(client_hn, 128, "%s", hent->h_name);
		}
		if (connection_ok(client_ip, client_hn)) {
			handle_osm_connection(p_osm, new_fd, client_ip,
					      client_hn);
		} else {
			osm_log(&(p_osm->log), OSM_LOG_ERROR,
				"osm_console: ERR 4B05: Console connection denied: %s (%s)\n",
				client_hn, client_ip);
			close(new_fd);
		}
		return;
	}
#endif

	if (pollfd[1].revents & POLLIN) {
		p_line = NULL;
		/* Get input line */
		n = getline(&p_line, &len, p_osm->console.in);
		if (n > 0) {
			/* Parse and act on input */
			parse_cmd_line(p_line, p_osm);
			if (!loop_command.on) {
				osm_console_prompt(p_osm->console.out);
			}
		} else
			osm_console_close_socket(p_osm);
		if (p_line)
			free(p_line);
	}
}
