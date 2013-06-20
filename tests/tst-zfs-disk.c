/* dumbed down version of ztest for bringup */
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012 by Delphix. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2012 Martin Matuska <mm@FreeBSD.org>.  All rights reserved.
 */
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/dmu.h>
#include <sys/txg.h>
#include <sys/dbuf.h>
#include <sys/zap.h>
#include <sys/dmu_objset.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/zio.h>
#include <sys/zil.h>
#include <sys/zil_impl.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_file.h>
#include <sys/spa_impl.h>
#include <sys/metaslab_impl.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_scan.h>
#include <sys/zio_checksum.h>
#include <sys/refcount.h>
#include <sys/zfeature.h>

typedef struct ztest_shared_opts {
	char zo_pool[MAXNAMELEN];
	char zo_dir[MAXNAMELEN];
	char zo_alt_ztest[MAXNAMELEN];
	char zo_alt_libpath[MAXNAMELEN];
	uint64_t zo_vdevs;
	uint64_t zo_vdevtime;
	size_t zo_vdev_size;
	int zo_ashift;
	int zo_mirrors;
	int zo_raidz;
	int zo_raidz_parity;
	int zo_datasets;
	int zo_threads;
	uint64_t zo_passtime;
	uint64_t zo_killrate;
	int zo_verbose;
	int zo_init;
	uint64_t zo_time;
	uint64_t zo_maxloops;
	uint64_t zo_metaslab_gang_bang;
} ztest_shared_opts_t;

static ztest_shared_opts_t ztest_opts = {
	.zo_pool = { 'o', 's', 'v', '\0' },
	.zo_dir = { '/', 'u', 's', 'r', '\0' },
	.zo_alt_ztest = { '\0' },
	.zo_alt_libpath = { '\0' },
	.zo_vdevs = 5,
	.zo_ashift = SPA_MINBLOCKSHIFT,
	.zo_mirrors = 2,
	.zo_raidz = 4,
	.zo_raidz_parity = 1,
	.zo_vdev_size = SPA_MINDEVSIZE,
	.zo_datasets = 7,
	.zo_threads = 23,
	.zo_passtime = 60,		/* 60 seconds */
	.zo_killrate = 70,		/* 70% kill rate */
	.zo_verbose = 0,
	.zo_init = 1,
	.zo_time = 300,			/* 5 minutes */
	.zo_maxloops = 50,		/* max loops during spa_freeze() */
	.zo_metaslab_gang_bang = 32 << 10
};

#if 0
static nvlist_t *
make_vdev_disk(char *path, uint64_t pgid, uint64_t guid)
{
	nvlist_t *disk;

	VERIFY(nvlist_alloc(&disk, NV_UNIQUE_NAME, 0) == 0);
	VERIFY(nvlist_add_string(disk, ZPOOL_CONFIG_TYPE, VDEV_TYPE_DISK) == 0);
	VERIFY(nvlist_add_string(disk, ZPOOL_CONFIG_PATH, path) == 0);
	VERIFY(nvlist_add_uint64(disk, ZPOOL_CONFIG_ASHIFT, SPA_MINBLOCKSHIFT) == 0);
	VERIFY(nvlist_add_uint64(disk, ZPOOL_CONFIG_POOL_GUID, pgid) == 0);
	VERIFY(nvlist_add_uint64(disk, ZPOOL_CONFIG_GUID, guid) == 0);

	return (disk);
}

static nvlist_t *
make_vdev_root(uint64_t pgid, uint64_t guid)
{
	nvlist_t *root, **child;
	int c;

	ASSERT(t > 0);

	child = calloc(1, sizeof (nvlist_t *));
	child[0] = make_vdev_disk("/dev/vblk1", pgid, guid);
	VERIFY(nvlist_add_uint64(child[0], ZPOOL_CONFIG_IS_LOG, 0) == 0);

	VERIFY(nvlist_alloc(&root, NV_UNIQUE_NAME, 0) == 0);
	VERIFY(nvlist_add_string(root, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT) == 0);
	VERIFY(nvlist_add_uint64(root, ZPOOL_CONFIG_POOL_GUID, pgid) == 0);

	VERIFY(nvlist_add_nvlist_array(root, ZPOOL_CONFIG_CHILDREN, child, 1) == 0);

	nvlist_free(child[0]);
	free(child);

	return (root);
}
#endif

int
vdev_disk_read_rootlabel(char *devname, nvlist_t **config);

int
spa_config_parse(spa_t *spa, vdev_t **vdp, nvlist_t *nv, vdev_t *parent,
    uint_t id, int atype);

#if 0
static void fix_type(nvlist_t *nv)
{
	nvlist_t **child;
	uint_t c, children;
	char *type;

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
			&child, &children) == 0) {
		for (c = 0; c < children; c++)
			fix_type(child[c]);
		return;
	}

	kprintf("fixing type\n");
	VERIFY(nvlist_lookup_string(nv, ZPOOL_CONFIG_TYPE, &type) == 0);
	VERIFY(nvlist_add_string(nv, ZPOOL_CONFIG_TYPE, VDEV_TYPE_DISK) == 0);
}
#endif

static nvlist_t *
spa_generate_rootconf(char *devpath, uint64_t *guid)
{
	nvlist_t *config;
	nvlist_t *nvtop, *nvroot, *children;
	uint64_t pgid;

	if (vdev_disk_read_rootlabel(devpath, &config) != 0)
		return (NULL);

	/*
	 * Add this top-level vdev to the child array.
	 */
	VERIFY(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvtop) == 0);

//	fix_type(nvtop);

	VERIFY(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
	    &pgid) == 0);
	VERIFY(nvlist_lookup_uint64(config, ZPOOL_CONFIG_GUID, guid) == 0);

	/*
	 * Put this pool's top-level vdevs into a root vdev.
	 */
	VERIFY(nvlist_alloc(&nvroot, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_add_string(nvroot, ZPOOL_CONFIG_TYPE,
	    VDEV_TYPE_ROOT) == 0);
	VERIFY(nvlist_add_uint64(nvroot, ZPOOL_CONFIG_ID, 0ULL) == 0);
	VERIFY(nvlist_add_uint64(nvroot, ZPOOL_CONFIG_GUID, pgid) == 0);
	VERIFY(nvlist_add_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
	    &nvtop, 1) == 0);

	/*
	 * Replace the existing vdev_tree with the new root vdev in
	 * this pool's configuration (remove the old, add the new).
	 */
	VERIFY(nvlist_add_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, nvroot) == 0);
	nvlist_free(nvroot);
	return (config);
}

int main(int argc, char **argv)
{
	nvlist_t *config, *nvtop, *children;
	spa_t *spa;
	vdev_t *rvd;
	uint64_t guid, txg;
	char *pname;
	int error;

	(void) spa_destroy(ztest_opts.zo_pool);

#if 0
	VERIFY3U(0, ==, vdev_disk_read_rootlabel("/dev/vblk1", &config));
	VERIFY3U(0, ==, nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID, &pgid));
	VERIFY3U(0, ==, nvlist_lookup_uint64(config, ZPOOL_CONFIG_GUID, &guid));
	VERIFY3U(0, ==, nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,, &nvtop));

	/*
	 * Create the storage pool.
	 */
	nvroot = make_vdev_root(pgid, guid);
//	VERIFY3U(0, ==, spa_create(ztest_opts.zo_pool, nvroot, props,
//	    NULL, NULL));

#endif

	config = spa_generate_rootconf("/dev/vblk1", &guid);

	VERIFY(nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME,
	    &pname) == 0);
	VERIFY(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_TXG, &txg) == 0);

	mutex_enter(&spa_namespace_lock);
	if ((spa = spa_lookup(pname)) != NULL) {
		/*
		 * Remove the existing root pool from the namespace so that we
		 * can replace it with the correct config we just read in.
		 */
		spa_remove(spa);
	}

	spa = spa_add(pname, config, NULL);
	VERIFY3U(NULL, !=, spa);

	spa->spa_is_root = B_TRUE;
	spa->spa_import_flags = ZFS_IMPORT_VERBATIM;

	/*
	 * Build up a vdev tree based on the boot device's label config.
	 */
	VERIFY(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvtop) == 0);
	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	error = spa_config_parse(spa, &rvd, nvtop, NULL, 0,
	    VDEV_ALLOC_ROOTPOOL);
	spa_config_exit(spa, SCL_ALL, FTAG);
	if (error) {
		mutex_exit(&spa_namespace_lock);
		nvlist_free(config);
		kprintf("Can not parse the config for pool '%s'", pname);
		return (error);
	}

	spa_history_log_version(spa, LOG_POOL_IMPORT);

	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	vdev_free(rvd);
	spa_config_exit(spa, SCL_ALL, FTAG);
	mutex_exit(&spa_namespace_lock);

	nvlist_free(config);


	VERIFY3U(0, ==, spa_open(ztest_opts.zo_pool, &spa, FTAG));
	spa_close(spa, FTAG);
	return 0;
}
