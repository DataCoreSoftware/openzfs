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

#ifndef _SPL_BITMAP_H
#define	_SPL_BITMAP_H

/*
 * Windows: delegate to canonical bitmap macros from include/sys/bitmap.h
 * (the empty stub here was shadowing it; BRT and other 2.4 code need BT_*)
 */
#ifdef _LP64
#define	BT_ULSHIFT	6
#define	BT_ULSHIFT32	5
#else
#define	BT_ULSHIFT	5
#endif

#define	BT_NBIPUL	(1 << BT_ULSHIFT)
#define	BT_ULMASK	(BT_NBIPUL - 1)

#define	BT_WIM(bitmap, bitindex) \
	((bitmap)[(bitindex) >> BT_ULSHIFT])
#define	BT_BIW(bitindex) \
	(1UL << ((bitindex) & BT_ULMASK))

#define	BT_BITOUL(nbits) \
	(((nbits) + BT_NBIPUL - 1l) / BT_NBIPUL)
#define	BT_SIZEOFMAP(nbits) \
	(BT_BITOUL(nbits) * sizeof (ulong_t))
#define	BT_TEST(bitmap, bitindex) \
	((BT_WIM((bitmap), (bitindex)) & BT_BIW(bitindex)) ? 1 : 0)
#define	BT_SET(bitmap, bitindex) \
	{ BT_WIM((bitmap), (bitindex)) |= BT_BIW(bitindex); }
#define	BT_CLEAR(bitmap, bitindex) \
	{ BT_WIM((bitmap), (bitindex)) &= ~BT_BIW(bitindex); }

#endif /* _SPL_BITMAP_H */
