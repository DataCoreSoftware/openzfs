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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2014 by Delphix. All rights reserved.
 * Portions Copyright 2022 Andrew Innes <andrew.c12@gmail.com>
 */

#include <sys/zfs_context.h>

typedef struct zfs_dbgmsg {
	list_node_t zdm_node;
	time_t zdm_timestamp;
	int zdm_size;
	char zdm_msg[1]; /* variable length allocation */
} zfs_dbgmsg_t;

list_t zfs_dbgmsgs;
int zfs_dbgmsg_size;
kmutex_t zfs_dbgmsgs_lock;
int zfs_dbgmsg_maxsize = 4<<20; /* 4MB */
kstat_t *zfs_dbgmsg_kstat;

int zfs_dbgmsg_enable = 1;

static int
zfs_dbgmsg_headers(char *buf, size_t size)
{
	(void) snprintf(buf, size, "%-12s %-8s\n", "timestamp", "message");

	return (0);
}

static int
zfs_dbgmsg_data(char *buf, size_t size, void *data)
{
	zfs_dbgmsg_t *zdm = (zfs_dbgmsg_t *)data;

	(void) snprintf(buf, size, "%-12llu %-s\n",
	    (u_longlong_t)zdm->zdm_timestamp, zdm->zdm_msg);

	return (0);
}

static void *
zfs_dbgmsg_addr(kstat_t *ksp, loff_t n)
{
	zfs_dbgmsg_t *zdm = (zfs_dbgmsg_t *)ksp->ks_private;

	ASSERT(MUTEX_HELD(&zfs_dbgmsgs_lock));

	if (n == 0)
		ksp->ks_private = list_head(&zfs_dbgmsgs);
	else if (zdm)
		ksp->ks_private = list_next(&zfs_dbgmsgs, zdm);

	return (ksp->ks_private);
}

static void
zfs_dbgmsg_purge(int max_size)
{
	zfs_dbgmsg_t *zdm;
	int size;

	ASSERT(MUTEX_HELD(&zfs_dbgmsgs_lock));

	while (zfs_dbgmsg_size > max_size) {
		zdm = list_remove_head(&zfs_dbgmsgs);
		if (zdm == NULL)
			return;

		size = zdm->zdm_size;
		kmem_free(zdm, size);
		zfs_dbgmsg_size -= size;
	}
}

static int
zfs_dbgmsg_update(kstat_t *ksp, int rw)
{
	if (rw == KSTAT_WRITE)
		zfs_dbgmsg_purge(0);

	return (0);
}

/*
 * Debug logging is enabled by default for production kernel builds.
 * The overhead for this is negligible and the logs can be valuable when
 * debugging.  For non-production user space builds all debugging except
 * logging is enabled since performance is no longer a concern.
 */
void
zfs_dbgmsg_init(void)
{
	list_create(&zfs_dbgmsgs, sizeof (zfs_dbgmsg_t),
	    offsetof(zfs_dbgmsg_t, zdm_node));
	mutex_init(&zfs_dbgmsgs_lock, NULL, MUTEX_DEFAULT, NULL);

	zfs_dbgmsg_kstat = kstat_create("zfs", 0, "dbgmsg", "misc",
	    KSTAT_TYPE_RAW, 0, KSTAT_FLAG_VIRTUAL);
	if (zfs_dbgmsg_kstat) {
		zfs_dbgmsg_kstat->ks_lock = &zfs_dbgmsgs_lock;
		zfs_dbgmsg_kstat->ks_ndata = UINT32_MAX;
		zfs_dbgmsg_kstat->ks_private = NULL;
		zfs_dbgmsg_kstat->ks_update = zfs_dbgmsg_update;
		kstat_set_raw_ops(zfs_dbgmsg_kstat, zfs_dbgmsg_headers,
		    zfs_dbgmsg_data, zfs_dbgmsg_addr);
		kstat_install(zfs_dbgmsg_kstat);
	}
}

void
zfs_dbgmsg_fini(void)
{
	zfs_dbgmsg_t *zdm;

	if (zfs_dbgmsg_kstat)
		kstat_delete(zfs_dbgmsg_kstat);

	while ((zdm = list_remove_head(&zfs_dbgmsgs)) != NULL) {
		/*
		 * Free with the size that was recorded at allocation, not one
		 * recomputed from the message. __zfs_dbgmsg() stores it in
		 * zdm_size for exactly this reason and zfs_dbgmsg_purge()
		 * already uses it. Recomputing re-derives the length from
		 * zdm_msg, so any truncation or later edit of the message
		 * yields a smaller size than was allocated and hands
		 * kmem_free() the wrong size - which returns the buffer to the
		 * wrong kmem cache and silently corrupts the allocator.
		 */
		int size = zdm->zdm_size;
		kmem_free(zdm, size);
		zfs_dbgmsg_size -= size;
	}
	mutex_destroy(&zfs_dbgmsgs_lock);
	ASSERT0(zfs_dbgmsg_size);
}

void
__set_error(const char *file, const char *func, int line, int err)
{
	/*
	 * To enable this:
	 *
	 * $ echo 512 >/sys/module/zfs/parameters/zfs_flags
	 */
	if (zfs_flags & ZFS_DEBUG_SET_ERROR)
		__dprintf(B_FALSE, file, func, line, "error %lu", err);

	TraceEvent(5, "%s:%s Line:%d Error:%d", file, func, line, err);
}

/*
 * Print these messages by running:
 * echo ::zfs_dbgmsg | mdb -k
 *
 * Monitor these messages by running:
 * dtrace -qn 'zfs-dbgmsg{printf("%s\n", stringof(arg0))}'
 *
 * When used with libzpool, monitor with:
 * dtrace -qn 'zfs$pid::zfs_dbgmsg:probe1{printf("%s\n", copyinstr(arg1))}'
 */

/*
 * Look into Windows dtrace?
 * MacOS X's dtrace doesn't handle the PROBEs, so
 * we have a utility function that we can watch with
 * sudo dtrace -qn '__zfs_dbgmsg:entry{printf("%s\n", stringof(arg0));}'
 */
noinline void
__zfs_dbgmsg(char *buf)
{
	int size = sizeof (zfs_dbgmsg_t) + strlen(buf);
	zfs_dbgmsg_t *zdm = kmem_zalloc(size, KM_SLEEP);
	zdm->zdm_size = size;
	zdm->zdm_timestamp = gethrestime_sec();

	/*
	 * The bound is the space at zdm_msg, not the size of the whole
	 * allocation: zdm_msg starts at offsetof(zfs_dbgmsg_t, zdm_msg), so
	 * only size - that many bytes exist there. Passing `size` claimed 28
	 * bytes more than the buffer has. It does not overrun today only
	 * because strlcpy() stops at the source length, which is 3 bytes
	 * inside the real capacity - a margin no caller states or enforces.
	 */
	strlcpy(zdm->zdm_msg, buf,
	    size - offsetof(zfs_dbgmsg_t, zdm_msg));

	mutex_enter(&zfs_dbgmsgs_lock);
	list_insert_tail(&zfs_dbgmsgs, zdm);
	zfs_dbgmsg_size += size;
	zfs_dbgmsg_purge(MAX(zfs_dbgmsg_maxsize, 0));
	mutex_exit(&zfs_dbgmsgs_lock);
}

#ifdef _KERNEL
void
__dprintf(boolean_t dprint, const char *file, const char *func,
    int line, const char *fmt, ...)
{
	int size, i;
	va_list adx;
	char *buf, *nl;
	char *prefix = (dprint) ? "dprintf: " : "";
	const char *newfile;

	/*
	 * Skip everything if we can't write to the debug log due to
	 * being in a DPC.
	 */
	if (KeGetCurrentIrql() >= DISPATCH_LEVEL)
		return;

	/*
	 * Get rid of annoying prefix to filename.
	 */
	newfile = strrchr(file, '/');
	if (newfile != NULL) {
		newfile = newfile + 1; /* Get rid of leading / */
	} else {
		newfile = file;
	}
	newfile = strrchr(file, '\\');
	if (newfile != NULL) {
		newfile = newfile + 1; /* Get rid of leading / */
	} else {
		newfile = file;
	}

	/*
	 * This logger is reachable from inside the kmem/vmem allocators
	 * themselves (spl-kmem.c and spl-vmem.c call dprintf() in many places,
	 * including kmem_error()). It must therefore stay to a single bounded
	 * allocation: no grow-and-retry, no helper that allocates more than
	 * once. Do not "improve" this into kmem_vasprintf()/kmem_asprintf().
	 *
	 * zfs_vsnprintf()'s measuring path (size == 0) returns a length capped
	 * by zfs_vscprintf()'s scratch buffer, so a longer message is simply
	 * truncated below. That is safe because every write is bounded by the
	 * real remaining capacity, and the free uses the same `size` as the
	 * allocation - a capped measurement only ever costs message text.
	 */
	va_start(adx, fmt);
	size = zfs_vsnprintf(NULL, 0, fmt, adx);
	va_end(adx);

	size += snprintf(NULL, 0, "%s%s:%d:%s(): ", prefix, newfile, line,
	    func);

	size++;			/* terminating null */

	buf = kmem_alloc(size, KM_SLEEP);

	/*
	 * buf holds exactly `size` bytes, so both writes get the true
	 * remaining capacity. The previous code passed size + 1 here and
	 * size - i + 1 below, one byte more than existed in each case.
	 *
	 * i comes from strlen() rather than snprintf()'s return value: on
	 * truncation _vsnprintf_s returns -1, and buf + (-1) would be a wild
	 * pointer. After a truncating write the buffer is still
	 * null-terminated, so strlen() is always the real prefix length and
	 * always leaves size - i >= 1.
	 */
	va_start(adx, fmt);
	(void) snprintf(buf, size, "%s%s:%d:%s(): ", prefix, newfile, line,
	    func);
	i = (int)strlen(buf);
	(void) zfs_vsnprintf(buf + i, size - i, fmt, adx);
	va_end(adx);

	/*
	 * Get rid of trailing newline for dprintf logs.
	 */
	if (dprint && buf[0] != '\0') {
		nl = &buf[strlen(buf) - 1];
		if (*nl == '\n')
			*nl = '\0';
	}

	DTRACE_PROBE1(zfs__dbgmsg, char *, zdm->zdm_msg);

	__zfs_dbgmsg(buf);

	/* Also emit string to log/console */
	printBuffer("%s\n", buf);

	kmem_free(buf, size);
}

#else

#define	printBuffer printf

#endif

void
zfs_dbgmsg_print(const char *tag)
{
	zfs_dbgmsg_t *zdm;

	(void) printBuffer("ZFS_DBGMSG(%s):\n", tag);
	mutex_enter(&zfs_dbgmsgs_lock);
	for (zdm = list_head(&zfs_dbgmsgs); zdm;
	    zdm = list_next(&zfs_dbgmsgs, zdm))
		(void) printBuffer("%s\n", zdm->zdm_msg);
	mutex_exit(&zfs_dbgmsgs_lock);
}
