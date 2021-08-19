#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct mu_vec;
typedef struct mu_vec mu_vec;

/*
 * muvec interface
 */

static void mu_vec_init(mu_vec *mv, size_t stride, size_t capacity);
static void mu_vec_resize(mu_vec *mv, size_t stride, size_t count);
static void mu_vec_destroy(mu_vec *mv);
static size_t mu_vec_count(mu_vec *mv);
static size_t mu_vec_size(mu_vec *mv, size_t stride);
static size_t mu_vec_capacity(mu_vec *mv, size_t stride);
static int mu_vec_linear(mu_vec *mv, size_t idx, size_t count);
static void* mu_vec_get(mu_vec *mv, size_t stride, size_t idx);
static void mu_vec_set(mu_vec *mv, size_t stride, size_t idx, void *ptr);
static size_t mu_vec_alloc_atomic(mu_vec *mv, size_t stride, size_t count);
static size_t mu_vec_alloc_relaxed(mu_vec *mv, size_t stride, size_t count);
static size_t mu_vec_add_atomic(mu_vec *mv, size_t stride, void *ptr);
static size_t mu_vec_add_relaxed(mu_vec *mv, size_t stride, void *ptr);

#include "muvec.c"

#ifdef __cplusplus
}
#endif
