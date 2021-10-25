#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef __cplusplus
# include <stdatomic.h>
#else
# include <atomic>
# define _Atomic(X) std::atomic<X>
#endif

#include "ztdbits.h"

/*
 * muvec implementation
 */

enum { mu_vec_max_extents = 48 };

struct mu_vec {
    size_t capacity;
    size_t count;
	void * extents[mu_vec_max_extents];
};

typedef signed long long mu_index_t;

static mu_index_t _mu_vec_extent_num(mu_index_t x) { return (64 - clz(x+1)) - 1; }
static mu_index_t _mu_vec_extent_base(mu_index_t x) { return (1llu << x) - 1; }
static mu_index_t _mu_vec_extent_size(mu_index_t x) { return 1llu << x; }

static void _mu_vec_extent_alloc(mu_vec *mv, size_t stride, size_t extent)
{
	mu_index_t extent_size = _mu_vec_extent_size(extent);
	void *extent_mem = malloc(extent_size * stride);
	void *extent_empty = NULL;
	if (!atomic_compare_exchange_weak((_Atomic(void*)*)(mv->extents + extent),
			&extent_empty, extent_mem)) {
		free(extent_mem);
	}
}

static void _mu_vec_ensure_extents(mu_vec *mv, size_t stride, size_t min_extent, size_t max_extent)
{
	for (mu_index_t extent = min_extent; extent <= max_extent; extent++) {
		if (atomic_load((_Atomic(void*)*)(mv->extents + extent)) == NULL) {
			_mu_vec_extent_alloc(mv, stride, extent);
		}
	}
	size_t new_limit = _mu_vec_extent_size(max_extent + 1) - 1;
	for (;;) {
		size_t limit = atomic_load((_Atomic(size_t)*)&mv->capacity);
		if (limit >= new_limit) break;
		if (atomic_compare_exchange_weak((_Atomic(size_t)*)&mv->capacity,
			&limit, new_limit)) break;
	}
}

static void _mu_vec_ensure_range(mu_vec *mv, size_t stride, size_t idx, size_t count)
{
    size_t min_extent = _mu_vec_extent_num(idx);
    size_t max_extent = _mu_vec_extent_num(idx + count - 1);
	_mu_vec_ensure_extents(mv, stride, min_extent, max_extent);
}

static void mu_vec_init(mu_vec *mv, size_t stride, size_t limit)
{
    memset(mv, 0, sizeof(mu_vec));
    if (limit > 0) {
	    _mu_vec_ensure_extents(mv, stride, 0, _mu_vec_extent_num(limit - 1));
	}
}

static void mu_vec_resize(mu_vec *mv, size_t stride, size_t limit)
{
	if (limit > mv->capacity) {
	    _mu_vec_ensure_extents(mv, stride, 0, _mu_vec_extent_num(limit - 1));
	}
}

static void mu_vec_destroy(mu_vec *mv)
{
	size_t limit = atomic_load((_Atomic(size_t)*)&mv->capacity);
	mu_index_t max_extent = limit > 0 ? _mu_vec_extent_num(limit - 1) : -1;
	for (mu_index_t extent = 0; extent <= max_extent; extent++) {
		void *extent_mem = atomic_load((_Atomic(void*)*)(mv->extents + extent));
		if (extent_mem) {
			if (atomic_compare_exchange_weak((_Atomic(void*)*)(mv->extents + extent),
					&extent_mem, NULL)) {
				free(extent_mem);
			}
		}
	}
}

static size_t mu_vec_count(mu_vec *mv)
{
    return mv->count;
}

static size_t mu_vec_size(mu_vec *mv, size_t stride)
{
    return mv->count * stride;
}

static size_t mu_vec_capacity(mu_vec *mv, size_t stride)
{
    return mv->capacity * stride;
}

static int mu_vec_linear(mu_vec *mv, size_t idx, size_t count)
{
	return _mu_vec_extent_num(idx) == _mu_vec_extent_num(idx + count - 1);
}

static void* mu_vec_get(mu_vec *mv, size_t stride, size_t idx)
{
	mu_index_t extent = _mu_vec_extent_num(idx);
	mu_index_t base = _mu_vec_extent_base(extent);
	void *extent_mem = atomic_load((_Atomic(void*)*)(mv->extents + extent));
    return (char*)extent_mem + (idx - base) * stride;
}

static void mu_vec_set(mu_vec *mv, size_t stride, size_t idx, void *ptr)
{
    size_t extent = _mu_vec_extent_num(idx);
	_mu_vec_ensure_extents(mv, stride, extent, extent);
	mu_index_t base = _mu_vec_extent_base(extent);
	void *extent_mem = atomic_load((_Atomic(void*)*)(mv->extents + extent));
    void *dest = (char*)extent_mem + (idx - base) * stride;
    memcpy(dest, ptr, stride);
}

/*
 * current main use of muvec is for immovability not atomicity.
 * there must be only a single writer and writes to the arrays
 * will eventually be revealed to other threads. recommended to
 * communicate the bounds out-of-band because the counter can
 * be incremented before the array contents have been written.
 */

static size_t mu_vec_alloc_atomic(mu_vec *mv, size_t stride, size_t count)
{
    size_t idx = atomic_fetch_add((_Atomic(size_t)*)&mv->count, count);
    _mu_vec_ensure_range(mv, stride, idx, count);
    return idx;
}

static size_t mu_vec_alloc_relaxed(mu_vec *mv, size_t stride, size_t count)
{
    size_t idx = mv->count;
    mv->count += count;
    _mu_vec_ensure_range(mv, stride, idx, count);
    return idx;
}

static size_t mu_vec_add_atomic(mu_vec *mv, size_t stride, void *ptr)
{
    size_t idx = atomic_fetch_add((_Atomic(size_t)*)&mv->count, 1);
    mu_vec_set(mv, stride, idx, ptr);
    return idx;
}

static size_t mu_vec_add_relaxed(mu_vec *mv, size_t stride, void *ptr)
{
    size_t idx = mv->count;
    mv->count++;
    mu_vec_set(mv, stride, idx, ptr);
    return idx;
}
