// SPDX-License-Identifier: GPL-2.0-only
/*
 * @author Ammar Faizi <ammarfaizi2@gmail.com> https://www.facebook.com/ammarfaizi2
 * @license GPL-2.0-only
 * @package tgvisd
 *
 * Copyright (C) 2021 Ammar Faizi <ammarfaizi2@gmail.com>
 */

#ifndef TGVISD__COMMON_HPP
#define TGVISD__COMMON_HPP

#include <cstdio>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdbool>

#ifndef unlikely
#  define unlikely(X) __builtin_expect((bool)(X), 0)
#endif

#ifndef likely
#  define likely(X) __builtin_expect((bool)(X), 1)
#endif

#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wreserved-id-macro"
#endif

#ifndef ____stringify
#  define ____stringify(EXPR) #EXPR
#endif

#ifndef __stringify
#  define __stringify(EXPR) ____stringify(EXPR)
#endif

#ifndef __acquires
#  define __acquires(LOCK)
#endif

#ifndef __releases
#  define __releases(LOCK)
#endif

#ifndef __must_hold
#  define __must_hold(LOCK)
#endif

#ifndef __hot
#  define __hot __attribute__((__hot__))
#endif

#ifndef __cold
#  define __cold __attribute__((__cold__))
#endif

#ifndef offsetof
#  define offsetof(TYPE, FIELD) ((size_t) &((TYPE *)0)->FIELD)
#endif

#ifndef container_of
#  define container_of(PTR, TYPE, FIELD) ({			\
	__typeof__(((TYPE *)0)->FIELD) *__FIELD_PTR = (PTR);	\
	(TYPE *)((char *) __FIELD_PTR - offsetof(TYPE, FIELD));	\
})
#endif

static __always_inline void cpu_relax(void)
{
	__asm__ volatile("pause");
}

#define ZSTRL(STR) STR, sizeof(STR) - 1

#include <tgvisd/print.h>

#endif /* #ifndef TGVISD__COMMON_HPP */
