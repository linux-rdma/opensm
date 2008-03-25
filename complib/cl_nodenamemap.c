/*
 * Copyright (c) 2007 Lawrence Livermore National Lab
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

#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctype.h>
#include <config.h>

#include <complib/cl_nodenamemap.h>

static void
read_names(nn_map_t *map)
{
	char *line = NULL;
	size_t len = 0;
	name_map_item_t *item;

	rewind(map->fp);
	while (getline(&line, &len, map->fp) != -1) {
		char *guid_str = NULL;
		char *name = NULL;
		line[len-1] = '\0';
		if (line[0] == '#')
			continue;

		guid_str = strtok(line, "\"#");
		name = strtok(NULL, "\"#");
		if (!guid_str || !name)
			continue;

		item = malloc(sizeof(*item));
		if (!item) {
			goto error;
		}
		item->guid = strtoull(guid_str, NULL, 0);
		item->name = strdup(name);
		cl_qmap_insert(&(map->map), item->guid, (cl_map_item_t *)item);
	}

error:
	free (line);
}

nn_map_t *
open_node_name_map(char *node_name_map)
{
	FILE *tmp_fp = NULL;
	nn_map_t *rc = NULL;

	if (node_name_map != NULL) {
		tmp_fp = fopen(node_name_map, "r");
		if (tmp_fp == NULL) {
			fprintf(stderr,
				"WARNING failed to open switch map \"%s\" (%s)\n",
				node_name_map, strerror(errno));
		}
#ifdef HAVE_DEFAULT_NODENAME_MAP
	} else {
		tmp_fp = fopen(HAVE_DEFAULT_NODENAME_MAP, "r");
#endif /* HAVE_DEFAULT_NODENAME_MAP */
	}
	if (!tmp_fp)
		return (NULL);

	rc = malloc(sizeof(*rc));
	if (!rc)
		return (NULL);
	rc->fp = tmp_fp;
	cl_qmap_init(&(rc->map));
	read_names(rc);
	return (rc);
}

void
close_node_name_map(nn_map_t *map)
{
	name_map_item_t *item = NULL;

	if (!map)
		return;

	item = (name_map_item_t *)cl_qmap_head(&(map->map));
	while (item != (name_map_item_t *)cl_qmap_end(&(map->map))) {
		item = (name_map_item_t *)cl_qmap_remove(&(map->map), item->guid);
		free(item->name);
		free(item);
		item = (name_map_item_t *)cl_qmap_head(&(map->map));
	}
	if (map->fp)
		fclose(map->fp);
	free(map);
}

char *
remap_node_name(nn_map_t *map, uint64_t target_guid, char *nodedesc)
{
	char *rc = NULL;
	name_map_item_t *item = NULL;

	if (!map)
		goto done;

	item = (name_map_item_t *)cl_qmap_get(&(map->map), target_guid);
	if (item != (name_map_item_t *)cl_qmap_end(&(map->map)))
		rc = strdup(item->name);

done:
	if (rc == NULL)
		rc = strdup(clean_nodedesc(nodedesc));
	return (rc);
}

char *
clean_nodedesc(char *nodedesc)
{
	int i = 0;

	nodedesc[63] = '\0';
	while (nodedesc[i]) {
		if (!isprint(nodedesc[i]))
			nodedesc[i] = ' ';
		i++;
	}

	return (nodedesc);
}

int parse_node_map(const char *file_name,
		   int (*create)(void *, uint64_t, char *), void *cxt)
{
	char line[256];
	FILE *f;

	if (!(f = fopen(file_name, "r")))
		return -1;

	while (fgets(line, sizeof(line),f)) {
		uint64_t guid;
		char *p, *e;

		p = line;
		while (isspace(*p))
			p++;
		if (*p == '\0' || *p == '\n' || *p == '#')
			continue;

		guid = strtoull(p, &e, 0);
		if (e == p || !isspace(*e)) {
			fclose(f);
			return -1;
		}

		p = e;
		if (*e)
			e++;
		while (isspace(*p))
			p++;

		e = strpbrk(p, "# \t\n");
		if (e)
			*e = '\0';

		if (create(cxt, guid, p)) {
			fclose(f);
			return -1;
		}
	}

	fclose(f);
	return 0;
}
