/*****************************************************************************\
 *  identity.c
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "src/common/group_cache.h"
#include "src/common/identity.h"
#include "src/common/pack.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

extern identity_t *fetch_identity(uid_t uid, gid_t gid, bool group_names)
{
	identity_t *id;
	struct passwd pwd, *result;
	char buffer[PW_BUF_SIZE];
	int rc;

	rc = slurm_getpwuid_r(uid, &pwd, buffer, PW_BUF_SIZE, &result);
	if (rc || !result) {
		if (!result && !rc)
			error("%s: getpwuid_r(%u): no record found",
			      __func__, uid);
		else
			error("%s: getpwuid_r(%u): %s",
			      __func__, uid, slurm_strerror(rc));
		return NULL;
	}

	id = xmalloc(sizeof(*id));

	id->pw_name = xstrdup(result->pw_name);
	id->pw_gecos = xstrdup(result->pw_gecos);
	id->pw_dir = xstrdup(result->pw_dir);
	id->pw_shell = xstrdup(result->pw_shell);

	id->ngids = group_cache_lookup(uid, gid, id->pw_name, &id->gids);

	if (group_names) {
		id->gr_names = xcalloc(id->ngids, sizeof(char *));
		for (int i = 0; i < id->ngids; i++)
			id->gr_names[i] = gid_to_string(id->gids[i]);
	}

	return id;
}

extern void pack_identity(identity_t *id, buf_t *buffer,
			  uint16_t protocol_version)
{
	/*
	 * The gr_names array is optional. If the array exists the length
	 * must match that of the gids array.
	 */
	uint32_t gr_names_cnt = (id->gr_names) ? id->ngids : 0;
	identity_t null_id = {};

	if (!id)
		id = &null_id;

	packstr(id->pw_name, buffer);
	packstr(id->pw_gecos, buffer);
	packstr(id->pw_dir, buffer);
	packstr(id->pw_shell, buffer);
	pack32_array(id->gids, id->ngids, buffer);
	packstr_array(id->gr_names, gr_names_cnt, buffer);
}

extern int unpack_identity(identity_t **out, buf_t *buffer,
			   uint16_t protocol_version)
{
	uint32_t u32_ngids;
	identity_t *id = xmalloc(sizeof(*id));

	safe_unpackstr(&id->pw_name, buffer);
	safe_unpackstr(&id->pw_gecos, buffer);
	safe_unpackstr(&id->pw_dir, buffer);
	safe_unpackstr(&id->pw_shell, buffer);
	safe_unpack32_array(&id->gids, &u32_ngids, buffer);
	id->ngids = u32_ngids;
	safe_unpackstr_array(&id->gr_names, &u32_ngids, buffer);
	if (u32_ngids && (id->ngids != u32_ngids)) {
		error("%s: mismatch on gr_names array, %u != %u",
		      __func__, u32_ngids, id->ngids);
		goto unpack_error;
	}

	*out = id;
	return SLURM_SUCCESS;

unpack_error:
	destroy_identity(id);
	return SLURM_ERROR;
}

extern identity_t *copy_identity(identity_t *id)
{
	identity_t *new;

	if (!id)
		return NULL;

	new = xmalloc(sizeof(*new));
	new->pw_name = xstrdup(id->pw_name);
	new->pw_gecos = xstrdup(id->pw_gecos);
	new->pw_dir = xstrdup(id->pw_dir);
	new->pw_shell = xstrdup(id->pw_shell);

	new->ngids = id->ngids;
	new->gids = copy_gids(id->ngids, id->gids);

	if (id->gr_names) {
		new->gr_names = xcalloc(id->ngids, sizeof(char *));
		for (int i = 0; i < new->ngids; i++)
			new->gr_names[i] = xstrdup(id->gr_names[i]);
	}

	return new;
}

extern void destroy_identity(identity_t *id)
{
	if (!id)
		return;

	xfree(id->pw_name);
	xfree(id->pw_gecos);
	xfree(id->pw_dir);
	xfree(id->pw_shell);
	xfree(id->gids);

	if (id->gr_names) {
		for (int i = 0; i < id->ngids; i++)
			xfree(id->gr_names[i]);
		xfree(id->gr_names);
	}
	id->ngids = 0;
}
