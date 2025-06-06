// Copyright (C) 2004-2025 Artifex Software, Inc.
//
// This file is part of MuPDF.
//
// MuPDF is free software: you can redistribute it and/or modify it under the
// terms of the GNU Affero General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// MuPDF is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
// details.
//
// You should have received a copy of the GNU Affero General Public License
// along with MuPDF. If not, see <https://www.gnu.org/licenses/agpl-3.0.en.html>
//
// Alternative licensing terms are available from the licensor.
// For commercial licensing, see <https://www.artifex.com/> or contact
// Artifex Software, Inc., 39 Mesa Street, Suite 108A, San Francisco,
// CA 94129, USA, for further information.

#include "mupdf/fitz.h"

#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

/* Enable FITZ_DEBUG_LOCKING_TIMES below if you want to check the times
 * for which locks are held too. */
#ifdef FITZ_DEBUG_LOCKING
#undef FITZ_DEBUG_LOCKING_TIMES
#endif

/*
 * The malloc family of functions will always try scavenging when they run out of memory.
 * They will only fail when scavenging cannot free up memory from caches in the fz_context.
 * All the functions will throw an exception when no memory can be allocated,
 * except the _no_throw family which instead silently returns NULL.
 */

static void *
do_scavenging_malloc(fz_context *ctx, size_t size)
{
	void *p;
	int phase = 0;

	fz_lock(ctx, FZ_LOCK_ALLOC);
	do {
		p = ctx->alloc.malloc(ctx->alloc.user, size);
		if (p != NULL)
		{
			fz_unlock(ctx, FZ_LOCK_ALLOC);
			return p;
		}
	} while (fz_store_scavenge(ctx, size, &phase));
	fz_unlock(ctx, FZ_LOCK_ALLOC);

	return NULL;
}

static void *
do_scavenging_realloc(fz_context *ctx, void *p, size_t size)
{
	void *q;
	int phase = 0;

	fz_lock(ctx, FZ_LOCK_ALLOC);
	do {
		q = ctx->alloc.realloc(ctx->alloc.user, p, size);
		if (q != NULL)
		{
			fz_unlock(ctx, FZ_LOCK_ALLOC);
			return q;
		}
	} while (fz_store_scavenge(ctx, size, &phase));
	fz_unlock(ctx, FZ_LOCK_ALLOC);

	return NULL;
}

void *
fz_malloc(fz_context *ctx, size_t size)
{
	void *p;
	if (size == 0)
		return NULL;
	p = do_scavenging_malloc(ctx, size);
	if (!p)
	{
		errno = ENOMEM;
		fz_throw(ctx, FZ_ERROR_SYSTEM, "malloc (%zu bytes) failed", size);
	}
	return p;
}

void *
fz_malloc_no_throw(fz_context *ctx, size_t size)
{
	if (size == 0)
		return NULL;
	return do_scavenging_malloc(ctx, size);
}

void *
fz_calloc(fz_context *ctx, size_t count, size_t size)
{
	void *p;
	if (count == 0 || size == 0)
		return NULL;
	if (count > SIZE_MAX / size)
		fz_throw(ctx, FZ_ERROR_LIMIT, "calloc (%zu x %zu bytes) failed (size_t overflow)", count, size);
	p = do_scavenging_malloc(ctx, count * size);
	if (!p)
	{
		errno = ENOMEM;
		fz_throw(ctx, FZ_ERROR_SYSTEM, "calloc (%zu x %zu bytes) failed", count, size);
	}
	memset(p, 0, count*size);
	return p;
}

void *
fz_calloc_no_throw(fz_context *ctx, size_t count, size_t size)
{
	void *p;
	if (count == 0 || size == 0)
		return NULL;
	if (count > SIZE_MAX / size)
		return NULL;
	p = do_scavenging_malloc(ctx, count * size);
	if (p)
		memset(p, 0, count * size);
	return p;
}

void *
fz_realloc(fz_context *ctx, void *p, size_t size)
{
	if (size == 0)
	{
		fz_free(ctx, p);
		return NULL;
	}
	p = do_scavenging_realloc(ctx, p, size);
	if (!p)
	{
		errno = ENOMEM;
		fz_throw(ctx, FZ_ERROR_SYSTEM, "realloc (%zu bytes) failed", size);
	}
	return p;
}

void *
fz_realloc_no_throw(fz_context *ctx, void *p, size_t size)
{
	if (size == 0)
	{
		fz_free(ctx, p);
		return NULL;
	}
	return do_scavenging_realloc(ctx, p, size);
}

void
fz_free(fz_context *ctx, void *p)
{
	if (p)
	{
		fz_lock(ctx, FZ_LOCK_ALLOC);
		ctx->alloc.free(ctx->alloc.user, p);
		fz_unlock(ctx, FZ_LOCK_ALLOC);
	}
}

/* align is assumed to be a power of 2. */
void *fz_malloc_aligned(fz_context *ctx, size_t size, int align)
{
	uint8_t *block;
	uint8_t *aligned;

	if (size == 0)
		return NULL;

	if (align >= 256)
		fz_throw(ctx, FZ_ERROR_ARGUMENT, "Alignment too large");
	if ((align & (align-1)) != 0)
		fz_throw(ctx, FZ_ERROR_ARGUMENT, "Alignment must be a power of 2");

	block = fz_malloc(ctx, size + align);

	aligned = (void *)((intptr_t)(block + align-1) & ~(align-1));
	if (aligned == block)
		aligned = block + align;
	memset(block, aligned-block, aligned-block);

	return aligned;
}

void fz_free_aligned(fz_context *ctx, void *ptr)
{
	uint8_t *block = ptr;

	if (ptr == NULL)
		return;

	block -= block[-1];

	fz_free(ctx, block);
}

char *
fz_strdup(fz_context *ctx, const char *s)
{
	size_t len = strlen(s) + 1;
	char *ns = fz_malloc(ctx, len);
	memcpy(ns, s, len);
	return ns;
}

fz_string *
fz_new_string(fz_context *ctx, const char *s)
{
	fz_string *str = fz_malloc_flexible(ctx, fz_string, str, strlen(s) + 1);
	str->refs = 1;
	strcpy(str->str, s);
	return str;
}

fz_string *fz_keep_string(fz_context *ctx, fz_string *str)
{
	return fz_keep_imp(ctx, str, &str->refs);
}

void fz_drop_string(fz_context *ctx, fz_string *str)
{
	if (fz_drop_imp(ctx, str, &str->refs))
		fz_free(ctx, str);
}


static void *
fz_malloc_default(void *opaque, size_t size)
{
	return malloc(size);
}

static void *
fz_realloc_default(void *opaque, void *old, size_t size)
{
	return realloc(old, size);
}

static void
fz_free_default(void *opaque, void *ptr)
{
	free(ptr);
}

fz_alloc_context fz_alloc_default =
{
	NULL,
	fz_malloc_default,
	fz_realloc_default,
	fz_free_default
};

static void
fz_lock_default(void *user, int lock)
{
}

static void
fz_unlock_default(void *user, int lock)
{
}

fz_locks_context fz_locks_default =
{
	NULL,
	fz_lock_default,
	fz_unlock_default
};

#ifdef FITZ_DEBUG_LOCKING

enum
{
	FZ_LOCK_DEBUG_CONTEXT_MAX = 100
};

fz_context *fz_lock_debug_contexts[FZ_LOCK_DEBUG_CONTEXT_MAX];
int fz_locks_debug[FZ_LOCK_DEBUG_CONTEXT_MAX][FZ_LOCK_MAX];

#ifdef FITZ_DEBUG_LOCKING_TIMES

int fz_debug_locking_inited = 0;
int fz_lock_program_start;
int fz_lock_time[FZ_LOCK_DEBUG_CONTEXT_MAX][FZ_LOCK_MAX] = { { 0 } };
int fz_lock_taken[FZ_LOCK_DEBUG_CONTEXT_MAX][FZ_LOCK_MAX] = { { 0 } };

/* We implement our own millisecond clock, as clock() cannot be trusted
 * when threads are involved. */
static int ms_clock(void)
{
#ifdef _WIN32
	return (int)GetTickCount();
#else
	struct timeval tp;
	gettimeofday(&tp, NULL);
	return (tp.tv_sec*1000) + (tp.tv_usec/1000);
#endif
}

static void dump_lock_times(void)
{
	int i, j;
	int prog_time = ms_clock() - fz_lock_program_start;

	for (j = 0; j < FZ_LOCK_MAX; j++)
	{
		int total = 0;
		for (i = 0; i < FZ_LOCK_DEBUG_CONTEXT_MAX; i++)
		{
			total += fz_lock_time[i][j];
		}
		printf("Lock %d held for %g seconds (%g%%)\n", j, total / 1000.0f, 100.0f*total/prog_time);
	}
	printf("Total program time %g seconds\n", prog_time / 1000.0f);
}

#endif

static int find_context(fz_context *ctx)
{
	int i;

	for (i = 0; i < FZ_LOCK_DEBUG_CONTEXT_MAX; i++)
	{
		if (fz_lock_debug_contexts[i] == ctx)
			return i;
		if (fz_lock_debug_contexts[i] == NULL)
		{
			int gottit = 0;
			/* We've not locked on this context before, so use
			 * this one for this new context. We might have other
			 * threads trying here too though so, so claim it
			 * atomically. No one has locked on this context
			 * before, so we are safe to take the ALLOC lock. */
			ctx->locks.lock(ctx->locks.user, FZ_LOCK_ALLOC);
			/* If it's still free, then claim it as ours,
			 * otherwise we'll keep hunting. */
			if (fz_lock_debug_contexts[i] == NULL)
			{
				gottit = 1;
				fz_lock_debug_contexts[i] = ctx;
#ifdef FITZ_DEBUG_LOCKING_TIMES
				if (fz_debug_locking_inited == 0)
				{
					fz_debug_locking_inited = 1;
					fz_lock_program_start = ms_clock();
					atexit(dump_lock_times);
				}
#endif
			}
			ctx->locks.unlock(ctx->locks.user, FZ_LOCK_ALLOC);
			if (gottit)
				return i;
		}
	}
	return -1;
}

void
fz_assert_lock_held(fz_context *ctx, int lock)
{
	int idx;

	if (ctx->locks.lock != fz_lock_default)
		return;

	idx = find_context(ctx);
	if (idx < 0)
		return;

	if (fz_locks_debug[idx][lock] == 0)
		fprintf(stderr, "Lock %d not held when expected\n", lock);
}

void
fz_assert_lock_not_held(fz_context *ctx, int lock)
{
	int idx;

	if (ctx->locks.lock != fz_lock_default)
		return;

	idx = find_context(ctx);
	if (idx < 0)
		return;

	if (fz_locks_debug[idx][lock] != 0)
		fprintf(stderr, "Lock %d held when not expected\n", lock);
}

void fz_lock_debug_lock(fz_context *ctx, int lock)
{
	int i, idx;

	if (ctx->locks.lock != fz_lock_default)
		return;

	idx = find_context(ctx);
	if (idx < 0)
		return;

	if (fz_locks_debug[idx][lock] != 0)
	{
		fprintf(stderr, "Attempt to take lock %d when held already!\n", lock);
	}
	for (i = lock-1; i >= 0; i--)
	{
		if (fz_locks_debug[idx][i] != 0)
		{
			fprintf(stderr, "Lock ordering violation: Attempt to take lock %d when %d held already!\n", lock, i);
		}
	}
	fz_locks_debug[idx][lock] = 1;
#ifdef FITZ_DEBUG_LOCKING_TIMES
	fz_lock_taken[idx][lock] = ms_clock();
#endif
}

void fz_lock_debug_unlock(fz_context *ctx, int lock)
{
	int idx;

	if (ctx->locks.lock != fz_lock_default)
		return;

	idx = find_context(ctx);
	if (idx < 0)
		return;

	if (fz_locks_debug[idx][lock] == 0)
	{
		fprintf(stderr, "Attempt to release lock %d when not held!\n", lock);
	}
	fz_locks_debug[idx][lock] = 0;
#ifdef FITZ_DEBUG_LOCKING_TIMES
	fz_lock_time[idx][lock] += ms_clock() - fz_lock_taken[idx][lock];
#endif
}

#else

void
(fz_assert_lock_held)(fz_context *ctx, int lock)
{
}

void
(fz_assert_lock_not_held)(fz_context *ctx, int lock)
{
}

void (fz_lock_debug_lock)(fz_context *ctx, int lock)
{
}

void (fz_lock_debug_unlock)(fz_context *ctx, int lock)
{
}

#endif
