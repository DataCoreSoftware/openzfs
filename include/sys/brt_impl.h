/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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

#ifndef _SYS_BRT_IMPL_H
#define	_SYS_BRT_IMPL_H

#include <sys/brt.h>
#include <sys/avl.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct brt_vdev {
	uint64_t	bv_vdevid;
	boolean_t	bv_initiated;
	uint64_t	bv_mos_brtvdev;
	uint64_t	bv_mos_entries;
	avl_tree_t	bv_tree;
	boolean_t	bv_need_byteswap;
	uint64_t	bv_size;
	uint16_t	*bv_entcount;
	uint64_t	bv_totalcount;
	uint64_t	bv_usedspace;
	uint64_t	bv_savedspace;
	boolean_t	bv_meta_dirty;
	boolean_t	bv_entcount_dirty;
	ulong_t		*bv_bitmap;
	uint64_t	bv_nblocks;
} brt_vdev_t;

typedef struct brt {
	krwlock_t	brt_lock;
	spa_t		*brt_spa;
#define	brt_mos		brt_spa->spa_meta_objset
	uint64_t	brt_rangesize;
	uint64_t	brt_usedspace;
	uint64_t	brt_savedspace;
	avl_tree_t	brt_pending_tree[TXG_SIZE];
	kmutex_t	brt_pending_lock[TXG_SIZE];
	uint64_t	brt_nentries;
	brt_vdev_t	*brt_vdevs;
	uint64_t	brt_nvdevs;
} brt_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_BRT_IMPL_H */
