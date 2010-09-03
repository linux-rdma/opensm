/*
 * Copyright 2009 Sandia Corporation.  Under the terms of Contract
 * DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
 * certain rights in this software.
 *
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

/* for getline() in stdio.h */
#define _GNU_SOURCE

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <opensm/osm_log.h>
#include <opensm/osm_port.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_node.h>
#include <opensm/osm_opensm.h>

#define TORUS_MAX_DIM        3
#define PORTGRP_MAX_PORTS    16
#define SWITCH_MAX_PORTGRPS  (1 + 2 * TORUS_MAX_DIM)

typedef ib_net64_t guid_t;
#define ntohllu(v_64bit) ((unsigned long long)cl_ntoh64(v_64bit))


/*
 * An endpoint terminates a link, and is one of three types:
 *   UNKNOWN  - Uninitialized endpoint.
 *   SRCSINK  - generates or consumes traffic, and thus has an associated LID;
 *		  i.e. a CA or router port.
 *   PASSTHRU - Has no associated LID; i.e. a switch port.
 *
 * If it is possible to communicate in-band with a switch, it will require
 * a port with a GUID in the switch to source/sink that traffic, but there
 * will be no attached link.  This code assumes there is only one such port.
 *
 * Here is an endpoint taxonomy:
 *
 *   type == SRCSINK
 *   link == pointer to a valid struct link
 *     ==> This endpoint is a CA or router port connected via a link to
 *	     either a switch or another CA/router.  Thus:
 *	   n_id ==> identifies the CA/router node GUID
 *	   sw   ==> NULL
 *	   port ==> identifies the port on the CA/router this endpoint uses
 *	   pgrp ==> NULL
 *
 *   type == SRCSINK
 *   link == NULL pointer
 *     ==> This endpoint is the switch port used for in-band communication
 *	     with the switch itself.  Thus:
 *	   n_id ==> identifies the node GUID used to talk to the switch
 *		      containing this endpoint
 *	   sw   ==> pointer to valid struct switch containing this endpoint
 *	   port ==> identifies the port on the switch this endpoint uses
 *	   pgrp ==> NULL, or pointer to the valid struct port_grp holding
 *		      the port in a t_switch.
 *
 *   type == PASSTHRU
 *   link == pointer to valid struct link
 *     ==> This endpoint is a switch port connected via a link to either
 *	     another switch or a CA/router.  Thus:
 *	   n_id ==> identifies the node GUID used to talk to the switch
 *		      containing this endpoint - since each switch is assumed
 *		      to have only one in-band communication port, this is a
 *		      convenient unique name for the switch itself.
 *	   sw   ==> pointer to valid struct switch containing this endpoint,
 *		      or NULL, in the case of a fabric link that has been
 *		      disconnected after being transferred to a torus link.
 *	   port ==> identifies the port on the switch this endpoint uses.
 *		      Note that in the special case of the coordinate direction
 *		      links, the port value is -1, as those links aren't
 *		      really connected to anything.
 *	   pgrp ==> NULL, or pointer to the valid struct port_grp holding
 *		      the port in a t_switch.
 */
enum endpt_type { UNKNOWN = 0, SRCSINK, PASSTHRU };
struct torus;
struct t_switch;
struct port_grp;

struct endpoint {
	enum endpt_type type;
	int port;
	guid_t n_id;		/* IBA node GUID */
	void *sw;		/* void* can point to either switch type */
	struct link *link;
	struct port_grp *pgrp;
	void *tmp;
	/*
	 * Note: osm_port is only guaranteed to contain a valid pointer
	 * when the call stack contains torus_build_lfts() or
	 * osm_port_relink_endpoint().
	 *
	 * Otherwise, the opensm core could have deleted an osm_port object
	 * without notifying us, invalidating the pointer we hold.
	 *
	 * When presented with a pointer to an osm_port_t, it is generally
	 * safe and required to cast osm_port_t:priv to struct endpoint, and
	 * check that the endpoint's osm_port is the same as the original
	 * osm_port_t pointer.  Failure to do so means that invalidated
	 * pointers will go undetected.
	 */
	struct osm_port *osm_port;
};

struct link {
	struct endpoint end[2];
};

/*
 * A port group is a collection of endpoints on a switch that share certain
 * characteristics.  All the endpoints in a port group must have the same
 * type.  Furthermore, if that type is PASSTHRU, then the connected links:
 *   1) are parallel to a given coordinate direction
 *   2) share the same two switches as endpoints.
 *
 * Torus-2QoS uses one master spanning tree for multicast, of which every
 * multicast group spanning tree is a subtree.  to_stree_root is a pointer
 * to the next port_grp on the path to the master spanning tree root.
 * to_stree_tip is a pointer to the next port_grp on the path to a master
 * spanning tree branch tip.
 *
 * Each t_switch can have at most one port_grp with a non-NULL to_stree_root.
 * Exactly one t_switch in the fabric will have all port_grp objects with
 * to_stree_root NULL; it is the master spanning tree root.
 *
 * A t_switch with all port_grp objects where to_stree_tip is NULL is at a
 * master spanning tree branch tip.
 */
struct port_grp {
	enum endpt_type type;
	size_t port_cnt;	/* number of attached ports in group
				 */
	size_t port_grp;	/* what switch port_grp we're in */
	unsigned sw_dlid_cnt;	/* switch dlids routed through this group */
	unsigned ca_dlid_cnt;	/* CA dlids routed through this group */
	struct t_switch *sw;	/* what switch we're attached to */
	struct port_grp *to_stree_root;
	struct port_grp *to_stree_tip;
	struct endpoint **port;
};

/*
 * A struct t_switch is used to represent a switch as placed in a torus.
 *
 * A t_switch used to build an N-dimensional torus will have 2N+1 port groups,
 * used as follows, assuming 0 <= d < N:
 *   port_grp[2d]   => links leaving in negative direction for coordinate d
 *   port_grp[2d+1] => links leaving in positive direction for coordinate d
 *   port_grp[2N]   => endpoints local to switch; i.e., hosts on switch
 *
 * struct link objects referenced by a t_switch are assumed to be oriented:
 * traversing a link from link.end[0] to link.end[1] is always in the positive
 * coordinate direction.
 */
struct t_switch {
	guid_t n_id;		/* IBA node GUID */
	int i, j, k;
	unsigned port_cnt;	/* including management port */
	struct torus *torus;
	void *tmp;
	/*
	 * Note: osm_switch is only guaranteed to contain a valid pointer
	 * when the call stack contains torus_build_lfts().
	 *
	 * Otherwise, the opensm core could have deleted an osm_switch object
	 * without notifying us, invalidating the pointer we hold.
	 *
	 * When presented with a pointer to an osm_switch_t, it is generally
	 * safe and required to cast osm_switch_t:priv to struct t_switch, and
	 * check that the switch's osm_switch is the same as the original
	 * osm_switch_t pointer.  Failure to do so means that invalidated
	 * pointers will go undetected.
	 */
	struct osm_switch *osm_switch;

	struct port_grp ptgrp[SWITCH_MAX_PORTGRPS];
	struct endpoint **port;
};

/*
 * We'd like to be able to discover the torus topology in a pile of switch
 * links if we can.  We'll use a struct f_switch to store raw topology for a
 * fabric description, then contruct the torus topology from struct t_switch
 * objects as we process the fabric and recover it.
 */
struct f_switch {
	guid_t n_id;		/* IBA node GUID */
	unsigned port_cnt;	/* including management port */
	void *tmp;
	/*
	 * Same rules apply here as for a struct t_switch member osm_switch.
	 */
	struct osm_switch *osm_switch;
	struct endpoint **port;
};

struct fabric {
	osm_opensm_t *osm;
	unsigned ca_cnt;
	unsigned link_cnt;
	unsigned switch_cnt;

	unsigned link_cnt_max;
	unsigned switch_cnt_max;

	struct link **link;
	struct f_switch **sw;
};

struct coord_dirs {
	/*
	 * These links define the coordinate directions for the torus.
	 * They are duplicates of links connected to switches.  Each of
	 * these links must connect to a common switch.
	 *
	 * In the event that a failed switch was specified as one of these
	 * link endpoints, our algorithm would not be able to find the
	 * torus in the fabric.  So, we'll allow multiple instances of
	 * this in the config file to allow improved resiliency.
	 */
	struct link xm_link, ym_link, zm_link;
	struct link xp_link, yp_link, zp_link;
	/*
	 * A torus dimension has coordinate values 0, 1, ..., radix - 1.
	 * The dateline, where we need to change VLs to avoid credit loops,
	 * for a torus dimension is always between coordinate values
	 * radix - 1 and 0.  The following specify the dateline location
	 * relative to the coordinate links shared switch location.
	 *
	 * E.g. if the shared switch is at 0,0,0, the following are all
	 * zero; if the shared switch is at 1,1,1, the following are all
	 * -1, etc.
	 *
	 * Since our SL/VL assignment for a path depends on the position
	 * of the path endpoints relative to the torus datelines, we need
	 * this information to keep SL/VL assignment constant in the event
	 * one of the switches used to specify coordinate directions fails.
	 */
	int x_dateline, y_dateline, z_dateline;
};

struct torus {
	osm_opensm_t *osm;
	unsigned ca_cnt;
	unsigned link_cnt;
	unsigned switch_cnt;
	unsigned seed_cnt, seed_idx;
	unsigned x_sz, y_sz, z_sz;

	unsigned sw_pool_sz;
	unsigned link_pool_sz;
	unsigned seed_sz;
	unsigned portgrp_sz;	/* max ports for port groups in this torus */

	struct fabric *fabric;
	struct t_switch **sw_pool;
	struct link *link_pool;

	struct coord_dirs *seed;
	struct t_switch ****sw;
	struct t_switch *master_stree_root;

	unsigned flags;
	int debug;
};

/*
 * Bits to use in torus.flags
 */
#define X_MESH (1U << 0)
#define Y_MESH (1U << 1)
#define Z_MESH (1U << 2)
#define MSG_DEADLOCK (1U << 29)
#define NOTIFY_CHANGES (1U << 30)

#define ALL_MESH(flags) \
	((flags & (X_MESH | Y_MESH | Z_MESH)) == (X_MESH | Y_MESH | Z_MESH))


struct torus_context {
	osm_opensm_t *osm;
	struct torus *torus;
	struct fabric fabric;
};

static
void teardown_fabric(struct fabric *f)
{
	unsigned l, p, s;
	struct endpoint *port;
	struct f_switch *sw;

	if (!f)
		return;

	if (f->sw) {
		/*
		 * Need to free switches, and also find/free the endpoints
		 * we allocated for switch management ports.
		 */
		for (s = 0; s < f->switch_cnt; s++) {
			sw = f->sw[s];
			if (!sw)
				continue;

			for (p = 0; p < sw->port_cnt; p++) {
				port = sw->port[p];
				if (port && !port->link)
					free(port);	/* management port */
			}
			free(sw);
		}
		free(f->sw);
	}
	if (f->link) {
		for (l = 0; l < f->link_cnt; l++)
			if (f->link[l])
				free(f->link[l]);

		free(f->link);
	}
	memset(f, 0, sizeof(*f));
}

void teardown_torus(struct torus *t)
{
	unsigned p, s;
	struct endpoint *port;
	struct t_switch *sw;

	if (!t)
		return;

	if (t->sw_pool) {
		/*
		 * Need to free switches, and also find/free the endpoints
		 * we allocated for switch management ports.
		 */
		for (s = 0; s < t->switch_cnt; s++) {
			sw = t->sw_pool[s];
			if (!sw)
				continue;

			for (p = 0; p < sw->port_cnt; p++) {
				port = sw->port[p];
				if (port && !port->link)
					free(port);	/* management port */
			}
			free(sw);
		}
		free(t->sw_pool);
	}
	if (t->link_pool)
		free(t->link_pool);

	if (t->sw)
		free(t->sw);

	if (t->seed)
		free(t->seed);

	free(t);
}

static
struct torus_context *torus_context_create(osm_opensm_t *osm)
{
	struct torus_context *ctx;

	ctx = calloc(1, sizeof(*ctx));
	ctx->osm = osm;

	return ctx;
}

static
void torus_context_delete(void *context)
{
	struct torus_context *ctx = context;

	teardown_fabric(&ctx->fabric);
	if (ctx->torus)
		teardown_torus(ctx->torus);
	free(ctx);
}

static
bool grow_seed_array(struct torus *t, int new_seeds)
{
	unsigned cnt;
	void *ptr;

	cnt = t->seed_cnt + new_seeds;
	if (cnt > t->seed_sz) {
		cnt += 2 + cnt / 2;
		ptr = realloc(t->seed, cnt * sizeof(*t->seed));
		if (!ptr)
			return false;
		t->seed = ptr;
		t->seed_sz = cnt;
		memset(&t->seed[t->seed_cnt], 0,
		       (cnt - t->seed_cnt) * sizeof(*t->seed));
	}
	return true;
}

static
struct f_switch *find_f_sw(struct fabric *f, guid_t sw_guid)
{
	unsigned s;
	struct f_switch *sw;

	if (f->sw) {
		for (s = 0; s < f->switch_cnt; s++) {
			sw = f->sw[s];
			if (sw->n_id == sw_guid)
				return sw;
		}
	}
	return NULL;
}

static
struct link *find_f_link(struct fabric *f,
			 guid_t guid0, int port0, guid_t guid1, int port1)
{
	unsigned l;
	struct link *link;

	if (f->link) {
		for (l = 0; l < f->link_cnt; l++) {
			link = f->link[l];
			if ((link->end[0].n_id == guid0 &&
			     link->end[0].port == port0 &&
			     link->end[1].n_id == guid1 &&
			     link->end[1].port == port1) ||
			    (link->end[0].n_id == guid1 &&
			     link->end[0].port == port1 &&
			     link->end[1].n_id == guid0 &&
			     link->end[1].port == port0))
				return link;
		}
	}
	return NULL;
}

static
struct f_switch *alloc_fswitch(struct fabric *f,
			       guid_t sw_id, unsigned port_cnt)
{
	size_t new_sw_sz;
	unsigned cnt_max;
	struct f_switch *sw = NULL;
	void *ptr;

	if (f->switch_cnt >= f->switch_cnt_max) {

		cnt_max = 16 + 5 * f->switch_cnt_max / 4;
		ptr = realloc(f->sw, cnt_max * sizeof(*f->sw));
		if (!ptr) {
			OSM_LOG(&f->osm->log, OSM_LOG_ERROR,
				"Error: realloc: %s\n", strerror(errno));
			goto out;
		}
		f->sw = ptr;
		f->switch_cnt_max = cnt_max;
		memset(&f->sw[f->switch_cnt], 0,
		       (f->switch_cnt_max - f->switch_cnt)*sizeof(*f->sw));
	}
	new_sw_sz = sizeof(*sw) + port_cnt * sizeof(*sw->port);
	sw = calloc(1, new_sw_sz);
	if (!sw) {
		OSM_LOG(&f->osm->log, OSM_LOG_ERROR,
			"Error: calloc: %s\n", strerror(errno));
		goto out;
	}
	sw->port = (void *)(sw + 1);
	sw->n_id = sw_id;
	sw->port_cnt = port_cnt;
	f->sw[f->switch_cnt++] = sw;
out:
	return sw;
}

static
struct link *alloc_flink(struct fabric *f)
{
	unsigned cnt_max;
	struct link *l = NULL;
	void *ptr;

	if (f->link_cnt >= f->link_cnt_max) {

		cnt_max = 16 + 5 * f->link_cnt_max / 4;
		ptr = realloc(f->link, cnt_max * sizeof(*f->link));
		if (!ptr) {
			OSM_LOG(&f->osm->log, OSM_LOG_ERROR,
				"Error: realloc: %s\n", strerror(errno));
			goto out;
		}
		f->link = ptr;
		f->link_cnt_max = cnt_max;
		memset(&f->link[f->link_cnt], 0,
		       (f->link_cnt_max - f->link_cnt) * sizeof(*f->link));
	}
	l = calloc(1, sizeof(*l));
	if (!l) {
		OSM_LOG(&f->osm->log, OSM_LOG_ERROR,
			"Error: calloc: %s\n", strerror(errno));
		goto out;
	}
	f->link[f->link_cnt++] = l;
out:
	return l;
}

/*
 * Caller must ensure osm_port points to a valid port which contains
 * a valid osm_physp_t pointer for port 0, the switch management port.
 */
static
bool build_sw_endpoint(struct fabric *f, osm_port_t *osm_port)
{
	int sw_port;
	guid_t sw_guid;
	struct osm_switch *osm_sw;
	struct f_switch *sw;
	struct endpoint *ep;
	bool success = false;

	sw_port = osm_physp_get_port_num(osm_port->p_physp);
	sw_guid = osm_node_get_node_guid(osm_port->p_node);
	osm_sw = osm_port->p_node->sw;

	/*
	 * The switch must already exist.
	 */
	sw = find_f_sw(f, sw_guid);
	if (!sw) {
		OSM_LOG(&f->osm->log, OSM_LOG_ERROR,
			"Error: missing switch w/ GUID 0x%04llx\n",
			ntohllu(sw_guid));
		goto out;
	}
	/*
	 * The endpoint may already exist.
	 */
	if (sw->port[sw_port]) {
		if (sw->port[sw_port]->n_id == sw_guid) {
			ep = sw->port[sw_port];
			goto success;
		} else
			OSM_LOG(&f->osm->log, OSM_LOG_ERROR,
				"Error: switch port %d has id "
				"0x%04llx, expected 0x%04llx\n",
				sw_port, ntohllu(sw->port[sw_port]->n_id),
				ntohllu(sw_guid));
		goto out;
	}
	ep = calloc(1, sizeof(*ep));
	if (!ep) {
		OSM_LOG(&f->osm->log, OSM_LOG_ERROR,
			"Error: allocating endpoint: %s\n", strerror(errno));
		goto out;
	}
	ep->type = SRCSINK;
	ep->port = sw_port;
	ep->n_id = sw_guid;
	ep->link = NULL;
	ep->sw = sw;

	sw->port[sw_port] = ep;

success:
	/*
	 * Fabric objects are temporary, so don't set osm_sw/osm_port priv
	 * pointers using them.  Wait until torus objects get constructed.
	 */
	sw->osm_switch = osm_sw;
	ep->osm_port = osm_port;

	success = true;
out:
	return success;
}

static
bool build_ca_link(struct fabric *f,
		   osm_port_t *osm_port_ca, guid_t sw_guid, int sw_port)
{
	int ca_port;
	guid_t ca_guid;
	struct link *l;
	struct f_switch *sw;
	bool success = false;

	ca_port = osm_physp_get_port_num(osm_port_ca->p_physp);
	ca_guid = osm_node_get_node_guid(osm_port_ca->p_node);

	/*
	 * The link may already exist.
	 */
	l = find_f_link(f, sw_guid, sw_port, ca_guid, ca_port);
	if (l) {
		success = true;
		goto out;
	}
	/*
	 * The switch must already exist.
	 */
	sw = find_f_sw(f, sw_guid);
	if (!sw) {
		OSM_LOG(&f->osm->log, OSM_LOG_ERROR,
			"Error: missing switch w/ GUID 0x%04llx\n",
			ntohllu(sw_guid));
		goto out;
	}
	l = alloc_flink(f);
	if (!l)
		goto out;

	l->end[0].type = PASSTHRU;
	l->end[0].port = sw_port;
	l->end[0].n_id = sw_guid;
	l->end[0].sw = sw;
	l->end[0].link = l;

	sw->port[sw_port] = &l->end[0];

	l->end[1].type = SRCSINK;
	l->end[1].port = ca_port;
	l->end[1].n_id = ca_guid;
	l->end[1].sw = NULL;		/* Correct for a CA */
	l->end[1].link = l;

	/*
	 * Fabric objects are temporary, so don't set osm_sw/osm_port priv
	 * pointers using them.  Wait until torus objects get constructed.
	 */
	l->end[1].osm_port = osm_port_ca;

	++f->ca_cnt;
	success = true;
out:
	return success;
}

static
bool build_link(struct fabric *f,
		guid_t sw_guid0, int sw_port0, guid_t sw_guid1, int sw_port1)
{
	struct link *l;
	struct f_switch *sw0, *sw1;
	bool success = false;

	/*
	 * The link may already exist.
	 */
	l = find_f_link(f, sw_guid0, sw_port0, sw_guid1, sw_port1);
	if (l) {
		success = true;
		goto out;
	}
	/*
	 * The switches must already exist.
	 */
	sw0 = find_f_sw(f, sw_guid0);
	if (!sw0) {
		OSM_LOG(&f->osm->log, OSM_LOG_ERROR,
			"Error: missing switch w/ GUID 0x%04llx\n",
			ntohllu(sw_guid0));
			goto out;
	}
	sw1 = find_f_sw(f, sw_guid1);
	if (!sw1) {
		OSM_LOG(&f->osm->log, OSM_LOG_ERROR,
			"Error: missing switch w/ GUID 0x%04llx\n",
			ntohllu(sw_guid1));
			goto out;
	}
	l = alloc_flink(f);
	if (!l)
		goto out;

	l->end[0].type = PASSTHRU;
	l->end[0].port = sw_port0;
	l->end[0].n_id = sw_guid0;
	l->end[0].sw = sw0;
	l->end[0].link = l;

	sw0->port[sw_port0] = &l->end[0];

	l->end[1].type = PASSTHRU;
	l->end[1].port = sw_port1;
	l->end[1].n_id = sw_guid1;
	l->end[1].sw = sw1;
	l->end[1].link = l;

	sw1->port[sw_port1] = &l->end[1];

	success = true;
out:
	return success;
}

static
bool parse_size(unsigned *tsz, unsigned *tflags, unsigned mask,
		const char *parse_sep)
{
	char *val, *nextchar;

	val = strtok(NULL, parse_sep);
	if (!val)
		return false;
	*tsz = strtoul(val, &nextchar, 0);
	if (*tsz) {
		if (*nextchar == 't' || *nextchar == 'T')
			*tflags &= ~mask;
		else if (*nextchar == 'm' || *nextchar == 'M')
			*tflags |= mask;
		/*
		 * A torus of radix two is also a mesh of radix two
		 * with multiple links between switches in that direction.
		 *
		 * Make it so always, otherwise the failure case routing
		 * logic gets confused.
		 */
		if (*tsz == 2)
			*tflags |= mask;
	}
	return true;
}

static
bool parse_torus(struct torus *t, const char *parse_sep)
{
	unsigned i, j, k, cnt;
	char *ptr;
	bool success = false;

	if (!parse_size(&t->x_sz, &t->flags, X_MESH, parse_sep))
		goto out;

	if (!parse_size(&t->y_sz, &t->flags, Y_MESH, parse_sep))
		goto out;

	if (!parse_size(&t->z_sz, &t->flags, Z_MESH, parse_sep))
		goto out;

	/*
	 * Set up a linear array of switch pointers big enough to hold
	 * all expected switches.
	 */
	t->sw_pool_sz = t->x_sz * t->y_sz * t->z_sz;
	t->sw_pool = calloc(t->sw_pool_sz, sizeof(*t->sw_pool));
	if (!t->sw_pool) {
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"Error: Torus switch array calloc: %s\n",
			strerror(errno));
		goto out;
	}
	/*
	 * Set things up so that t->sw[i][j][k] can point to the i,j,k switch.
	 */
	cnt = t->x_sz * (1 + t->y_sz * (1 + t->z_sz));
	t->sw = malloc(cnt * sizeof(void *));
	if (!t->sw) {
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"Error: Torus switch array malloc: %s\n",
			strerror(errno));
		goto out;
	}
	ptr = (void *)(t->sw);

	ptr += t->x_sz * sizeof(void *);
	for (i = 0; i < t->x_sz; i++) {
		t->sw[i] = (void *)ptr;
		ptr += t->y_sz * sizeof(void *);
	}
	for (i = 0; i < t->x_sz; i++)
		for (j = 0; j < t->y_sz; j++) {
			t->sw[i][j] = (void *)ptr;
			ptr += t->z_sz * sizeof(void *);
		}

	for (i = 0; i < t->x_sz; i++)
		for (j = 0; j < t->y_sz; j++)
			for (k = 0; k < t->z_sz; k++)
				t->sw[i][j][k] = NULL;

	success = true;
out:
	return success;
}

static
bool parse_pg_max_ports(struct torus *t, const char *parse_sep)
{
	char *val, *nextchar;

	val = strtok(NULL, parse_sep);
	if (!val)
		return false;
	t->portgrp_sz = strtoul(val, &nextchar, 0);
	return true;
}

static
bool parse_guid(struct torus *t, guid_t *guid, const char *parse_sep)
{
	char *val;
	bool success = false;

	val = strtok(NULL, parse_sep);
	if (!val)
		goto out;
	*guid = strtoull(val, NULL, 0);
	*guid = cl_hton64(*guid);

	success = true;
out:
	return success;
}

static
bool parse_dir_link(int c_dir, struct torus *t, const char *parse_sep)
{
	guid_t sw_guid0, sw_guid1;
	struct link *l;
	bool success = false;

	if (!parse_guid(t, &sw_guid0, parse_sep))
		goto out;

	if (!parse_guid(t, &sw_guid1, parse_sep))
		goto out;

	if (!t) {
		success = true;
		goto out;
	}

	switch (c_dir) {
	case -1:
		l = &t->seed[t->seed_cnt - 1].xm_link;
		break;
	case  1:
		l = &t->seed[t->seed_cnt - 1].xp_link;
		break;
	case -2:
		l = &t->seed[t->seed_cnt - 1].ym_link;
		break;
	case  2:
		l = &t->seed[t->seed_cnt - 1].yp_link;
		break;
	case -3:
		l = &t->seed[t->seed_cnt - 1].zm_link;
		break;
	case  3:
		l = &t->seed[t->seed_cnt - 1].zp_link;
		break;
	default:
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"Error: unknown link direction %d\n", c_dir);
		goto out;
	}
	l->end[0].type = PASSTHRU;
	l->end[0].port = -1;		/* We don't really connect. */
	l->end[0].n_id = sw_guid0;
	l->end[0].sw = NULL;		/* Fix this up later. */
	l->end[0].link = NULL;		/* Fix this up later. */

	l->end[1].type = PASSTHRU;
	l->end[1].port = -1;		/* We don't really connect. */
	l->end[1].n_id = sw_guid1;
	l->end[1].sw = NULL;		/* Fix this up later. */
	l->end[1].link = NULL;		/* Fix this up later. */

	success = true;
out:
	return success;
}

static
bool parse_dir_dateline(int c_dir, struct torus *t, const char *parse_sep)
{
	char *val;
	int *dl, max_dl;
	bool success = false;

	val = strtok(NULL, parse_sep);
	if (!val)
		goto out;

	if (!t) {
		success = true;
		goto out;
	}

	switch (c_dir) {
	case  1:
		dl = &t->seed[t->seed_cnt - 1].x_dateline;
		max_dl = t->x_sz;
		break;
	case  2:
		dl = &t->seed[t->seed_cnt - 1].y_dateline;
		max_dl = t->y_sz;
		break;
	case  3:
		dl = &t->seed[t->seed_cnt - 1].z_dateline;
		max_dl = t->z_sz;
		break;
	default:
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"Error: unknown dateline direction %d\n", c_dir);
		goto out;
	}
	*dl = strtol(val, NULL, 0);

	if ((*dl < 0 && *dl <= -max_dl) || *dl >= max_dl)
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"Error: dateline value for coordinate direction %d "
			"must be %d < dl < %d\n",
			c_dir, -max_dl, max_dl);
	else
		success = true;
out:
	return success;
}

static
bool parse_config(const char *fn, struct fabric *f, struct torus *t)
{
	FILE *fp;
	char *keyword;
	char *line_buf = NULL;
	const char *parse_sep = " \n\t";
	size_t line_buf_sz = 0;
	size_t line_cntr = 0;
	ssize_t llen;
	bool kw_success, success = true;

	if (!grow_seed_array(t, 2))
		return false;

	fp = fopen(fn, "r");
	if (!fp) {
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"Opening %s: %s\n", fn, strerror(errno));
		return false;
	}
	t->flags |= NOTIFY_CHANGES;
	t->portgrp_sz = PORTGRP_MAX_PORTS;

next_line:
	llen = getline(&line_buf, &line_buf_sz, fp);
	if (llen < 0)
		goto out;

	++line_cntr;

	keyword = strtok(line_buf, parse_sep);
	if (!keyword)
		goto next_line;

	if (strcmp("torus", keyword) == 0) {
		kw_success = parse_torus(t, parse_sep);
	} else if (strcmp("mesh", keyword) == 0) {
		t->flags |= X_MESH | Y_MESH | Z_MESH;
		kw_success = parse_torus(t, parse_sep);
	} else if (strcmp("next_seed", keyword) == 0) {
		kw_success = grow_seed_array(t, 1);
		t->seed_cnt++;
	} else if (strcmp("portgroup_max_ports", keyword) == 0) {
		kw_success = parse_pg_max_ports(t, parse_sep);
	} else if (strcmp("xp_link", keyword) == 0) {
		if (!t->seed_cnt)
			t->seed_cnt++;
		kw_success = parse_dir_link(1, t, parse_sep);
	} else if (strcmp("xm_link", keyword) == 0) {
		if (!t->seed_cnt)
			t->seed_cnt++;
		kw_success = parse_dir_link(-1, t, parse_sep);
	} else if (strcmp("x_dateline", keyword) == 0) {
		if (!t->seed_cnt)
			t->seed_cnt++;
		kw_success = parse_dir_dateline(1, t, parse_sep);
	} else if (strcmp("yp_link", keyword) == 0) {
		if (!t->seed_cnt)
			t->seed_cnt++;
		kw_success = parse_dir_link(2, t, parse_sep);
	} else if (strcmp("ym_link", keyword) == 0) {
		if (!t->seed_cnt)
			t->seed_cnt++;
		kw_success = parse_dir_link(-2, t, parse_sep);
	} else if (strcmp("y_dateline", keyword) == 0) {
		if (!t->seed_cnt)
			t->seed_cnt++;
		kw_success = parse_dir_dateline(2, t, parse_sep);
	} else if (strcmp("zp_link", keyword) == 0) {
		if (!t->seed_cnt)
			t->seed_cnt++;
		kw_success = parse_dir_link(3, t, parse_sep);
	} else if (strcmp("zm_link", keyword) == 0) {
		if (!t->seed_cnt)
			t->seed_cnt++;
		kw_success = parse_dir_link(-3, t, parse_sep);
	} else if (strcmp("z_dateline", keyword) == 0) {
		if (!t->seed_cnt)
			t->seed_cnt++;
		kw_success = parse_dir_dateline(3, t, parse_sep);
	} else if (keyword[0] == '#')
		goto next_line;
	else {
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"Error: no keyword found: line %u\n",
			(unsigned)line_cntr);
		kw_success = false;
	}
	if (!kw_success) {
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"Error: parsing '%s': line %u\n",
			keyword, (unsigned)line_cntr);
	}
	success = success && kw_success;
	goto next_line;

out:
	if (line_buf)
		free(line_buf);
	fclose(fp);
	return success;
}

static
bool capture_fabric(struct fabric *fabric)
{
	osm_subn_t *subnet = &fabric->osm->subn;
	osm_switch_t *osm_sw;
	osm_physp_t *lphysp, *rphysp;
	osm_port_t *lport;
	osm_node_t *osm_node;
	cl_map_item_t *item;
	uint8_t ltype, rtype;
	int p, port_cnt;
	guid_t sw_guid;
	bool success = true;

	OSM_LOG_ENTER(&fabric->osm->log);

	/*
	 * On OpenSM data structures:
	 *
	 * Apparently, every port in a fabric has an associated osm_physp_t,
	 * but not every port has an associated osm_port_t.  Apparently every
	 * osm_port_t has an associated osm_physp_t.
	 *
	 * So, in order to find the inter-switch links we need to walk the
	 * switch list and examine each port, via its osm_physp_t object.
	 *
	 * But, we need to associate our CA and switch management port
	 * endpoints with the corresponding osm_port_t objects, in order
	 * to simplify computation of LFT entries and perform SL lookup for
	 * path records. Since it is apparently difficult to locate the
	 * osm_port_t that corresponds to a given osm_physp_t, we also
	 * need to walk the list of ports indexed by GUID to get access
	 * to the appropriate osm_port_t objects.
	 *
	 * Need to allocate our switches before we do anything else.
	 */
	item = cl_qmap_head(&subnet->sw_guid_tbl);
	while (item != cl_qmap_end(&subnet->sw_guid_tbl)) {

		osm_sw = (osm_switch_t *)item;
		item = cl_qmap_next(item);
		osm_node = osm_sw->p_node;

		if (osm_node_get_type(osm_node) != IB_NODE_TYPE_SWITCH)
			continue;

		port_cnt = osm_node_get_num_physp(osm_node);
		sw_guid = osm_node_get_node_guid(osm_node);

		success = alloc_fswitch(fabric, sw_guid, port_cnt);
		if (!success)
			goto out;
	}
	/*
	 * Now build all our endpoints.
	 */
	item = cl_qmap_head(&subnet->port_guid_tbl);
	while (item != cl_qmap_end(&subnet->port_guid_tbl)) {

		lport = (osm_port_t *)item;
		item = cl_qmap_next(item);

		lphysp = lport->p_physp;
		if (!(lphysp && osm_physp_is_valid(lphysp)))
			continue;

		ltype = osm_node_get_type(lphysp->p_node);
		/*
		 * Switch management port is always port 0.
		 */
		if (lphysp->port_num == 0 && ltype == IB_NODE_TYPE_SWITCH) {
			success = build_sw_endpoint(fabric, lport);
			if (!success)
				goto out;
			continue;
		}
		rphysp = lphysp->p_remote_physp;
		if (!(rphysp && osm_physp_is_valid(rphysp)))
			continue;

		rtype = osm_node_get_type(rphysp->p_node);

		if ((ltype != IB_NODE_TYPE_CA &&
		     ltype != IB_NODE_TYPE_ROUTER) ||
		    rtype != IB_NODE_TYPE_SWITCH)
			continue;

		success =
			build_ca_link(fabric, lport,
				      osm_node_get_node_guid(rphysp->p_node),
				      osm_physp_get_port_num(rphysp));
		if (!success)
			goto out;
	}
	/*
	 * Lastly, build all our interswitch links.
	 */
	item = cl_qmap_head(&subnet->sw_guid_tbl);
	while (item != cl_qmap_end(&subnet->sw_guid_tbl)) {

		osm_sw = (osm_switch_t *)item;
		item = cl_qmap_next(item);

		port_cnt = osm_node_get_num_physp(osm_sw->p_node);
		for (p = 0; p < port_cnt; p++) {

			lphysp = osm_node_get_physp_ptr(osm_sw->p_node, p);
			if (!(lphysp && osm_physp_is_valid(lphysp)))
				continue;

			rphysp = lphysp->p_remote_physp;
			if (!(rphysp && osm_physp_is_valid(rphysp)))
				continue;

			if (lphysp == rphysp)
				continue;	/* ignore loopbacks */

			ltype = osm_node_get_type(lphysp->p_node);
			rtype = osm_node_get_type(rphysp->p_node);

			if (ltype != IB_NODE_TYPE_SWITCH ||
			    rtype != IB_NODE_TYPE_SWITCH)
				continue;

			success =
				build_link(fabric,
					   osm_node_get_node_guid(lphysp->p_node),
					   osm_physp_get_port_num(lphysp),
					   osm_node_get_node_guid(rphysp->p_node),
					   osm_physp_get_port_num(rphysp));
			if (!success)
				goto out;
		}
	}
out:
	OSM_LOG_EXIT(&fabric->osm->log);
	return success;
}

/*
 * diagnose_fabric() is just intended to report on fabric elements that
 * could not be placed into the torus.  We want to warn that there were
 * non-torus fabric elements, but they will be ignored for routing purposes.
 * Having them is not an error, and diagnose_fabric() thus has no return
 * value.
 */
static
void diagnose_fabric(struct fabric *f)
{
	struct link *l;
	struct endpoint *ep;
	unsigned k, p;

	/*
	 * Report on any links that didn't get transferred to the torus.
	 */
	for (k = 0; k < f->link_cnt; k++) {
		l = f->link[k];

		if (!(l->end[0].sw && l->end[1].sw))
			continue;

		OSM_LOG(&f->osm->log, OSM_LOG_INFO,
			"Found non-torus fabric link:"
			" sw GUID 0x%04llx port %d <->"
			" sw GUID 0x%04llx port %d\n",
			ntohllu(l->end[0].n_id), l->end[0].port,
			ntohllu(l->end[1].n_id), l->end[1].port);
	}
	/*
	 * Report on any switches with ports using endpoints that didn't
	 * get transferred to the torus.
	 */
	for (k = 0; k < f->switch_cnt; k++)
		for (p = 0; p < f->sw[k]->port_cnt; p++) {

			if (!f->sw[k]->port[p])
				continue;

			ep = f->sw[k]->port[p];

			/*
			 * We already reported on inter-switch links above.
			 */
			if (ep->type == PASSTHRU)
				continue;

			OSM_LOG(&f->osm->log, OSM_LOG_INFO,
				"Found non-torus fabric port:"
				" sw GUID 0x%04llx port %d\n",
				ntohllu(f->sw[k]->n_id), p);
		}
}

static
struct t_switch *alloc_tswitch(struct torus *t, struct f_switch *fsw)
{
	unsigned g;
	size_t new_sw_sz;
	struct t_switch *sw = NULL;
	void *ptr;

	if (!fsw)
		goto out;

	if (t->switch_cnt >= t->sw_pool_sz) {
		/*
		 * This should never happen, but occasionally a particularly
		 * pathological fabric can induce it.  So log an error.
		 */
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"Error: unexpectedly requested too many switch "
			"structures!\n");
		goto out;
	}
	new_sw_sz = sizeof(*sw)
		+ fsw->port_cnt * sizeof(*sw->port)
		+ SWITCH_MAX_PORTGRPS * t->portgrp_sz * sizeof(*sw->ptgrp[0].port);
	sw = calloc(1, new_sw_sz);
	if (!sw) {
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"Error: calloc: %s\n", strerror(errno));
		goto out;
	}
	sw->port = (void *)(sw + 1);
	sw->n_id = fsw->n_id;
	sw->port_cnt = fsw->port_cnt;
	sw->torus = t;
	sw->tmp = fsw;

	ptr = &sw->port[sw->port_cnt];

	for (g = 0; g < SWITCH_MAX_PORTGRPS; g++) {
		sw->ptgrp[g].port_grp = g;
		sw->ptgrp[g].sw = sw;
		sw->ptgrp[g].port = ptr;
		ptr = &sw->ptgrp[g].port[t->portgrp_sz];
	}
	t->sw_pool[t->switch_cnt++] = sw;
out:
	return sw;
}

/*
 * install_tswitch() expects the switch coordinates i,j,k to be canonicalized
 * by caller.
 */
static
bool install_tswitch(struct torus *t,
		     int i, int j, int k, struct f_switch *fsw)
{
	struct t_switch **sw = &t->sw[i][j][k];

	if (!*sw)
		*sw = alloc_tswitch(t, fsw);

	if (*sw) {
		(*sw)->i = i;
		(*sw)->j = j;
		(*sw)->k = k;
	}
	return !!*sw;
}

static
struct link *alloc_tlink(struct torus *t)
{
	if (t->link_cnt >= t->link_pool_sz) {
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"Error: unexpectedly out of pre-allocated link "
			"structures!\n");
		return NULL;
	}
	return &t->link_pool[t->link_cnt++];
}

static
int canonicalize(int v, int vmax)
{
	if (v >= 0 && v < vmax)
		return v;

	if (v < 0)
		v += vmax * (1 - v/vmax);

	return v % vmax;
}

static
unsigned set_fp_bit(bool present, int i, int j, int k)
{
	return (unsigned)(!present) << (i + 2 * j + 4 * k);
}

/*
 * Returns an 11-bit fingerprint of what switches are absent in a cube of
 * neighboring switches.  Each bit 0-7 corresponds to a corner of the cube;
 * if a bit is set the corresponding switch is absent.
 *
 * Bits 8-10 distinguish between 2D and 3D cases.  If bit 8+d is set,
 * for 0 <= d < 3;  the d dimension of the desired torus has radix greater
 * than 1. Thus, if all bits 8-10 are set, the desired torus is 3D.
 */
static
unsigned fingerprint(struct torus *t, int i, int j, int k)
{
	unsigned fp;
	int ip1, jp1, kp1;
	int x_sz_gt1, y_sz_gt1, z_sz_gt1;

	x_sz_gt1 = t->x_sz > 1;
	y_sz_gt1 = t->y_sz > 1;
	z_sz_gt1 = t->z_sz > 1;

	ip1 = canonicalize(i + 1, t->x_sz);
	jp1 = canonicalize(j + 1, t->y_sz);
	kp1 = canonicalize(k + 1, t->z_sz);

	fp  = set_fp_bit(t->sw[i][j][k], 0, 0, 0);
	fp |= set_fp_bit(t->sw[ip1][j][k], x_sz_gt1, 0, 0);
	fp |= set_fp_bit(t->sw[i][jp1][k], 0, y_sz_gt1, 0);
	fp |= set_fp_bit(t->sw[ip1][jp1][k], x_sz_gt1, y_sz_gt1, 0);
	fp |= set_fp_bit(t->sw[i][j][kp1], 0, 0, z_sz_gt1);
	fp |= set_fp_bit(t->sw[ip1][j][kp1], x_sz_gt1, 0, z_sz_gt1);
	fp |= set_fp_bit(t->sw[i][jp1][kp1], 0, y_sz_gt1, z_sz_gt1);
	fp |= set_fp_bit(t->sw[ip1][jp1][kp1], x_sz_gt1, y_sz_gt1, z_sz_gt1);

	fp |= x_sz_gt1 << 8;
	fp |= y_sz_gt1 << 9;
	fp |= z_sz_gt1 << 10;

	return fp;
}

static
bool connect_tlink(struct port_grp *pg0, struct endpoint *f_ep0,
		   struct port_grp *pg1, struct endpoint *f_ep1,
		   struct torus *t)
{
	struct link *l;
	bool success = false;

	if (pg0->port_cnt == t->portgrp_sz) {
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"Error: exceeded port group max "
			"port count (%d): switch GUID 0x%04llx\n",
			t->portgrp_sz, ntohllu(pg0->sw->n_id));
		goto out;
	}
	if (pg1->port_cnt == t->portgrp_sz) {
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"Error: exceeded port group max "
			"port count (%d): switch GUID 0x%04llx\n",
			t->portgrp_sz, ntohllu(pg1->sw->n_id));
		goto out;
	}
	l = alloc_tlink(t);
	if (!l)
		goto out;

	l->end[0].type = f_ep0->type;
	l->end[0].port = f_ep0->port;
	l->end[0].n_id = f_ep0->n_id;
	l->end[0].sw = pg0->sw;
	l->end[0].link = l;
	l->end[0].pgrp = pg0;
	pg0->port[pg0->port_cnt++] = &l->end[0];
	pg0->sw->port[f_ep0->port] = &l->end[0];

	if (f_ep0->osm_port) {
		l->end[0].osm_port = f_ep0->osm_port;
		l->end[0].osm_port->priv = &l->end[0];
		f_ep0->osm_port = NULL;
	}

	l->end[1].type = f_ep1->type;
	l->end[1].port = f_ep1->port;
	l->end[1].n_id = f_ep1->n_id;
	l->end[1].sw = pg1->sw;
	l->end[1].link = l;
	l->end[1].pgrp = pg1;
	pg1->port[pg1->port_cnt++] = &l->end[1];
	pg1->sw->port[f_ep1->port] = &l->end[1];

	if (f_ep1->osm_port) {
		l->end[1].osm_port = f_ep1->osm_port;
		l->end[1].osm_port->priv = &l->end[1];
		f_ep1->osm_port = NULL;
	}
	/*
	 * Disconnect fabric link, so that later we can see if any were
	 * left unconnected in the torus.
	 */
	((struct f_switch *)f_ep0->sw)->port[f_ep0->port] = NULL;
	f_ep0->sw = NULL;
	f_ep0->port = -1;

	((struct f_switch *)f_ep1->sw)->port[f_ep1->port] = NULL;
	f_ep1->sw = NULL;
	f_ep1->port = -1;

	success = true;
out:
	return success;
}

static
bool link_tswitches(struct torus *t, int cdir,
		    struct t_switch *t_sw0, struct t_switch *t_sw1)
{
	int p;
	struct port_grp *pg0, *pg1;
	struct f_switch *f_sw0, *f_sw1;
	char *cdir_name = "unknown";
	unsigned port_cnt;
	int success = false;

	/*
	 * If this is a 2D torus, it is possible for this function to be
	 * called with its two switch arguments being the same switch, in
	 * which case there are no links to install.
	 */
	if (t_sw0 == t_sw1 &&
	    ((cdir == 0 && t->x_sz == 1) ||
	     (cdir == 1 && t->y_sz == 1) ||
	     (cdir == 2 && t->z_sz == 1))) {
		success = true;
		goto out;
	}
	/*
	 * Ensure that t_sw1 is in the positive cdir direction wrt. t_sw0.
	 * ring_next_sw() relies on it.
	 */
	switch (cdir) {
	case 0:
		if (t->x_sz > 1 &&
		    canonicalize(t_sw0->i + 1, t->x_sz) != t_sw1->i) {
			cdir_name = "x";
			goto cdir_error;
		}
		break;
	case 1:
		if (t->y_sz > 1 &&
		    canonicalize(t_sw0->j + 1, t->y_sz) != t_sw1->j) {
			cdir_name = "y";
			goto cdir_error;
		}
		break;
	case 2:
		if (t->z_sz > 1 &&
		    canonicalize(t_sw0->k + 1, t->z_sz) != t_sw1->k) {
			cdir_name = "z";
			goto cdir_error;
		}
		break;
	default:
	cdir_error:
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR, "Error: "
			"sw 0x%04llx (%d,%d,%d) <--> sw 0x%04llx (%d,%d,%d) "
			"invalid torus %s link orientation\n",
			ntohllu(t_sw0->n_id), t_sw0->i, t_sw0->j, t_sw0->k,
			ntohllu(t_sw1->n_id), t_sw1->i, t_sw1->j, t_sw1->k,
			cdir_name);
		goto out;
	}

	f_sw0 = t_sw0->tmp;
	f_sw1 = t_sw1->tmp;

	if (!f_sw0 || !f_sw1) {
		OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
			"Error: missing fabric switches!\n"
			"  switch GUIDs: 0x%04llx 0x%04llx\n",
			ntohllu(t_sw0->n_id), ntohllu(t_sw1->n_id));
		goto out;
	}
	pg0 = &t_sw0->ptgrp[2*cdir + 1];
	pg0->type = PASSTHRU;

	pg1 = &t_sw1->ptgrp[2*cdir];
	pg1->type = PASSTHRU;

	port_cnt = f_sw0->port_cnt;
	/*
	 * Find all the links between these two switches.
	 */
	for (p = 0; p < port_cnt; p++) {
		struct endpoint *f_ep0 = NULL, *f_ep1 = NULL;

		if (!f_sw0->port[p] || !f_sw0->port[p]->link)
			continue;

		if (f_sw0->port[p]->link->end[0].n_id == t_sw0->n_id &&
		    f_sw0->port[p]->link->end[1].n_id == t_sw1->n_id) {

			f_ep0 = &f_sw0->port[p]->link->end[0];
			f_ep1 = &f_sw0->port[p]->link->end[1];
		} else if (f_sw0->port[p]->link->end[1].n_id == t_sw0->n_id &&
			   f_sw0->port[p]->link->end[0].n_id == t_sw1->n_id) {

			f_ep0 = &f_sw0->port[p]->link->end[1];
			f_ep1 = &f_sw0->port[p]->link->end[0];
		} else
			continue;

		if (!(f_ep0->type == PASSTHRU && f_ep1->type == PASSTHRU)) {
			OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
				"Error: not interswitch "
				"link:\n  0x%04llx/%d <-> 0x%04llx/%d\n",
				ntohllu(f_ep0->n_id), f_ep0->port,
				ntohllu(f_ep1->n_id), f_ep1->port);
			goto out;
		}
		/*
		 * Skip over links that already have been established in the
		 * torus.
		 */
		if (!(f_ep0->sw && f_ep1->sw))
			continue;

		if (!connect_tlink(pg0, f_ep0, pg1, f_ep1, t))
			goto out;
	}
	success = true;
out:
	return success;
}

static
bool link_srcsink(struct torus *t, int i, int j, int k)
{
	struct endpoint *f_ep0;
	struct endpoint *f_ep1;
	struct t_switch *tsw;
	struct f_switch *fsw;
	struct port_grp *pg;
	struct link *fl, *tl;
	unsigned p, port_cnt;
	bool success = false;

	i = canonicalize(i, t->x_sz);
	j = canonicalize(j, t->y_sz);
	k = canonicalize(k, t->z_sz);

	tsw = t->sw[i][j][k];
	if (!tsw)
		return true;

	fsw = tsw->tmp;
	pg = &tsw->ptgrp[2 * TORUS_MAX_DIM];
	pg->type = SRCSINK;
	tsw->osm_switch = fsw->osm_switch;
	tsw->osm_switch->priv = tsw;
	fsw->osm_switch = NULL;

	port_cnt = fsw->port_cnt;
	for (p = 0; p < port_cnt; p++) {

		if (!fsw->port[p])
			continue;

		if (fsw->port[p]->type == SRCSINK) {
			/*
			 * If the endpoint is the switch port used for in-band
			 * communication with the switch itself, move it to
			 * the torus.
			 */
			if (pg->port_cnt == t->portgrp_sz) {
				OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
					"Error: exceeded port group max port "
					"count (%d): switch GUID 0x%04llx\n",
					t->portgrp_sz, ntohllu(tsw->n_id));
				goto out;
			}
			fsw->port[p]->sw = tsw;
			fsw->port[p]->pgrp = pg;
			tsw->port[p] = fsw->port[p];
			tsw->port[p]->osm_port->priv = tsw->port[p];
			pg->port[pg->port_cnt++] = fsw->port[p];
			fsw->port[p] = NULL;

		} else if (fsw->port[p]->link &&
			   fsw->port[p]->type == PASSTHRU) {
			/*
			 * If the endpoint is a link to a CA, create a new link
			 * in the torus.  Disconnect the fabric link.
			 */

			fl = fsw->port[p]->link;

			if (fl->end[0].sw == fsw) {
				f_ep0 = &fl->end[0];
				f_ep1 = &fl->end[1];
			} else if (fl->end[1].sw == fsw) {
				f_ep1 = &fl->end[0];
				f_ep0 = &fl->end[1];
			} else
				continue;

			if (f_ep1->type != SRCSINK)
				continue;

			if (pg->port_cnt == t->portgrp_sz) {
				OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
					"Error: exceeded port group max port "
					"count (%d): switch GUID 0x%04llx\n",
					t->portgrp_sz, ntohllu(tsw->n_id));
				goto out;
			}
			/*
			 * Switch ports connected to links don't get
			 * associated with osm_port_t objects; see
			 * capture_fabric().  So just check CA end.
			 */
			if (!f_ep1->osm_port) {
				OSM_LOG(&t->osm->log, OSM_LOG_ERROR,
					"Error: NULL osm_port->priv port "
					"GUID 0x%04llx\n",
					ntohllu(f_ep1->n_id));
				goto out;
			}
			tl = alloc_tlink(t);
			if (!tl)
				continue;

			tl->end[0].type = f_ep0->type;
			tl->end[0].port = f_ep0->port;
			tl->end[0].n_id = f_ep0->n_id;
			tl->end[0].sw = tsw;
			tl->end[0].link = tl;
			tl->end[0].pgrp = pg;
			pg->port[pg->port_cnt++] = &tl->end[0];
			pg->sw->port[f_ep0->port] =  &tl->end[0];

			tl->end[1].type = f_ep1->type;
			tl->end[1].port = f_ep1->port;
			tl->end[1].n_id = f_ep1->n_id;
			tl->end[1].sw = NULL;	/* Correct for a CA */
			tl->end[1].link = tl;
			tl->end[1].pgrp = NULL;	/* Correct for a CA */

			tl->end[1].osm_port = f_ep1->osm_port;
			tl->end[1].osm_port->priv = &tl->end[1];
			f_ep1->osm_port = NULL;

			t->ca_cnt++;
			f_ep0->sw = NULL;
			f_ep0->port = -1;
			fsw->port[p] = NULL;
		}
	}
	success = true;
out:
	return success;
}

static
struct f_switch *ffind_face_corner(struct f_switch *fsw0,
				   struct f_switch *fsw1,
				   struct f_switch *fsw2)
{
	int p0, p3;
	struct link *l;
	struct endpoint *far_end;
	struct f_switch *fsw, *fsw3 = NULL;

	if (!(fsw0 && fsw1 && fsw2))
		goto out;

	for (p0 = 0; p0 < fsw0->port_cnt; p0++) {
		/*
		 * Ignore everything except switch links that haven't
		 * been installed into the torus.
		 */
		if (!(fsw0->port[p0] && fsw0->port[p0]->sw &&
		      fsw0->port[p0]->type == PASSTHRU))
			continue;

		l = fsw0->port[p0]->link;

		if (l->end[0].n_id == fsw0->n_id)
			far_end = &l->end[1];
		else
			far_end = &l->end[0];

		/*
		 * Ignore CAs
		 */
		if (!(far_end->type == PASSTHRU && far_end->sw))
			continue;

		fsw3 = far_end->sw;
		if (fsw3->n_id == fsw1->n_id)	/* existing corner */
			continue;

		for (p3 = 0; p3 < fsw3->port_cnt; p3++) {
			/*
			 * Ignore everything except switch links that haven't
			 * been installed into the torus.
			 */
			if (!(fsw3->port[p3] && fsw3->port[p3]->sw &&
			      fsw3->port[p3]->type == PASSTHRU))
				continue;

			l = fsw3->port[p3]->link;

			if (l->end[0].n_id == fsw3->n_id)
				far_end = &l->end[1];
			else
				far_end = &l->end[0];

			/*
			 * Ignore CAs
			 */
			if (!(far_end->type == PASSTHRU && far_end->sw))
				continue;

			fsw = far_end->sw;
			if (fsw->n_id == fsw2->n_id)
				goto out;
		}
	}
	fsw3 = NULL;
out:
	return fsw3;
}

static
struct f_switch *tfind_face_corner(struct t_switch *tsw0,
				   struct t_switch *tsw1,
				   struct t_switch *tsw2)
{
	if (!(tsw0 && tsw1 && tsw2))
		return NULL;

	return ffind_face_corner(tsw0->tmp, tsw1->tmp, tsw2->tmp);
}

/*
 * This code can break on any torus with a dimension that has radix four.
 *
 * What is supposed to happen is that this code will find the
 * two faces whose shared edge is the desired perpendicular.
 *
 * What actually happens is while searching we send two connected
 * edges that are colinear in a torus dimension with radix four to
 * ffind_face_corner(), which tries to complete a face by finding a
 * 4-loop of edges.
 *
 * In the radix four torus case, it can find a 4-loop which is a ring in a
 * dimension with radix four, rather than the desired face.  It thus returns
 * true when it shouldn't, so the wrong edge is returned as the perpendicular.
 *
 * The appropriate instance of safe_N_perpendicular() (where N == x, y, z)
 * should be used to determine if it is safe to call ffind_perpendicular();
 * these functions will return false it there is a possibility of finding
 * a wrong perpendicular.
 */
struct f_switch *ffind_3d_perpendicular(struct f_switch *fsw0,
					struct f_switch *fsw1,
					struct f_switch *fsw2,
					struct f_switch *fsw3)
{
	int p1;
	struct link *l;
	struct endpoint *far_end;
	struct f_switch *fsw4 = NULL;

	if (!(fsw0 && fsw1 && fsw2 && fsw3))
		goto out;

	/*
	 * Look at all the ports on the switch, fsw1,  that is the base of
	 * the perpendicular.
	 */
	for (p1 = 0; p1 < fsw1->port_cnt; p1++) {
		/*
		 * Ignore everything except switch links that haven't
		 * been installed into the torus.
		 */
		if (!(fsw1->port[p1] && fsw1->port[p1]->sw &&
		      fsw1->port[p1]->type == PASSTHRU))
			continue;

		l = fsw1->port[p1]->link;

		if (l->end[0].n_id == fsw1->n_id)
			far_end = &l->end[1];
		else
			far_end = &l->end[0];
		/*
		 * Ignore CAs
		 */
		if (!(far_end->type == PASSTHRU && far_end->sw))
			continue;

		fsw4 = far_end->sw;
		if (fsw4->n_id == fsw3->n_id)	/* wrong perpendicular */
			continue;

		if (ffind_face_corner(fsw0, fsw1, fsw4) &&
		    ffind_face_corner(fsw2, fsw1, fsw4))
			goto out;
	}
	fsw4 = NULL;
out:
	return fsw4;
}
struct f_switch *ffind_2d_perpendicular(struct f_switch *fsw0,
					struct f_switch *fsw1,
					struct f_switch *fsw2)
{
	int p1;
	struct link *l;
	struct endpoint *far_end;
	struct f_switch *fsw3 = NULL;

	if (!(fsw0 && fsw1 && fsw2))
		goto out;

	/*
	 * Look at all the ports on the switch, fsw1,  that is the base of
	 * the perpendicular.
	 */
	for (p1 = 0; p1 < fsw1->port_cnt; p1++) {
		/*
		 * Ignore everything except switch links that haven't
		 * been installed into the torus.
		 */
		if (!(fsw1->port[p1] && fsw1->port[p1]->sw &&
		      fsw1->port[p1]->type == PASSTHRU))
			continue;

		l = fsw1->port[p1]->link;

		if (l->end[0].n_id == fsw1->n_id)
			far_end = &l->end[1];
		else
			far_end = &l->end[0];
		/*
		 * Ignore CAs
		 */
		if (!(far_end->type == PASSTHRU && far_end->sw))
			continue;

		fsw3 = far_end->sw;
		if (fsw3->n_id == fsw2->n_id)	/* wrong perpendicular */
			continue;

		if (ffind_face_corner(fsw0, fsw1, fsw3))
			goto out;
	}
	fsw3 = NULL;
out:
	return fsw3;
}

static
struct f_switch *tfind_3d_perpendicular(struct t_switch *tsw0,
					struct t_switch *tsw1,
					struct t_switch *tsw2,
					struct t_switch *tsw3)
{
	if (!(tsw0 && tsw1 && tsw2 && tsw3))
		return NULL;

	return ffind_3d_perpendicular(tsw0->tmp, tsw1->tmp,
				      tsw2->tmp, tsw3->tmp);
}

static
struct f_switch *tfind_2d_perpendicular(struct t_switch *tsw0,
					struct t_switch *tsw1,
					struct t_switch *tsw2)
{
	if (!(tsw0 && tsw1 && tsw2))
		return NULL;

	return ffind_2d_perpendicular(tsw0->tmp, tsw1->tmp, tsw2->tmp);
}

static
bool safe_x_ring(struct torus *t, int i, int j, int k)
{
	int im1, ip1, ip2;
	bool success = true;

	/*
	 * If this x-direction radix-4 ring has at least two links
	 * already installed into the torus,  then this ring does not
	 * prevent us from looking for y or z direction perpendiculars.
	 *
	 * It is easier to check for the appropriate switches being installed
	 * into the torus than it is to check for the links, so force the
	 * link installation if the appropriate switches are installed.
	 *
	 * Recall that canonicalize(n - 2, 4) == canonicalize(n + 2, 4).
	 */
	if (t->x_sz != 4 || t->flags & X_MESH)
		goto out;

	im1 = canonicalize(i - 1, t->x_sz);
	ip1 = canonicalize(i + 1, t->x_sz);
	ip2 = canonicalize(i + 2, t->x_sz);

	if (!!t->sw[im1][j][k] +
	    !!t->sw[ip1][j][k] + !!t->sw[ip2][j][k] < 2) {
		success = false;
		goto out;
	}
	if (t->sw[ip2][j][k] && t->sw[im1][j][k])
		success = link_tswitches(t, 0,
					 t->sw[ip2][j][k],
					 t->sw[im1][j][k])
			&& success;

	if (t->sw[im1][j][k] && t->sw[i][j][k])
		success = link_tswitches(t, 0,
					 t->sw[im1][j][k],
					 t->sw[i][j][k])
			&& success;

	if (t->sw[i][j][k] && t->sw[ip1][j][k])
		success = link_tswitches(t, 0,
					 t->sw[i][j][k],
					 t->sw[ip1][j][k])
			&& success;

	if (t->sw[ip1][j][k] && t->sw[ip2][j][k])
		success = link_tswitches(t, 0,
					 t->sw[ip1][j][k],
					 t->sw[ip2][j][k])
			&& success;
out:
	return success;
}

static
bool safe_y_ring(struct torus *t, int i, int j, int k)
{
	int jm1, jp1, jp2;
	bool success = true;

	/*
	 * If this y-direction radix-4 ring has at least two links
	 * already installed into the torus,  then this ring does not
	 * prevent us from looking for x or z direction perpendiculars.
	 *
	 * It is easier to check for the appropriate switches being installed
	 * into the torus than it is to check for the links, so force the
	 * link installation if the appropriate switches are installed.
	 *
	 * Recall that canonicalize(n - 2, 4) == canonicalize(n + 2, 4).
	 */
	if (t->y_sz != 4 || (t->flags & Y_MESH))
		goto out;

	jm1 = canonicalize(j - 1, t->y_sz);
	jp1 = canonicalize(j + 1, t->y_sz);
	jp2 = canonicalize(j + 2, t->y_sz);

	if (!!t->sw[i][jm1][k] +
	    !!t->sw[i][jp1][k] + !!t->sw[i][jp2][k] < 2) {
		success = false;
		goto out;
	}
	if (t->sw[i][jp2][k] && t->sw[i][jm1][k])
		success = link_tswitches(t, 1,
					 t->sw[i][jp2][k],
					 t->sw[i][jm1][k])
			&& success;

	if (t->sw[i][jm1][k] && t->sw[i][j][k])
		success = link_tswitches(t, 1,
					 t->sw[i][jm1][k],
					 t->sw[i][j][k])
			&& success;

	if (t->sw[i][j][k] && t->sw[i][jp1][k])
		success = link_tswitches(t, 1,
					 t->sw[i][j][k],
					 t->sw[i][jp1][k])
			&& success;

	if (t->sw[i][jp1][k] && t->sw[i][jp2][k])
		success = link_tswitches(t, 1,
					 t->sw[i][jp1][k],
					 t->sw[i][jp2][k])
			&& success;
out:
	return success;
}

static
bool safe_z_ring(struct torus *t, int i, int j, int k)
{
	int km1, kp1, kp2;
	bool success = true;

	/*
	 * If this z-direction radix-4 ring has at least two links
	 * already installed into the torus,  then this ring does not
	 * prevent us from looking for x or y direction perpendiculars.
	 *
	 * It is easier to check for the appropriate switches being installed
	 * into the torus than it is to check for the links, so force the
	 * link installation if the appropriate switches are installed.
	 *
	 * Recall that canonicalize(n - 2, 4) == canonicalize(n + 2, 4).
	 */
	if (t->z_sz != 4 || t->flags & Z_MESH)
		goto out;

	km1 = canonicalize(k - 1, t->z_sz);
	kp1 = canonicalize(k + 1, t->z_sz);
	kp2 = canonicalize(k + 2, t->z_sz);

	if (!!t->sw[i][j][km1] +
	    !!t->sw[i][j][kp1] + !!t->sw[i][j][kp2] < 2) {
		success = false;
		goto out;
	}
	if (t->sw[i][j][kp2] && t->sw[i][j][km1])
		success = link_tswitches(t, 2,
					 t->sw[i][j][kp2],
					 t->sw[i][j][km1])
			&& success;

	if (t->sw[i][j][km1] && t->sw[i][j][k])
		success = link_tswitches(t, 2,
					 t->sw[i][j][km1],
					 t->sw[i][j][k])
			&& success;

	if (t->sw[i][j][k] && t->sw[i][j][kp1])
		success = link_tswitches(t, 2,
					 t->sw[i][j][k],
					 t->sw[i][j][kp1])
			&& success;

	if (t->sw[i][j][kp1] && t->sw[i][j][kp2])
		success = link_tswitches(t, 2,
					 t->sw[i][j][kp1],
					 t->sw[i][j][kp2])
			&& success;
out:
	return success;
}

/*
 * These functions return true when it safe to call
 * tfind_3d_perpendicular()/ffind_3d_perpendicular().
 */
static
bool safe_x_perpendicular(struct torus *t, int i, int j, int k)
{
	/*
	 * If the dimensions perpendicular to the search direction are
	 * not radix 4 torus dimensions, it is always safe to search for
	 * a perpendicular.
	 *
	 * Here we are checking for enough appropriate links having been
	 * installed into the torus to prevent an incorrect link from being
	 * considered as a perpendicular candidate.
	 */
	return safe_y_ring(t, i, j, k) && safe_z_ring(t, i, j, k);
}

static
bool safe_y_perpendicular(struct torus *t, int i, int j, int k)
{
	/*
	 * If the dimensions perpendicular to the search direction are
	 * not radix 4 torus dimensions, it is always safe to search for
	 * a perpendicular.
	 *
	 * Here we are checking for enough appropriate links having been
	 * installed into the torus to prevent an incorrect link from being
	 * considered as a perpendicular candidate.
	 */
	return safe_x_ring(t, i, j, k) && safe_z_ring(t, i, j, k);
}

static
bool safe_z_perpendicular(struct torus *t, int i, int j, int k)
{
	/*
	 * If the dimensions perpendicular to the search direction are
	 * not radix 4 torus dimensions, it is always safe to search for
	 * a perpendicular.
	 *
	 * Implement this by checking for enough appropriate links having
	 * been installed into the torus to prevent an incorrect link from
	 * being considered as a perpendicular candidate.
	 */
	return safe_x_ring(t, i, j, k) && safe_y_ring(t, i, j, k);
}

/*
 * Templates for determining 2D/3D case fingerprints. Recall that if
 * a fingerprint bit is set the corresponding switch is absent from
 * the all-switches-present template.
 *
 * I.e., for the 2D case where the x,y dimensions have a radix greater
 * than one, and the z dimension has radix 1, fingerprint bits 4-7 are
 * always zero.
 *
 * For the 2D case where the x,z dimensions have a radix greater than
 * one, and the y dimension has radix 1, fingerprint bits 2,3,6,7 are
 * always zero.
 *
 * For the 2D case where the y,z dimensions have a radix greater than
 * one, and the x dimension has radix 1, fingerprint bits 1,3,5,7 are
 * always zero.
 *
 * Recall also that bits 8-10 distinguish between 2D and 3D cases.
 * If bit 8+d is set, for 0 <= d < 3;  the d dimension of the desired
 * torus has radix greater than 1.
 */

/*
 * 2D case 0x300
 *  b0: t->sw[i  ][j  ][0  ]
 *  b1: t->sw[i+1][j  ][0  ]
 *  b2: t->sw[i  ][j+1][0  ]
 *  b3: t->sw[i+1][j+1][0  ]
 *                                    O . . . . . O
 * 2D case 0x500                      .           .
 *  b0: t->sw[i  ][0  ][k  ]          .           .
 *  b1: t->sw[i+1][0  ][k  ]          .           .
 *  b4: t->sw[i  ][0  ][k+1]          .           .
 *  b5: t->sw[i+1][0  ][k+1]          .           .
 *                                    @ . . . . . O
 * 2D case 0x600
 *  b0: t->sw[0  ][j  ][k  ]
 *  b2: t->sw[0  ][j+1][k  ]
 *  b4: t->sw[0  ][j  ][k+1]
 *  b6: t->sw[0  ][j+1][k+1]
 */

/*
 * 3D case 0x700:                           O
 *                                        . . .
 *  b0: t->sw[i  ][j  ][k  ]            .   .   .
 *  b1: t->sw[i+1][j  ][k  ]          .     .     .
 *  b2: t->sw[i  ][j+1][k  ]        .       .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4: t->sw[i  ][j  ][k+1]      . .       O       . .
 *  b5: t->sw[i+1][j  ][k+1]      .   .   .   .   .   .
 *  b6: t->sw[i  ][j+1][k+1]      .     .       .     .
 *  b7: t->sw[i+1][j+1][k+1]      .   .   .   .   .   .
 *                                . .       O       . .
 *                                O         .         O
 *                                  .       .       .
 *                                    .     .     .
 *                                      .   .   .
 *                                        . . .
 *                                          @
 */

static
void log_no_crnr(struct torus *t, unsigned n,
		 int case_i, int case_j, int case_k,
		 int crnr_i, int crnr_j, int crnr_k)
{
	if (t->debug)
		OSM_LOG(&t->osm->log, OSM_LOG_INFO, "Case 0x%03x "
			"@ %d %d %d: no corner @ %d %d %d\n",
			n, case_i, case_j, case_k, crnr_i, crnr_j, crnr_k);
}

static
void log_no_perp(struct torus *t, unsigned n,
		 int case_i, int case_j, int case_k,
		 int perp_i, int perp_j, int perp_k)
{
	if (t->debug)
		OSM_LOG(&t->osm->log, OSM_LOG_INFO, "Case 0x%03x "
			"@ %d %d %d: no perpendicular @ %d %d %d\n",
			n, case_i, case_j, case_k, perp_i, perp_j, perp_k);
}

/*
 * Handle the 2D cases with a single existing edge.
 *
 */

/*
 * 2D case 0x30c
 *  b0: t->sw[i  ][j  ][0  ]
 *  b1: t->sw[i+1][j  ][0  ]
 *  b2:
 *  b3:
 *                                    O           O
 * 2D case 0x530
 *  b0: t->sw[i  ][0  ][k  ]
 *  b1: t->sw[i+1][0  ][k  ]
 *  b4:
 *  b5:
 *                                    @ . . . . . O
 * 2D case 0x650
 *  b0: t->sw[0  ][j  ][k  ]
 *  b2: t->sw[0  ][j+1][k  ]
 *  b4:
 *  b6:
 */
static
bool handle_case_0x30c(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jm1 = canonicalize(j - 1, t->y_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);

	if (safe_y_perpendicular(t, i, j, k) &&
	    install_tswitch(t, i, jp1, k,
			    tfind_2d_perpendicular(t->sw[ip1][j][k],
						   t->sw[i][j][k],
						   t->sw[i][jm1][k]))) {
		return true;
	}
	log_no_perp(t, 0x30c, i, j, k, i, j, k);

	if (safe_y_perpendicular(t, ip1, j, k) &&
	    install_tswitch(t, ip1, jp1, k,
			    tfind_2d_perpendicular(t->sw[i][j][k],
						   t->sw[ip1][j][k],
						   t->sw[ip1][jm1][k]))) {
		return true;
	}
	log_no_perp(t, 0x30c, i, j, k, ip1, j, k);
	return false;
}

static
bool handle_case_0x530(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int km1 = canonicalize(k - 1, t->z_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_z_perpendicular(t, i, j, k) &&
	    install_tswitch(t, i, j, kp1,
			    tfind_2d_perpendicular(t->sw[ip1][j][k],
						   t->sw[i][j][k],
						   t->sw[i][j][km1]))) {
		return true;
	}
	log_no_perp(t, 0x530, i, j, k, i, j, k);

	if (safe_z_perpendicular(t, ip1, j, k) &&
	      install_tswitch(t, ip1, j, kp1,
			      tfind_2d_perpendicular(t->sw[i][j][k],
						     t->sw[ip1][j][k],
						     t->sw[ip1][j][km1]))) {
		return true;
	}
	log_no_perp(t, 0x530, i, j, k, ip1, j, k);
	return false;
}

static
bool handle_case_0x650(struct torus *t, int i, int j, int k)
{
	int jp1 = canonicalize(j + 1, t->y_sz);
	int km1 = canonicalize(k - 1, t->z_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_z_perpendicular(t, i, j, k) &&
	    install_tswitch(t, i, j, kp1,
			    tfind_2d_perpendicular(t->sw[i][jp1][k],
						   t->sw[i][j][k],
						   t->sw[i][j][km1]))) {
		return true;
	}
	log_no_perp(t, 0x650, i, j, k, i, j, k);

	if (safe_z_perpendicular(t, i, jp1, k) &&
	    install_tswitch(t, i, jp1, kp1,
			    tfind_2d_perpendicular(t->sw[i][j][k],
						   t->sw[i][jp1][k],
						   t->sw[i][jp1][km1]))) {
		return true;
	}
	log_no_perp(t, 0x650, i, j, k, i, jp1, k);
	return false;
}

/*
 * 2D case 0x305
 *  b0:
 *  b1: t->sw[i+1][j  ][0  ]
 *  b2:
 *  b3: t->sw[i+1][j+1][0  ]
 *                                    O           O
 * 2D case 0x511                                  .
 *  b0:                                           .
 *  b1: t->sw[i+1][0  ][k  ]                      .
 *  b4:                                           .
 *  b5: t->sw[i+1][0  ][k+1]                      .
 *                                    @           O
 * 2D case 0x611
 *  b0:
 *  b2: t->sw[0  ][j+1][k  ]
 *  b4:
 *  b6: t->sw[0  ][j+1][k+1]
 */
static
bool handle_case_0x305(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int ip2 = canonicalize(i + 2, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);

	if (safe_x_perpendicular(t, ip1, j, k) &&
	    install_tswitch(t, i, j, k,
			    tfind_2d_perpendicular(t->sw[ip1][jp1][k],
						   t->sw[ip1][j][k],
						   t->sw[ip2][j][k]))) {
		return true;
	}
	log_no_perp(t, 0x305, i, j, k, ip1, j, k);

	if (safe_x_perpendicular(t, ip1, jp1, k) &&
	    install_tswitch(t, i, jp1, k,
			    tfind_2d_perpendicular(t->sw[ip1][j][k],
						   t->sw[ip1][jp1][k],
						   t->sw[ip2][jp1][k]))) {
		return true;
	}
	log_no_perp(t, 0x305, i, j, k, ip1, jp1, k);
	return false;
}

static
bool handle_case_0x511(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int ip2 = canonicalize(i + 2, t->x_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_x_perpendicular(t, ip1, j, k) &&
	    install_tswitch(t, i, j, k,
			    tfind_2d_perpendicular(t->sw[ip1][j][kp1],
						   t->sw[ip1][j][k],
						   t->sw[ip2][j][k]))) {
		return true;
	}
	log_no_perp(t, 0x511, i, j, k, ip1, j, k);

	if (safe_x_perpendicular(t, ip1, j, kp1) &&
	    install_tswitch(t, i, j, kp1,
			    tfind_2d_perpendicular(t->sw[ip1][j][k],
						   t->sw[ip1][j][kp1],
						   t->sw[ip2][j][kp1]))) {
		return true;
	}
	log_no_perp(t, 0x511, i, j, k, ip1, j, kp1);
	return false;
}

static
bool handle_case_0x611(struct torus *t, int i, int j, int k)
{
	int jp1 = canonicalize(j + 1, t->y_sz);
	int jp2 = canonicalize(j + 2, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_y_perpendicular(t, i, jp1, k) &&
	    install_tswitch(t, i, j, k,
			    tfind_2d_perpendicular(t->sw[i][jp1][kp1],
						   t->sw[i][jp1][k],
						   t->sw[i][jp2][k]))) {
		return true;
	}
	log_no_perp(t, 0x611, i, j, k, i, jp1, k);

	if (safe_y_perpendicular(t, i, jp1, kp1) &&
	    install_tswitch(t, i, j, kp1,
			    tfind_2d_perpendicular(t->sw[i][jp1][k],
						   t->sw[i][jp1][kp1],
						   t->sw[i][jp2][kp1]))) {
		return true;
	}
	log_no_perp(t, 0x611, i, j, k, i, jp1, kp1);
	return false;
}

/*
 * 2D case 0x303
 *  b0:
 *  b1:
 *  b2: t->sw[i  ][j+1][0  ]
 *  b3: t->sw[i+1][j+1][0  ]
 *                                    O . . . . . O
 * 2D case 0x503
 *  b0:
 *  b1:
 *  b4: t->sw[i  ][0  ][k+1]
 *  b5: t->sw[i+1][0  ][k+1]
 *                                    @           O
 * 2D case 0x605
 *  b0:
 *  b2:
 *  b4: t->sw[0  ][j  ][k+1]
 *  b6: t->sw[0  ][j+1][k+1]
 */
static
bool handle_case_0x303(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int jp2 = canonicalize(j + 2, t->y_sz);

	if (safe_y_perpendicular(t, i, jp1, k) &&
	    install_tswitch(t, i, j, k,
			    tfind_2d_perpendicular(t->sw[ip1][jp1][k],
						   t->sw[i][jp1][k],
						   t->sw[i][jp2][k]))) {
		return true;
	}
	log_no_perp(t, 0x303, i, j, k, i, jp1, k);

	if (safe_y_perpendicular(t, ip1, jp1, k) &&
	    install_tswitch(t, ip1, j, k,
			    tfind_2d_perpendicular(t->sw[i][jp1][k],
						   t->sw[ip1][jp1][k],
						   t->sw[ip1][jp2][k]))) {
		return true;
	}
	log_no_perp(t, 0x303, i, j, k, ip1, jp1, k);
	return false;
}

static
bool handle_case_0x503(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);
	int kp2 = canonicalize(k + 2, t->z_sz);

	if (safe_z_perpendicular(t, i, j, kp1) &&
	    install_tswitch(t, i, j, k,
			    tfind_2d_perpendicular(t->sw[ip1][j][kp1],
						   t->sw[i][j][kp1],
						   t->sw[i][j][kp2]))) {
		return true;
	}
	log_no_perp(t, 0x503, i, j, k, i, j, kp1);

	if (safe_z_perpendicular(t, ip1, j, kp1) &&
	    install_tswitch(t, ip1, j, k,
			    tfind_2d_perpendicular(t->sw[i][j][kp1],
						   t->sw[ip1][j][kp1],
						   t->sw[ip1][j][kp2]))) {
		return true;
	}
	log_no_perp(t, 0x503, i, j, k, ip1, j, kp1);
	return false;
}

static
bool handle_case_0x605(struct torus *t, int i, int j, int k)
{
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);
	int kp2 = canonicalize(k + 2, t->z_sz);

	if (safe_z_perpendicular(t, i, j, kp1) &&
	    install_tswitch(t, i, j, k,
			    tfind_2d_perpendicular(t->sw[i][jp1][kp1],
						   t->sw[i][j][kp1],
						   t->sw[i][j][kp2]))) {
		return true;
	}
	log_no_perp(t, 0x605, i, j, k, i, j, kp1);

	if (safe_z_perpendicular(t, i, jp1, kp1) &&
	    install_tswitch(t, i, jp1, k,
			    tfind_2d_perpendicular(t->sw[i][j][kp1],
						   t->sw[i][jp1][kp1],
						   t->sw[i][jp1][kp2]))) {
		return true;
	}
	log_no_perp(t, 0x605, i, j, k, i, jp1, kp1);
	return false;
}

/*
 * 2D case 0x30a
 *  b0: t->sw[i  ][j  ][0  ]
 *  b1:
 *  b2: t->sw[i  ][j+1][0  ]
 *  b3:
 *                                    O           O
 * 2D case 0x522                      .
 *  b0: t->sw[i  ][0  ][k  ]          .
 *  b1:                               .
 *  b4: t->sw[i  ][0  ][k+1]          .
 *  b5:                               .
 *                                    @           O
 * 2D case 0x644
 *  b0: t->sw[0  ][j  ][k  ]
 *  b2:
 *  b4: t->sw[0  ][j  ][k+1]
 *  b6:
 */
static
bool handle_case_0x30a(struct torus *t, int i, int j, int k)
{
	int im1 = canonicalize(i - 1, t->x_sz);
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);

	if (safe_x_perpendicular(t, i, j, k) &&
	    install_tswitch(t, ip1, j, k,
			    tfind_2d_perpendicular(t->sw[i][jp1][k],
						   t->sw[i][j][k],
						   t->sw[im1][j][k]))) {
		return true;
	}
	log_no_perp(t, 0x30a, i, j, k, i, j, k);

	if (safe_x_perpendicular(t, i, jp1, k) &&
	    install_tswitch(t, ip1, jp1, k,
			    tfind_2d_perpendicular(t->sw[i][j][k],
						   t->sw[i][jp1][k],
						   t->sw[im1][jp1][k]))) {
		return true;
	}
	log_no_perp(t, 0x30a, i, j, k, i, jp1, k);
	return false;
}

static
bool handle_case_0x522(struct torus *t, int i, int j, int k)
{
	int im1 = canonicalize(i - 1, t->x_sz);
	int ip1 = canonicalize(i + 1, t->x_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_x_perpendicular(t, i, j, k) &&
	    install_tswitch(t, ip1, j, k,
			    tfind_2d_perpendicular(t->sw[i][j][kp1],
						   t->sw[i][j][k],
						   t->sw[im1][j][k]))) {
		return true;
	}
	log_no_perp(t, 0x522, i, j, k, i, j, k);

	if (safe_x_perpendicular(t, i, j, kp1) &&
	    install_tswitch(t, ip1, j, kp1,
			    tfind_2d_perpendicular(t->sw[i][j][k],
						   t->sw[i][j][kp1],
						   t->sw[im1][j][kp1]))) {
		return true;
	}
	log_no_perp(t, 0x522, i, j, k, i, j, kp1);
	return false;
}

static
bool handle_case_0x644(struct torus *t, int i, int j, int k)
{
	int jm1 = canonicalize(j - 1, t->y_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_y_perpendicular(t, i, j, k) &&
	    install_tswitch(t, i, jp1, k,
			    tfind_2d_perpendicular(t->sw[i][j][kp1],
						   t->sw[i][j][k],
						   t->sw[i][jm1][k]))) {
		return true;
	}
	log_no_perp(t, 0x644, i, j, k, i, j, k);

	if (safe_y_perpendicular(t, i, j, kp1) &&
	    install_tswitch(t, i, jp1, kp1,
			    tfind_2d_perpendicular(t->sw[i][j][k],
						   t->sw[i][j][kp1],
						   t->sw[i][jm1][kp1]))) {
		return true;
	}
	log_no_perp(t, 0x644, i, j, k, i, j, kp1);
	return false;
}

/*
 * Handle the 2D cases where two existing edges meet at a corner.
 *
 */

/*
 * 2D case 0x301
 *  b0:
 *  b1: t->sw[i+1][j  ][0  ]
 *  b2: t->sw[i  ][j+1][0  ]
 *  b3: t->sw[i+1][j+1][0  ]
 *                                    O . . . . . O
 * 2D case 0x501                                  .
 *  b0:                                           .
 *  b1: t->sw[i+1][0  ][k  ]                      .
 *  b4: t->sw[i  ][0  ][k+1]                      .
 *  b5: t->sw[i+1][0  ][k+1]                      .
 *                                    @           O
 * 2D case 0x601
 *  b0:
 *  b2: t->sw[0  ][j+1][k  ]
 *  b4: t->sw[0  ][j  ][k+1]
 *  b6: t->sw[0  ][j+1][k+1]
 */
static
bool handle_case_0x301(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);

	if (install_tswitch(t, i, j, k,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[ip1][jp1][k],
					      t->sw[i][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x301, i, j, k, i, j, k);
	return false;
}

static
bool handle_case_0x501(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, j, k,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[ip1][j][kp1],
					      t->sw[i][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x501, i, j, k, i, j, k);
	return false;
}

static
bool handle_case_0x601(struct torus *t, int i, int j, int k)
{
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, j, k,
			    tfind_face_corner(t->sw[i][jp1][k],
					      t->sw[i][jp1][kp1],
					      t->sw[i][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x601, i, j, k, i, j, k);
	return false;
}

/*
 * 2D case 0x302
 *  b0: t->sw[i  ][j  ][0  ]
 *  b1:
 *  b2: t->sw[i  ][j+1][0  ]
 *  b3: t->sw[i+1][j+1][0  ]
 *                                    O . . . . . O
 * 2D case 0x502                      .
 *  b0: t->sw[i  ][0  ][k  ]          .
 *  b1:                               .
 *  b4: t->sw[i  ][0  ][k+1]          .
 *  b5: t->sw[i+1][0  ][k+1]          .
 *                                    @           O
 * 2D case 0x604
 *  b0: t->sw[0  ][j  ][k  ]
 *  b2:
 *  b4: t->sw[0  ][j  ][k+1]
 *  b6: t->sw[0  ][j+1][k+1]
 */
static
bool handle_case_0x302(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);

	if (install_tswitch(t, ip1, j, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][jp1][k],
					      t->sw[ip1][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x302, i, j, k, ip1, j, k);
	return false;
}

static
bool handle_case_0x502(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, j, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][j][kp1],
					      t->sw[ip1][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x502, i, j, k, ip1, j, k);
	return false;
}

static
bool handle_case_0x604(struct torus *t, int i, int j, int k)
{
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, jp1, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][j][kp1],
					      t->sw[i][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x604, i, j, k, i, jp1, k);
	return false;
}


/*
 * 2D case 0x308
 *  b0: t->sw[i  ][j  ][0  ]
 *  b1: t->sw[i+1][j  ][0  ]
 *  b2: t->sw[i  ][j+1][0  ]
 *  b3:
 *                                    O           O
 * 2D case 0x520                      .
 *  b0: t->sw[i  ][0  ][k  ]          .
 *  b1: t->sw[i+1][0  ][k  ]          .
 *  b4: t->sw[i  ][0  ][k+1]          .
 *  b5:                               .
 *                                    @ . . . . . O
 * 2D case 0x640
 *  b0: t->sw[0  ][j  ][k  ]
 *  b2: t->sw[0  ][j+1][k  ]
 *  b4: t->sw[0  ][j  ][k+1]
 *  b6:
 */
static
bool handle_case_0x308(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);

	if (install_tswitch(t, ip1, jp1, k,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[i][j][k],
					      t->sw[i][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x308, i, j, k, ip1, jp1, k);
	return false;
}

static
bool handle_case_0x520(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, j, kp1,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[i][j][k],
					      t->sw[i][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x520, i, j, k, ip1, j, kp1);
	return false;
}

static
bool handle_case_0x640(struct torus *t, int i, int j, int k)
{
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, jp1, kp1,
			    tfind_face_corner(t->sw[i][jp1][k],
					      t->sw[i][j][k],
					      t->sw[i][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x640, i, j, k, i, jp1, kp1);
	return false;
}

/*
 * 2D case 0x304
 *  b0: t->sw[i  ][j  ][0  ]
 *  b1: t->sw[i+1][j  ][0  ]
 *  b2:
 *  b3: t->sw[i+1][j+1][0  ]
 *                                    O           O
 * 2D case 0x510                                  .
 *  b0: t->sw[i  ][0  ][k  ]                      .
 *  b1: t->sw[i+1][0  ][k  ]                      .
 *  b4:                                           .
 *  b5: t->sw[i+1][0  ][k+1]                      .
 *                                    @ . . . . . O
 * 2D case 0x610
 *  b0: t->sw[0  ][j  ][k  ]
 *  b2: t->sw[0  ][j+1][k  ]
 *  b4:
 *  b6: t->sw[0  ][j+1][k+1]
 */
static
bool handle_case_0x304(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);

	if (install_tswitch(t, i, jp1, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[ip1][j][k],
					      t->sw[ip1][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x304, i, j, k, i, jp1, k);
	return false;
}

static
bool handle_case_0x510(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, j, kp1,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[ip1][j][k],
					      t->sw[ip1][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x510, i, j, k, i, j, kp1);
	return false;
}

static
bool handle_case_0x610(struct torus *t, int i, int j, int k)
{
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, j, kp1,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][jp1][k],
					      t->sw[i][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x610, i, j, k, i, j, kp1);
	return false;
}

/*
 * Handle the 3D cases where two existing edges meet at a corner.
 *
 */

/*
 * 3D case 0x71f:                           O
 *                                        .   .
 *  b0:                                 .       .
 *  b1:                               .           .
 *  b2:                             .               .
 *  b3:                           O                   O
 *  b4:                                     O
 *  b5: t->sw[i+1][j  ][k+1]
 *  b6: t->sw[i  ][j+1][k+1]
 *  b7: t->sw[i+1][j+1][k+1]
 *                                          O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x71f(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);
	int kp2 = canonicalize(k + 2, t->z_sz);

	if (safe_z_perpendicular(t, ip1, jp1, kp1) &&
	    install_tswitch(t, ip1, jp1, k,
			    tfind_3d_perpendicular(t->sw[ip1][j][kp1],
						   t->sw[ip1][jp1][kp1],
						   t->sw[i][jp1][kp1],
						   t->sw[ip1][jp1][kp2]))) {
		return true;
	}
	log_no_perp(t, 0x71f, i, j, k, ip1, jp1, kp1);
	return false;
}

/*
 * 3D case 0x72f:                           O
 *                                        .
 *  b0:                                 .
 *  b1:                               .
 *  b2:                             .
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]        .       O
 *  b5:                               .
 *  b6: t->sw[i  ][j+1][k+1]            .
 *  b7: t->sw[i+1][j+1][k+1]              .
 *                                          O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x72f(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);
	int kp2 = canonicalize(k + 2, t->z_sz);

	if (safe_z_perpendicular(t, i, jp1, kp1) &&
	    install_tswitch(t, i, jp1, k,
			    tfind_3d_perpendicular(t->sw[i][j][kp1],
						   t->sw[i][jp1][kp1],
						   t->sw[ip1][jp1][kp1],
						   t->sw[i][jp1][kp2]))) {
		return true;
	}
	log_no_perp(t, 0x72f, i, j, k, i, jp1, kp1);
	return false;
}

/*
 * 3D case 0x737:                           O
 *                                        . .
 *  b0:                                 .   .
 *  b1:                               .     .
 *  b2:                             .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4:                                     O
 *  b5:
 *  b6: t->sw[i  ][j+1][k+1]
 *  b7: t->sw[i+1][j+1][k+1]
 *                                          O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x737(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int jp2 = canonicalize(j + 2, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_y_perpendicular(t, ip1, jp1, kp1) &&
	    install_tswitch(t, ip1, j, kp1,
			    tfind_3d_perpendicular(t->sw[i][jp1][kp1],
						   t->sw[ip1][jp1][kp1],
						   t->sw[ip1][jp1][k],
						   t->sw[ip1][jp2][kp1]))) {
		return true;
	}
	log_no_perp(t, 0x737, i, j, k, ip1, jp1, kp1);
	return false;
}

/*
 * 3D case 0x73b:                           O
 *                                        .
 *  b0:                                 .
 *  b1:                               .
 *  b2: t->sw[i  ][j+1][k  ]        .
 *  b3:                           O                   O
 *  b4:                           .         O
 *  b5:                           .
 *  b6: t->sw[i  ][j+1][k+1]      .
 *  b7: t->sw[i+1][j+1][k+1]      .
 *                                .         O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x73b(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int jp2 = canonicalize(j + 2, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_y_perpendicular(t, i, jp1, kp1) &&
	    install_tswitch(t, i, j, kp1,
			    tfind_3d_perpendicular(t->sw[i][jp1][k],
						   t->sw[i][jp1][kp1],
						   t->sw[ip1][jp1][kp1],
						   t->sw[i][jp2][kp1]))) {
		return true;
	}
	log_no_perp(t, 0x73b, i, j, k, i, jp1, kp1);
	return false;
}

/*
 * 3D case 0x74f:                           O
 *                                            .
 *  b0:                                         .
 *  b1:                                           .
 *  b2:                                             .
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]                O       .
 *  b5: t->sw[i+1][j  ][k+1]                      .
 *  b6:                                         .
 *  b7: t->sw[i+1][j+1][k+1]                  .
 *                                          O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x74f(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);
	int kp2 = canonicalize(k + 2, t->z_sz);

	if (safe_z_perpendicular(t, ip1, j, kp1) &&
	    install_tswitch(t, ip1, j, k,
			    tfind_3d_perpendicular(t->sw[i][j][kp1],
						   t->sw[ip1][j][kp1],
						   t->sw[ip1][jp1][kp1],
						   t->sw[ip1][j][kp2]))) {
		return true;
	}
	log_no_perp(t, 0x74f, i, j, k, ip1, j, kp1);
	return false;
}

/*
 * 3D case 0x757:                           O
 *                                          . .
 *  b0:                                     .   .
 *  b1:                                     .     .
 *  b2:                                     .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4:                                     O
 *  b5: t->sw[i+1][j  ][k+1]
 *  b6:
 *  b7: t->sw[i+1][j+1][k+1]
 *                                          O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x757(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int ip2 = canonicalize(i + 2, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_x_perpendicular(t, ip1, jp1, kp1) &&
	    install_tswitch(t, i, jp1, kp1,
			    tfind_3d_perpendicular(t->sw[ip1][j][kp1],
						   t->sw[ip1][jp1][kp1],
						   t->sw[ip1][jp1][k],
						   t->sw[ip2][jp1][kp1]))) {
		return true;
	}
	log_no_perp(t, 0x757, i, j, k, ip1, jp1, kp1);
	return false;
}

/*
 * 3D case 0x75d:                           O
 *                                            .
 *  b0:                                         .
 *  b1: t->sw[i+1][j  ][k  ]                      .
 *  b2:                                             .
 *  b3:                           O                   O
 *  b4:                                     O         .
 *  b5: t->sw[i+1][j  ][k+1]                          .
 *  b6:                                               .
 *  b7: t->sw[i+1][j+1][k+1]                          .
 *                                          O         .
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x75d(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int ip2 = canonicalize(i + 2, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_x_perpendicular(t, ip1, j, kp1) &&
	    install_tswitch(t, i, j, kp1,
			    tfind_3d_perpendicular(t->sw[ip1][j][k],
						   t->sw[ip1][j][kp1],
						   t->sw[ip1][jp1][kp1],
						   t->sw[ip2][j][kp1]))) {
		return true;
	}
	log_no_perp(t, 0x75d, i, j, k, ip1, j, kp1);
	return false;
}

/*
 * 3D case 0x773:                           O
 *                                          .
 *  b0:                                     .
 *  b1:                                     .
 *  b2: t->sw[i  ][j+1][k  ]                .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4:                                     O
 *  b5:                                   .
 *  b6:                                 .
 *  b7: t->sw[i+1][j+1][k+1]          .
 *                                  .       O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x773(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int jp2 = canonicalize(j + 2, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_y_perpendicular(t, ip1, jp1, k) &&
	    install_tswitch(t, ip1, j, k,
			    tfind_3d_perpendicular(t->sw[i][jp1][k],
						   t->sw[ip1][jp1][k],
						   t->sw[ip1][jp1][kp1],
						   t->sw[ip1][jp2][k]))) {
		return true;
	}
	log_no_perp(t, 0x773, i, j, k, ip1, jp1, k);
	return false;
}

/*
 * 3D case 0x775:                           O
 *                                          .
 *  b0:                                     .
 *  b1: t->sw[i+1][j  ][k  ]                .
 *  b2:                                     .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4:                                     O
 *  b5:                                       .
 *  b6:                                         .
 *  b7: t->sw[i+1][j+1][k+1]                      .
 *                                          O       .
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x775(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int ip2 = canonicalize(i + 2, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_x_perpendicular(t, ip1, jp1, k) &&
	    install_tswitch(t, i, jp1, k,
			    tfind_3d_perpendicular(t->sw[ip1][j][k],
						   t->sw[ip1][jp1][k],
						   t->sw[ip1][jp1][kp1],
						   t->sw[ip2][jp1][k]))) {
		return true;
	}
	log_no_perp(t, 0x775, i, j, k, ip1, jp1, k);
	return false;
}

/*
 * 3D case 0x78f:                           O
 *
 *  b0:
 *  b1:
 *  b2:
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]        .       O       .
 *  b5: t->sw[i+1][j  ][k+1]          .           .
 *  b6: t->sw[i  ][j+1][k+1]            .       .
 *  b7:                                   .   .
 *                                          O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x78f(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);
	int kp2 = canonicalize(k + 2, t->z_sz);

	if (safe_z_perpendicular(t, i, j, kp1) &&
	    install_tswitch(t, i, j, k,
			    tfind_3d_perpendicular(t->sw[ip1][j][kp1],
						   t->sw[i][j][kp1],
						   t->sw[i][jp1][kp1],
						   t->sw[i][j][kp2]))) {
		return true;
	}
	log_no_perp(t, 0x78f, i, j, k, i, j, kp1);
	return false;
}

/*
 * 3D case 0x7ab:                           O
 *
 *  b0:
 *  b1:
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]      . .       O
 *  b5:                           .   .
 *  b6: t->sw[i  ][j+1][k+1]      .     .
 *  b7:                           .       .
 *                                .         O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x7ab(struct torus *t, int i, int j, int k)
{
	int im1 = canonicalize(i - 1, t->x_sz);
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_x_perpendicular(t, i, jp1, kp1) &&
	    install_tswitch(t, ip1, jp1, kp1,
			    tfind_3d_perpendicular(t->sw[i][j][kp1],
						   t->sw[i][jp1][kp1],
						   t->sw[i][jp1][k],
						   t->sw[im1][jp1][kp1]))) {
		return true;
	}
	log_no_perp(t, 0x7ab, i, j, k, i, jp1, kp1);
	return false;
}

/*
 * 3D case 0x7ae:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1:
 *  b2:
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]        .       O
 *  b5:                               .
 *  b6: t->sw[i  ][j+1][k+1]            .
 *  b7:                                   .
 *                                          O
 *                                O         .         O
 *                                          .
 *                                          .
 *                                          .
 *                                          .
 *                                          @
 */
static
bool handle_case_0x7ae(struct torus *t, int i, int j, int k)
{
	int im1 = canonicalize(i - 1, t->x_sz);
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_x_perpendicular(t, i, j, kp1) &&
	    install_tswitch(t, ip1, j, kp1,
			    tfind_3d_perpendicular(t->sw[i][j][k],
						   t->sw[i][j][kp1],
						   t->sw[i][jp1][kp1],
						   t->sw[im1][j][kp1]))) {
		return true;
	}
	log_no_perp(t, 0x7ae, i, j, k, i, j, kp1);
	return false;
}

/*
 * 3D case 0x7b3:                           O
 *
 *  b0:
 *  b1:
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3: t->sw[i+1][j+1][k  ]      O                   O
 *  b4:                           .         O
 *  b5:                           .       .
 *  b6: t->sw[i  ][j+1][k+1]      .     .
 *  b7:                           .   .
 *                                . .       O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x7b3(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int jp2 = canonicalize(j + 2, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_y_perpendicular(t, i, jp1, k) &&
	    install_tswitch(t, i, j, k,
			    tfind_3d_perpendicular(t->sw[i][jp1][kp1],
						   t->sw[i][jp1][k],
						   t->sw[ip1][jp1][k],
						   t->sw[i][jp2][k]))) {
		return true;
	}
	log_no_perp(t, 0x7b3, i, j, k, i, jp1, k);
	return false;
}

/*
 * 3D case 0x7ba:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1:
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3:                           O                   O
 *  b4:                           .         O
 *  b5:                           .
 *  b6: t->sw[i  ][j+1][k+1]      .
 *  b7:                           .
 *                                .         O
 *                                O                   O
 *                                  .
 *                                    .
 *                                      .
 *                                        .
 *                                          @
 */
static
bool handle_case_0x7ba(struct torus *t, int i, int j, int k)
{
	int im1 = canonicalize(i - 1, t->x_sz);
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_x_perpendicular(t, i, jp1, k) &&
	    install_tswitch(t, ip1, jp1, k,
			    tfind_3d_perpendicular(t->sw[i][j][k],
						   t->sw[i][jp1][k],
						   t->sw[i][jp1][kp1],
						   t->sw[im1][jp1][k]))) {
		return true;
	}
	log_no_perp(t, 0x7ba, i, j, k, i, jp1, k);
	return false;
}

/*
 * 3D case 0x7cd:                           O
 *
 *  b0:
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2:
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]                O       . .
 *  b5: t->sw[i+1][j  ][k+1]                      .   .
 *  b6:                                         .     .
 *  b7:                                       .       .
 *                                          O         .
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x7cd(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int jm1 = canonicalize(j - 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_y_perpendicular(t, ip1, j, kp1) &&
	    install_tswitch(t, ip1, jp1, kp1,
			    tfind_3d_perpendicular(t->sw[i][j][kp1],
						   t->sw[ip1][j][kp1],
						   t->sw[ip1][j][k],
						   t->sw[ip1][jm1][kp1]))) {
		return true;
	}
	log_no_perp(t, 0x7cd, i, j, k, ip1, j, kp1);
	return false;
}

/*
 * 3D case 0x7ce:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1:
 *  b2:
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]                O       .
 *  b5: t->sw[i+1][j  ][k+1]                      .
 *  b6:                                         .
 *  b7:                                       .
 *                                          O
 *                                O         .         O
 *                                          .
 *                                          .
 *                                          .
 *                                          .
 *                                          @
 */
static
bool handle_case_0x7ce(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int jm1 = canonicalize(j - 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_y_perpendicular(t, i, j, kp1) &&
	    install_tswitch(t, i, jp1, kp1,
			    tfind_3d_perpendicular(t->sw[i][j][k],
						   t->sw[i][j][kp1],
						   t->sw[ip1][j][kp1],
						   t->sw[i][jm1][kp1]))) {
		return true;
	}
	log_no_perp(t, 0x7ce, i, j, k, i, j, kp1);
	return false;
}

/*
 * 3D case 0x7d5:                           O
 *
 *  b0:
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2:
 *  b3: t->sw[i+1][j+1][k  ]      O                   O
 *  b4:                                     O         .
 *  b5: t->sw[i+1][j  ][k+1]                  .       .
 *  b6:                                         .     .
 *  b7:                                           .   .
 *                                          O       . .
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x7d5(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int ip2 = canonicalize(i + 2, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_x_perpendicular(t, ip1, j, k) &&
	    install_tswitch(t, i, j, k,
			    tfind_3d_perpendicular(t->sw[ip1][j][kp1],
						   t->sw[ip1][j][k],
						   t->sw[ip1][jp1][k],
						   t->sw[ip2][j][k]))) {
		return true;
	}
	log_no_perp(t, 0x7d5, i, j, k, ip1, j, k);
	return false;
}

/*
 * 3D case 0x7dc:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2:
 *  b3:                           O                   O
 *  b4:                                     O         .
 *  b5: t->sw[i+1][j  ][k+1]                          .
 *  b6:                                               .
 *  b7:                                               .
 *                                          O         .
 *                                O                   O
 *                                                  .
 *                                                .
 *                                              .
 *                                            .
 *                                          @
 */
static
bool handle_case_0x7dc(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int jm1 = canonicalize(j - 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_y_perpendicular(t, ip1, j, k) &&
	    install_tswitch(t, ip1, jp1, k,
			    tfind_3d_perpendicular(t->sw[i][j][k],
						   t->sw[ip1][j][k],
						   t->sw[ip1][j][kp1],
						   t->sw[ip1][jm1][k]))) {
		return true;
	}
	log_no_perp(t, 0x7dc, i, j, k, ip1, j, k);
	return false;
}

/*
 * 3D case 0x7ea:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1:
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3:                            O                   O
 *  b4: t->sw[i  ][j  ][k+1]                 O
 *  b5:
 *  b6:
 *  b7:
 *                                          O
 *                                O         .         O
 *                                  .       .
 *                                    .     .
 *                                      .   .
 *                                        . .
 *                                          @
 */
static
bool handle_case_0x7ea(struct torus *t, int i, int j, int k)
{
	int im1 = canonicalize(i - 1, t->x_sz);
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_x_perpendicular(t, i, j, k) &&
	    install_tswitch(t, ip1, j, k,
			    tfind_3d_perpendicular(t->sw[i][j][kp1],
						   t->sw[i][j][k],
						   t->sw[i][jp1][k],
						   t->sw[im1][j][k]))) {
		return true;
	}
	log_no_perp(t, 0x7ea, i, j, k, i, j, k);
	return false;
}

/*
 * 3D case 0x7ec:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2:
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]                O
 *  b5:
 *  b6:
 *  b7:
 *                                          O
 *                                O         .         O
 *                                          .       .
 *                                          .     .
 *                                          .   .
 *                                          . .
 *                                          @
 */
static
bool handle_case_0x7ec(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int jm1 = canonicalize(j - 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_y_perpendicular(t, i, j, k) &&
	    install_tswitch(t, i, jp1, k,
			    tfind_3d_perpendicular(t->sw[i][j][kp1],
						   t->sw[i][j][k],
						   t->sw[ip1][j][k],
						   t->sw[i][jm1][k]))) {
		return true;
	}
	log_no_perp(t, 0x7ec, i, j, k, i, j, k);
	return false;
}

/*
 * 3D case 0x7f1:                           O
 *
 *  b0:
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3: t->sw[i+1][j+1][k  ]      O                   O
 *  b4:                                     O
 *  b5:                                   .   .
 *  b6:                                 .       .
 *  b7:                               .           .
 *                                  .       O       .
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x7f1(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int km1 = canonicalize(k - 1, t->z_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_z_perpendicular(t, ip1, jp1, k) &&
	    install_tswitch(t, ip1, jp1, kp1,
			    tfind_3d_perpendicular(t->sw[ip1][j][k],
						   t->sw[ip1][jp1][k],
						   t->sw[i][jp1][k],
						   t->sw[ip1][jp1][km1]))) {
		return true;
	}
	log_no_perp(t, 0x7f1, i, j, k, ip1, jp1, k);
	return false;
}

/*
 * 3D case 0x7f2:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1:
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3: t->sw[i+1][j+1][k  ]      O                   O
 *  b4:                                     O
 *  b5:                                   .
 *  b6:                                 .
 *  b7:                               .
 *                                  .       O
 *                                O                   O
 *                                  .
 *                                    .
 *                                      .
 *                                        .
 *                                          @
 */
static
bool handle_case_0x7f2(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int km1 = canonicalize(k - 1, t->z_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_z_perpendicular(t, i, jp1, k) &&
	    install_tswitch(t, i, jp1, kp1,
			    tfind_3d_perpendicular(t->sw[i][j][k],
						   t->sw[i][jp1][k],
						   t->sw[ip1][jp1][k],
						   t->sw[i][jp1][km1]))) {
		return true;
	}
	log_no_perp(t, 0x7f2, i, j, k, i, jp1, k);
	return false;
}

/*
 * 3D case 0x7f4:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2:
 *  b3: t->sw[i+1][j+1][k  ]      O                   O
 *  b4:                                     O
 *  b5:                                       .
 *  b6:                                         .
 *  b7:                                           .
 *                                          O       .
 *                                O                   O
 *                                                  .
 *                                                .
 *                                              .
 *                                            .
 *                                          @
 */
static
bool handle_case_0x7f4(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int km1 = canonicalize(k - 1, t->z_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_z_perpendicular(t, ip1, j, k) &&
	    install_tswitch(t, ip1, j, kp1,
			    tfind_3d_perpendicular(t->sw[i][j][k],
						   t->sw[ip1][j][k],
						   t->sw[ip1][jp1][k],
						   t->sw[ip1][j][km1]))) {
		return true;
	}
	log_no_perp(t, 0x7f4, i, j, k, ip1, j, k);
	return false;
}

/*
 * 3D case 0x7f8:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3:                           O                   O
 *  b4:                                     O
 *  b5:
 *  b6:
 *  b7:
 *                                          O
 *                                O                   O
 *                                  .               .
 *                                    .           .
 *                                      .       .
 *                                        .   .
 *                                          @
 */
static
bool handle_case_0x7f8(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int km1 = canonicalize(k - 1, t->z_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (safe_z_perpendicular(t, i, j, k) &&
	    install_tswitch(t, i, j, kp1,
			    tfind_3d_perpendicular(t->sw[ip1][j][k],
						   t->sw[i][j][k],
						   t->sw[i][jp1][k],
						   t->sw[i][j][km1]))) {
		return true;
	}
	log_no_perp(t, 0x7f8, i, j, k, i, j, k);
	return false;
}

/*
 * Handle the cases where three existing edges meet at a corner.
 */

/*
 * 3D case 0x717:                           O
 *                                        . . .
 *  b0:                                 .   .   .
 *  b1:                               .     .     .
 *  b2:                             .       .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4:                                     O
 *  b5: t->sw[i+1][j  ][k+1]
 *  b6: t->sw[i  ][j+1][k+1]
 *  b7: t->sw[i+1][j+1][k+1]
 *                                          O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x717(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, j, kp1,
			    tfind_face_corner(t->sw[i][jp1][kp1],
					      t->sw[ip1][jp1][kp1],
					      t->sw[ip1][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x717, i, j, k, i, j, kp1);

	if (install_tswitch(t, ip1, j, k,
			    tfind_face_corner(t->sw[ip1][jp1][k],
					      t->sw[ip1][jp1][kp1],
					      t->sw[ip1][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x717, i, j, k, ip1, j, k);

	if (install_tswitch(t, i, jp1, k,
			    tfind_face_corner(t->sw[ip1][jp1][k],
					      t->sw[ip1][jp1][kp1],
					      t->sw[i][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x717, i, j, k, i, jp1, k);
	return false;
}

/*
 * 3D case 0x72b:                           O
 *                                        .
 *  b0:                                 .
 *  b1:                               .
 *  b2: t->sw[i  ][j+1][k  ]        .
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]      . .       O
 *  b5:                           .   .
 *  b6: t->sw[i  ][j+1][k+1]      .     .
 *  b7: t->sw[i+1][j+1][k+1]      .       .
 *                                .         O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x72b(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, j, kp1,
			    tfind_face_corner(t->sw[i][j][kp1],
					      t->sw[i][jp1][kp1],
					      t->sw[ip1][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x72b, i, j, k, ip1, j, kp1);

	if (install_tswitch(t, i, j, k,
			    tfind_face_corner(t->sw[i][jp1][k],
					      t->sw[i][jp1][kp1],
					      t->sw[i][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x72b, i, j, k, i, j, k);

	if (install_tswitch(t, ip1, jp1, k,
			    tfind_face_corner(t->sw[i][jp1][k],
					      t->sw[i][jp1][kp1],
					      t->sw[ip1][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x72b, i, j, k, ip1, jp1, k);
	return false;
}

/*
 * 3D case 0x74d:                           O
 *                                            .
 *  b0:                                         .
 *  b1: t->sw[i+1][j  ][k  ]                      .
 *  b2:                                             .
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]                O       . .
 *  b5: t->sw[i+1][j  ][k+1]                      .   .
 *  b6:                                         .     .
 *  b7: t->sw[i+1][j+1][k+1]                  .       .
 *                                          O         .
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x74d(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, jp1, kp1,
			    tfind_face_corner(t->sw[i][j][kp1],
					      t->sw[ip1][j][kp1],
					      t->sw[ip1][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x74d, i, j, k, i, jp1, kp1);

	if (install_tswitch(t, i, j, k,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[ip1][j][kp1],
					      t->sw[i][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x74d, i, j, k, i, j, k);

	if (install_tswitch(t, ip1, jp1, k,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[ip1][j][kp1],
					      t->sw[ip1][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x74d, i, j, k, ip1, jp1, k);
	return false;
}

/*
 * 3D case 0x771:                           O
 *                                          .
 *  b0:                                     .
 *  b1: t->sw[i+1][j  ][k  ]                .
 *  b2: t->sw[i  ][j+1][k  ]                .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4:                                     O
 *  b5:                                   .   .
 *  b6:                                 .       .
 *  b7: t->sw[i+1][j+1][k+1]          .           .
 *                                  .       O       .
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x771(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, j, k,
			    tfind_face_corner(t->sw[i][jp1][k],
					      t->sw[ip1][jp1][k],
					      t->sw[ip1][j][k]))) {
		return true;
	}
	log_no_crnr(t, 0x771, i, j, k, i, j, k);

	if (install_tswitch(t, ip1, j, kp1,
			    tfind_face_corner(t->sw[ip1][jp1][kp1],
					      t->sw[ip1][jp1][k],
					      t->sw[ip1][j][k]))) {
		return true;
	}
	log_no_crnr(t, 0x771, i, j, k, ip1, j, kp1);

	if (install_tswitch(t, i, jp1, kp1,
			    tfind_face_corner(t->sw[ip1][jp1][kp1],
					      t->sw[ip1][jp1][k],
					      t->sw[i][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x771, i, j, k, i, jp1, kp1);
	return false;
}

/*
 * 3D case 0x78e:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1:
 *  b2:
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]        .       O       .
 *  b5: t->sw[i+1][j  ][k+1]          .           .
 *  b6: t->sw[i  ][j+1][k+1]            .       .
 *  b7:                                   .   .
 *                                          O
 *                                O         .         O
 *                                          .
 *                                          .
 *                                          .
 *                                          .
 *                                          @
 */
static
bool handle_case_0x78e(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, jp1, kp1,
			    tfind_face_corner(t->sw[ip1][j][kp1],
					      t->sw[i][j][kp1],
					      t->sw[i][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x78e, i, j, k, ip1, jp1, kp1);

	if (install_tswitch(t, ip1, j, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][j][kp1],
					      t->sw[ip1][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x78e, i, j, k, ip1, j, k);

	if (install_tswitch(t, i, jp1, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][j][kp1],
					      t->sw[i][jp1][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x78e, i, j, k, i, jp1, k);
	return false;
}

/*
 * 3D case 0x7b2:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1:
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3: t->sw[i+1][j+1][k  ]      O                   O
 *  b4:                           .         O
 *  b5:                           .       .
 *  b6: t->sw[i  ][j+1][k+1]      .     .
 *  b7:                           .   .
 *                                . .       O
 *                                O                   O
 *                                  .
 *                                    .
 *                                      .
 *                                        .
 *                                          @
 */
static
bool handle_case_0x7b2(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, j, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[i][jp1][k],
					      t->sw[ip1][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7b2, i, j, k, ip1, j, k);

	if (install_tswitch(t, ip1, jp1, kp1,
			    tfind_face_corner(t->sw[i][jp1][kp1],
					      t->sw[i][jp1][k],
					      t->sw[ip1][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7b2, i, j, k, ip1, jp1, kp1);

	if (install_tswitch(t, i, j, kp1,
			    tfind_face_corner(t->sw[i][jp1][kp1],
					      t->sw[i][jp1][k],
					      t->sw[i][j][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7b2, i, j, k, i, j, kp1);
	return false;
}

/*
 * 3D case 0x7d4:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2:
 *  b3: t->sw[i+1][j+1][k  ]      O                   O
 *  b4:                                     O         .
 *  b5: t->sw[i+1][j  ][k+1]                  .       .
 *  b6:                                         .     .
 *  b7:                                           .   .
 *                                          O       . .
 *                                O                   O
 *                                                  .
 *                                                .
 *                                              .
 *                                            .
 *                                          @
 */
static
bool handle_case_0x7d4(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, i, jp1, k,
			    tfind_face_corner(t->sw[i][j][k],
					      t->sw[ip1][j][k],
					      t->sw[ip1][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7d4, i, j, k, i, jp1, k);

	if (install_tswitch(t, i, j, kp1,
			    tfind_face_corner(t->sw[ip1][j][kp1],
					      t->sw[ip1][j][k],
					      t->sw[i][j][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7d4, i, j, k, i, j, kp1);

	if (install_tswitch(t, ip1, jp1, kp1,
			    tfind_face_corner(t->sw[ip1][j][kp1],
					      t->sw[ip1][j][k],
					      t->sw[ip1][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7d4, i, j, k, ip1, jp1, kp1);
	return false;
}

/*
 * 3D case 0x7e8:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]                O
 *  b5:
 *  b6:
 *  b7:
 *                                          O
 *                                O         .         O
 *                                  .       .       .
 *                                    .     .     .
 *                                      .   .   .
 *                                        . . .
 *                                          @
 */
static
bool handle_case_0x7e8(struct torus *t, int i, int j, int k)
{
	int ip1 = canonicalize(i + 1, t->x_sz);
	int jp1 = canonicalize(j + 1, t->y_sz);
	int kp1 = canonicalize(k + 1, t->z_sz);

	if (install_tswitch(t, ip1, jp1, k,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[i][j][k],
					      t->sw[i][jp1][k]))) {
		return true;
	}
	log_no_crnr(t, 0x7e8, i, j, k, ip1, jp1, k);

	if (install_tswitch(t, ip1, j, kp1,
			    tfind_face_corner(t->sw[ip1][j][k],
					      t->sw[i][j][k],
					      t->sw[i][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x7e8, i, j, k, ip1, j, kp1);

	if (install_tswitch(t, i, jp1, kp1,
			    tfind_face_corner(t->sw[i][jp1][k],
					      t->sw[i][j][k],
					      t->sw[i][j][kp1]))) {
		return true;
	}
	log_no_crnr(t, 0x7e8, i, j, k, i, jp1, kp1);
	return false;
}

/*
 * Handle the cases where four corners on a single face are missing.
 */

/*
 * 3D case 0x70f:                           O
 *                                        .   .
 *  b0:                                 .       .
 *  b1:                               .           .
 *  b2:                             .               .
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]        .       O       .
 *  b5: t->sw[i+1][j  ][k+1]          .           .
 *  b6: t->sw[i  ][j+1][k+1]            .       .
 *  b7: t->sw[i+1][j+1][k+1]              .   .
 *                                          O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x70f(struct torus *t, int i, int j, int k)
{
	if (handle_case_0x71f(t, i, j, k))
		return true;

	if (handle_case_0x72f(t, i, j, k))
		return true;

	if (handle_case_0x74f(t, i, j, k))
		return true;

	return handle_case_0x78f(t, i, j, k);
}

/*
 * 3D case 0x733:                           O
 *                                        . .
 *  b0:                                 .   .
 *  b1:                               .     .
 *  b2: t->sw[i  ][j+1][k  ]        .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4:                           .         O
 *  b5:                           .       .
 *  b6: t->sw[i  ][j+1][k+1]      .     .
 *  b7: t->sw[i+1][j+1][k+1]      .   .
 *                                . .       O
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x733(struct torus *t, int i, int j, int k)
{
	if (handle_case_0x737(t, i, j, k))
		return true;

	if (handle_case_0x73b(t, i, j, k))
		return true;

	if (handle_case_0x773(t, i, j, k))
		return true;

	return handle_case_0x7b3(t, i, j, k);
}

/*
 * 3D case 0x755:                           O
 *                                          . .
 *  b0:                                     .   .
 *  b1: t->sw[i+1][j  ][k  ]                .     .
 *  b2:                                     .       .
 *  b3: t->sw[i+1][j+1][k  ]      O         .         O
 *  b4:                                     O         .
 *  b5: t->sw[i+1][j  ][k+1]                  .       .
 *  b6:                                         .     .
 *  b7: t->sw[i+1][j+1][k+1]                      .   .
 *                                          O       . .
 *                                O                   O
 *
 *
 *
 *
 *                                          @
 */
static
bool handle_case_0x755(struct torus *t, int i, int j, int k)
{
	if (handle_case_0x757(t, i, j, k))
		return true;

	if (handle_case_0x75d(t, i, j, k))
		return true;

	if (handle_case_0x775(t, i, j, k))
		return true;

	return handle_case_0x7d5(t, i, j, k);
}

/*
 * 3D case 0x7aa:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1:
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]      . .       O
 *  b5:                           .   .
 *  b6: t->sw[i  ][j+1][k+1]      .     .
 *  b7:                           .       .
 *                                .         O
 *                                O         .         O
 *                                  .       .
 *                                    .     .
 *                                      .   .
 *                                        . .
 *                                          @
 */
static
bool handle_case_0x7aa(struct torus *t, int i, int j, int k)
{
	if (handle_case_0x7ab(t, i, j, k))
		return true;

	if (handle_case_0x7ae(t, i, j, k))
		return true;

	if (handle_case_0x7ba(t, i, j, k))
		return true;

	return handle_case_0x7ea(t, i, j, k);
}

/*
 * 3D case 0x7cc:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2:
 *  b3:                           O                   O
 *  b4: t->sw[i  ][j  ][k+1]                O       . .
 *  b5: t->sw[i+1][j  ][k+1]                      .   .
 *  b6:                                         .     .
 *  b7:                                       .       .
 *                                          O         .
 *                                O         .         O
 *                                          .       .
 *                                          .     .
 *                                          .   .
 *                                          . .
 *                                          @
 */
static
bool handle_case_0x7cc(struct torus *t, int i, int j, int k)
{
	if (handle_case_0x7cd(t, i, j, k))
		return true;

	if (handle_case_0x7ce(t, i, j, k))
		return true;

	if (handle_case_0x7dc(t, i, j, k))
		return true;

	return handle_case_0x7ec(t, i, j, k);
}

/*
 * 3D case 0x7f0:                           O
 *
 *  b0: t->sw[i  ][j  ][k  ]
 *  b1: t->sw[i+1][j  ][k  ]
 *  b2: t->sw[i  ][j+1][k  ]
 *  b3: t->sw[i+1][j+1][k  ]      O                   O
 *  b4:                                     O
 *  b5:                                   .   .
 *  b6:                                 .       .
 *  b7:                               .           .
 *                                  .       O       .
 *                                O                   O
 *                                  .               .
 *                                    .           .
 *                                      .       .
 *                                        .   .
 *                                          @
 */
static
bool handle_case_0x7f0(struct torus *t, int i, int j, int k)
{
	if (handle_case_0x7f1(t, i, j, k))
		return true;

	if (handle_case_0x7f2(t, i, j, k))
		return true;

	if (handle_case_0x7f4(t, i, j, k))
		return true;

	return handle_case_0x7f8(t, i, j, k);
}
