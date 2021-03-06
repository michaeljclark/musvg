/*
 * musvg
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software
 * in a product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * The SVG parser is based on Anti-Grain Geometry 2.4 SVG example
 * Copyright (C) 2002-2004 Maxim Shemanarev (McSeem) (http://www.antigrain.com/)
 * Copyright (c) 2013-14 Mikko Mononen memon@inside.org (https://github.com/memononen/nanosvg)
 * Copyright (c) 2021 Michael Clark <michaeljclark@mac.com>
 */

#undef NDEBUG
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdalign.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <ctype.h>
#include <threads.h>

#ifndef _WIN32
#include <alloca.h>
#else
#define alloca _alloca
#endif

#include "blake3.h"
#include "sha256.h"
#include "sha512.h"
#include "ztdbits.h"
#include "mubuf.h"
#include "muvec.h"
#include "mumule.h"
#include "musvg.h"

#ifndef M_PI
    #define M_PI 3.14159265358979323846264338327
#endif

#define MUSVG_BUFFER_MEMSET 0
#define USE_MUVEC 1
#define USE_BLAKE3 1
#define USE_SHA256 0

#if USE_BLAKE3
#define mu_hash_len BLAKE3_OUT_LEN
#define mu_hash_ctx blake3_hasher
#define mu_hash_init(ctx) blake3_hasher_init(ctx);
#define mu_hash_update(ctx,buf,len) blake3_hasher_update(ctx,buf,len);
#define mu_hash_final(ctx,sum) blake3_hasher_finalize(ctx,sum,BLAKE3_OUT_LEN);
#endif
#if USE_SHA256
#define mu_hash_len sha256_hash_size
#define mu_hash_ctx sha256_ctx
#define mu_hash_init(ctx) sha256_init(ctx);
#define mu_hash_update(ctx,buf,len) sha256_update(ctx,buf,len);
#define mu_hash_final(ctx,sum) sha256_final(ctx,sum);
#endif

// Array buffer

typedef struct array_buffer array_buffer;

struct array_buffer
{
    size_t capacity;
    size_t count;
    char *data;
};

static void array_buffer_init(array_buffer *sb, size_t stride, size_t capacity)
{
    sb->capacity = capacity;
    sb->count = 0;
    sb->data = (char*)malloc(stride * sb->capacity);
#if MUSVG_BUFFER_MEMSET
    memset(sb->data, 0, stride * sb->capacity);
#endif
}

static void array_buffer_destroy(array_buffer *sb)
{
    free(sb->data);
    sb->data = NULL;
}

static size_t array_buffer_count(array_buffer *sb)
{
    return sb->count;
}

static size_t array_buffer_size(array_buffer *sb, size_t stride)
{
    return sb->count * stride;
}

static size_t array_buffer_capacity(array_buffer *sb, size_t stride)
{
    return sb->capacity * stride;
}

static void* array_buffer_get(array_buffer *sb, size_t stride, size_t idx)
{
    return sb->data + idx * stride;
}

static int array_buffer_linear(array_buffer *sb, size_t idx, size_t count)
{
    return 1;
}

static void array_buffer_resize(array_buffer *sb, size_t stride, size_t count)
{
    if (count > sb->capacity) {
        size_t new_capacity = pow2_ge(count);
        sb->data = (char*)realloc(sb->data, stride * new_capacity);
#if MUSVG_BUFFER_MEMSET
        memset(sb->data + stride * sb->capacity, 0, stride * (new_capacity - sb->capacity));
#endif
        sb->capacity = new_capacity;
    }
}

static size_t array_buffer_alloc(array_buffer *sb, size_t stride, size_t count)
{
    array_buffer_resize(sb, stride, sb->count + count);
    size_t idx = sb->count;
    sb->count += count;
    return idx;
}

static size_t array_buffer_add(array_buffer *sb, size_t stride, void *ptr)
{
    size_t idx = array_buffer_alloc(sb, stride, 1);
    memcpy(sb->data + (idx * stride), ptr, stride);
    return idx;
}

// Storage buffer

typedef struct storage_buffer storage_buffer;

struct storage_buffer
{
    size_t capacity;
    size_t offset;
    char *data;
};

static void storage_buffer_init(storage_buffer *sb, size_t capacity)
{
    sb->capacity = capacity;
    sb->offset = 0;
    sb->data = (char*)malloc(sb->capacity);
#if MUSVG_BUFFER_MEMSET
    memset(sb->data, 0, sb->capacity);
#endif
}

static void storage_buffer_destroy(storage_buffer *sb)
{
    free(sb->data);
    sb->data = NULL;
}

static size_t storage_buffer_size(storage_buffer *sb)
{
    return sb->offset;
}

static size_t storage_buffer_capacity(storage_buffer *sb)
{
    return sb->capacity;
}

static void* storage_buffer_get(storage_buffer *sb, size_t idx)
{
    return sb->data + idx;
}

static void storage_buffer_resize(storage_buffer *sb, size_t offset)
{
    if (offset > sb->capacity) {
        size_t new_size = pow2_ge(offset);
        sb->data = (char*)realloc(sb->data, new_size);
#if MUSVG_BUFFER_MEMSET
        memset(sb->data + sb->capacity, 0, new_size - sb->capacity);
#endif
        sb->capacity = new_size;
    }
}

static musvg_index storage_buffer_alloc(storage_buffer *sb, size_t size, size_t align)
{
    size_t offset = sb->offset, max_align = align > 8 ? 8 : align;
    size_t our_offset = (offset + max_align - 1) & ~(max_align - 1);
    size_t align_size = (size   + max_align - 1) & ~(max_align - 1);
    storage_buffer_resize(sb, our_offset + align_size);
    sb->offset = our_offset + align_size;
    return our_offset;
}

// SVG parser init

#if USE_MUVEC
#define vec mu_vec
#define vec_init(p,stride,size)      mu_vec_init(p,stride,size)
#define vec_resize(p,stride,count)   mu_vec_resize(p,stride,count)
#define vec_destroy(p)               mu_vec_destroy(p)
#define vec_count(p)                 mu_vec_count(p)
#define vec_size(p,stride)           mu_vec_size(p,stride)
#define vec_capacity(p,stride)       mu_vec_capacity(p,stride)
#define vec_linear(p,idx,count)      mu_vec_linear(p,idx,count)
#define vec_get(p,stride,idx)        mu_vec_get(p,stride,idx)
#define vec_set(p,stride,idx,ptr)    mu_vec_set(p,stride,idx,ptr)
#define vec_add(p,stride,ptr)        mu_vec_add_relaxed(p,stride,ptr)
#define vec_alloc(p,stride,count)    mu_vec_alloc_relaxed(p,stride,count)
#else
#define vec array_buffer
#define vec_init(p,stride,size)      array_buffer_init(p,stride,size)
#define vec_resize(p,stride,count)   array_buffer_resize(p,stride,count)
#define vec_destroy(p)               array_buffer_destroy(p)
#define vec_count(p)                 array_buffer_count(p)
#define vec_size(p,stride)           array_buffer_size(p,stride)
#define vec_capacity(p,stride)       array_buffer_capacity(p,stride)
#define vec_linear(p,idx,count)      array_buffer_linear(p,idx,count)
#define vec_get(p,stride,idx)        array_buffer_get(p,stride,idx)
#define vec_add(p,stride,ptr)        array_buffer_add(p,stride,ptr)
#define vec_alloc(p,stride,count)    array_buffer_alloc(p,stride,count)
#endif

#define points_init(p) vec_init(&p->points,sizeof(float),16)
#define points_destroy(p) vec_destroy(&p->points)
#define points_count(p) vec_count(&p->points)
#define points_size(p) vec_size(&p->points,sizeof(float))
#define points_capacity(p) vec_capacity(&p->points,sizeof(float))
#define points_linear(p,idx,count) vec_linear(&p->points,idx,count)
#define points_get(p,idx) ((float*)vec_get(&p->points,sizeof(float),idx))
#define points_add(p,ptr) vec_add(&p->points,sizeof(float),ptr)
#define points_alloc(p,count) vec_alloc(&p->points,sizeof(float),count)

#define path_ops_init(p) vec_init(&p->path_ops,sizeof(musvg_path_op),16)
#define path_ops_destroy(p) vec_destroy(&p->path_ops)
#define path_ops_count(p) vec_count(&p->path_ops)
#define path_ops_size(p) vec_size(&p->path_ops,sizeof(musvg_path_op))
#define path_ops_capacity(p) vec_capacity(&p->path_ops,sizeof(musvg_path_op))
#define path_ops_get(p,idx) ((musvg_path_op*)vec_get(&p->path_ops,sizeof(musvg_path_op),idx))
#define path_ops_add(p,ptr) vec_add(&p->path_ops,sizeof(musvg_path_op),ptr)

#define path_points_init(p) vec_init(&p->path_points,sizeof(musvg_points),16)
#define path_points_destroy(p) vec_destroy(&p->path_points)
#define path_points_count(p) vec_count(&p->path_points)
#define path_points_size(p) vec_size(&p->path_points,sizeof(musvg_points))
#define path_points_capacity(p) vec_capacity(&p->path_points,sizeof(musvg_points))
#define path_points_get(p,idx) ((musvg_points*)vec_get(&p->path_points,sizeof(musvg_points),idx))
#define path_points_add(p,ptr) vec_add(&p->path_points,sizeof(musvg_points),ptr)

#define brushes_init(p) vec_init(&p->brushes,sizeof(musvg_brush),16)
#define brushes_destroy(p) vec_destroy(&p->brushes)
#define brushes_count(p) vec_count(&p->brushes)
#define brushes_size(p) vec_size(&p->brushes,sizeof(musvg_brush))
#define brushes_capacity(p) vec_capacity(&p->brushes,sizeof(musvg_brush))
#define brushes_get(p,idx) ((musvg_brush*)vec_get(&p->brushes,sizeof(musvg_brush),idx))
#define brushes_add(p,ptr) vec_add(&p->brushes,sizeof(musvg_brush),ptr)

#define nodes_init(p) vec_init(&p->nodes,sizeof(musvg_node),16)
#define nodes_destroy(p) vec_destroy(&p->nodes)
#define nodes_count(p) vec_count(&p->nodes)
#define nodes_size(p) vec_size(&p->nodes,sizeof(musvg_node))
#define nodes_capacity(p) vec_capacity(&p->nodes,sizeof(musvg_node))
#define nodes_get(p,idx) ((musvg_node*)vec_get(&p->nodes,sizeof(musvg_node),idx))
#define nodes_alloc(p,count) vec_alloc(&p->nodes,sizeof(musvg_node),count)

#define hashes_init(p) vec_init(&p->hashes,sizeof(musvg_hash),16)
#define hashes_destroy(p) vec_destroy(&p->hashes)
#define hashes_count(p) vec_count(&p->hashes)
#define hashes_size(p) vec_size(&p->hashes,sizeof(musvg_hash))
#define hashes_capacity(p) vec_capacity(&p->hashes,sizeof(musvg_hash))
#define hashes_get(p,idx) ((musvg_hash*)vec_get(&p->hashes,sizeof(musvg_hash),idx))
#define hashes_resize(p,size) vec_resize(&p->hashes,sizeof(musvg_hash),size)

#define slots_init(p) vec_init(&p->slots,sizeof(musvg_slot),16)
#define slots_destroy(p) vec_destroy(&p->slots)
#define slots_count(p) vec_count(&p->slots)
#define slots_size(p) vec_size(&p->slots,sizeof(musvg_slot))
#define slots_capacity(p) vec_capacity(&p->slots,sizeof(musvg_slot))
#define slots_get(p,idx) ((musvg_slot*)vec_get(&p->slots,sizeof(musvg_slot),idx))
#define slots_add(p,ptr) vec_add(&p->slots,sizeof(musvg_slot),ptr)

#define storage_init(p) storage_buffer_init(&p->storage,16)
#define storage_destroy(p) storage_buffer_destroy(&p->storage)
#define storage_size(p) storage_buffer_size(&p->storage)
#define storage_capacity(p) storage_buffer_capacity(&p->storage)
#define storage_get(p,idx) ((char*)storage_buffer_get(&p->storage,idx))
#define storage_alloc(p,size,align) storage_buffer_alloc(&p->storage,size,align)

#define strings_init(p) storage_buffer_init(&p->strings,16)
#define strings_destroy(p) storage_buffer_destroy(&p->strings)
#define strings_size(p) storage_buffer_size(&p->strings)
#define strings_capacity(p) storage_buffer_capacity(&p->strings)
#define strings_get(p,idx) ((char*)storage_buffer_get(&p->strings,idx))
#define strings_alloc(p,size,align) storage_buffer_alloc(&p->strings,size,align)

static inline musvg_attr as_attr(int i) { return (musvg_attr)i; }

enum { musvg_max_depth = 256 };

// SVG parser

typedef struct musvg_slot musvg_slot;
typedef struct musvg_node musvg_node;
typedef struct musvg_hash musvg_hash;

struct musvg_slot
{
    uint type;                 /* attribute type */
    mnu_int48 storage;         /* index to storage space */
    mnu_int48 left;            /* index to sibling attribute */
};

struct musvg_node
{
    uint type;                 /* element type */
    mnu_int48 left;            /* index to sibling node */
    mnu_int48 down;            /* index to child node */
    mnu_int48 attr;            /* index to attribute slot */
    mnu_int48 up;              /* index to parent node */
};

struct musvg_hash
{
    uint8_t sum[mu_hash_len];
};

struct musvg_parser
{
    vec points;                /* polygon points */
    vec path_ops;              /* path ops */
    vec path_points;           /* path op points */
    vec brushes;               /* brushes*/
    vec nodes;                 /* node graph */
    vec hashes;                /* node hashes */
    vec slots;                 /* attribute storage slot linked list */
    storage_buffer storage;    /* aligned attribute value storage */
    storage_buffer strings;    /* variable length string storage */

    mu_mule mule;
    mu_hash_ctx hash_ctx;
    mu_buf *hash_buf;

    musvg_index node_stack[musvg_max_depth];
    uint node_depth;

    int (*f32_read)(mu_buf *buf, float *value);
    int (*f32_write)(mu_buf *buf, const float value);
    int (*f32_read_vec)(mu_buf *buf, float *value, size_t n);
    int (*f32_write_vec)(mu_buf *buf, const float *value, size_t n);
};

// parser common

static int musvg_isspace(char c)
{
    return strchr(" \t\n\v\f\r", c) != 0;
}

static int musvg_isdigit(char c)
{
    return c >= '0' && c <= '9';
}

// SVG tables

#define array_size(arr) ((sizeof(arr)/sizeof(arr[0])))

static const char * musvg_element_names[] = {
    [musvg_element_svg]                       = "svg",
    [musvg_element_g]                         = "g",
    [musvg_element_defs]                      = "defs",
    [musvg_element_path]                      = "path",
    [musvg_element_rect]                      = "rect",
    [musvg_element_circle]                    = "circle",
    [musvg_element_ellipse]                   = "ellipse",
    [musvg_element_line]                      = "line",
    [musvg_element_polyline]                  = "polyline",
    [musvg_element_polygon]                   = "polygon",
    [musvg_element_linear_gradient]           = "linearGradient",
    [musvg_element_radial_gradient]           = "radialGradient",
    [musvg_element_stop]                      = "stop",
};

static const char * musvg_attribute_names[] = {
    [musvg_attr_display]                      = "display",
    [musvg_attr_fill]                         = "fill",
    [musvg_attr_fill_opacity]                 = "fill-opacity",
    [musvg_attr_fill_rule]                    = "fill-rule",
    [musvg_attr_font_size]                    = "font-size",
    [musvg_attr_id]                           = "id",
    [musvg_attr_stroke]                       = "stroke",
    [musvg_attr_stroke_width]                 = "stroke-width",
    [musvg_attr_stroke_dasharray]             = "stroke-dasharray",
    [musvg_attr_stroke_dashoffset]            = "stroke-dashoffset",
    [musvg_attr_stroke_opacity]               = "stroke-opacity",
    [musvg_attr_stroke_linecap]               = "stroke-linecap",
    [musvg_attr_stroke_linejoin]              = "stroke-linejoin",
    [musvg_attr_stroke_miterlimit]            = "stroke-miterlimit",
    [musvg_attr_style]                        = "style",
    [musvg_attr_transform]                    = "transform",
    [musvg_attr_d]                            = "d",
    [musvg_attr_points]                       = "points",
    [musvg_attr_width]                        = "width",
    [musvg_attr_height]                       = "height",
    [musvg_attr_x]                            = "x",
    [musvg_attr_y]                            = "y",
    [musvg_attr_r]                            = "r",
    [musvg_attr_rx]                           = "rx",
    [musvg_attr_ry]                           = "ry",
    [musvg_attr_cx]                           = "cx",
    [musvg_attr_cy]                           = "cy",
    [musvg_attr_x1]                           = "x1",
    [musvg_attr_y1]                           = "y1",
    [musvg_attr_x2]                           = "x2",
    [musvg_attr_y2]                           = "y2",
    [musvg_attr_fx]                           = "fx",
    [musvg_attr_fy]                           = "fy",
    [musvg_attr_offset]                       = "offset",
    [musvg_attr_stop_color]                   = "stop-color",
    [musvg_attr_stop_opacity]                 = "stop-opacity",
    [musvg_attr_gradient_units]               = "gradientUnits",
    [musvg_attr_gradient_transform]           = "gradientTransform",
    [musvg_attr_spread_method]                = "spreadMethod",
    [musvg_attr_view_box]                     = "viewBox",
    [musvg_attr_preserve_aspect_ratio]        = "preserveAspectRatio",
    [musvg_attr_xmlns]                        = "xmlns",
    [musvg_attr_xmlns_xlink]                  = "xmlns:xlink",
    [musvg_attr_xlink_href]                   = "xlink:href",
};

static const char * musvg_path_op_names[] = {
    [musvg_path_none]                         = "none",
    [musvg_path_closepath]                    = "closepath",
    [musvg_path_moveto_abs]                   = "moveto_abs",
    [musvg_path_moveto_rel]                   = "moveto_rel",
    [musvg_path_lineto_abs]                   = "lineto_abs",
    [musvg_path_lineto_rel]                   = "lineto_rel",
    [musvg_path_curveto_cubic_abs]            = "curveto_cubic_abs",
    [musvg_path_curveto_cubic_rel]            = "curveto_cubic_rel",
    [musvg_path_quadratic_curve_to_abs]       = "quadratic_curve_to_abs",
    [musvg_path_quadratic_curve_to_rel]       = "quadratic_curve_to_rel",
    [musvg_path_eliptical_arc_abs]            = "eliptical_arc_abs",
    [musvg_path_eliptical_arc_rel]            = "eliptical_arc_rel",
    [musvg_path_line_to_horizontal_abs]       = "line_to_horizontal_abs",
    [musvg_path_line_to_horizontal_rel]       = "line_to_horizontal_rel",
    [musvg_path_line_to_vertical_abs]         = "line_to_vertical_abs",
    [musvg_path_line_to_vertical_rel]         = "line_to_vertical_rel",
    [musvg_path_curveto_cubic_smooth_abs]     = "curveto_cubic_smooth_abs",
    [musvg_path_curveto_cubic_smooth_rel]     = "curveto_cubic_smooth_rel",
    [musvg_path_curveto_quadratic_smooth_abs] = "curveto_quadratic_smooth_abs",
    [musvg_path_curveto_quadratic_smooth_rel] = "curveto_quadratic_smooth_rel",
};

static const char * musvg_brush_names[] = {
    [musvg_brush_color]                       = "color",
    [musvg_brush_linear_gradient]             = "linearGradient",
    [musvg_brush_radial_gradient]             = "radialGradient",
};

static const char * musvg_align_names[] = {
    [musvg_align_min]                         = "Min",
    [musvg_align_mid]                         = "Mid",
    [musvg_align_max]                         = "Max",
    [musvg_align_none]                        = "none",
};

static const char * musvg_crop_names[] = {
    [musvg_crop_meet]                         = "meet",
    [musvg_crop_slice]                        = "slice",
    [musvg_crop_none]                         = "none",
};

static const char * musvg_spread_method_names[] = {
    [musvg_spread_method_pad]                 = "pad",
    [musvg_spread_method_reflect]             = "reflect",
    [musvg_spread_method_repeat]              = "repeat",
};

static const char * musvg_gradient_unit_names[] = {
    [musvg_gradient_unit_user]                = "userSpaceOnUse",
    [musvg_gradient_unit_obb]                 = "objectBoundingBox",
};

static const char * musvg_linecap_names[] = {
    [musvg_linecap_butt]                      = "butt",
    [musvg_linecap_round]                     = "round",
    [musvg_linecap_square]                    = "square",
};

static const char * musvg_linejoin_names[] = {
    [musvg_linejoin_miter]                    = "miter",
    [musvg_linejoin_round]                    = "round",
    [musvg_linejoin_bevel]                    = "bevel",
};

static const char * musvg_fillrule_names[] = {
    [musvg_fillrule_nonzero]                  = "nonzero",
    [musvg_fillrule_evenodd]                  = "evenodd",
};

static const char * musvg_display_names[] = {
    [musvg_display_none]                      = "none",
    [musvg_display_inline]                    = "inline",
};

static const char * musvg_unit_names[] = {
    [musvg_unit_user]                         = "user",
    [musvg_unit_px]                           = "px",
    [musvg_unit_pt]                           = "pt",
    [musvg_unit_pc]                           = "pc",
    [musvg_unit_mm]                           = "mm",
    [musvg_unit_cm]                           = "cm",
    [musvg_unit_in]                           = "in",
    [musvg_unit_percent]                      = "%",
    [musvg_unit_em]                           = "em",
    [musvg_unit_ex]                           = "ex",
};

static const char * musvg_transform_names[] = {
    [musvg_transform_matrix]                  = "matrix",
    [musvg_transform_translate]               = "translate",
    [musvg_transform_scale]                   = "scale",
    [musvg_transform_rotate]                  = "rotate",
    [musvg_transform_skew_x]                  = "skewX",
    [musvg_transform_skew_y]                  = "skewY",
};

static const char* musvg_type_names[] = {
    [musvg_type_enum]                         = "enum",
    [musvg_type_id]                           = "id",
    [musvg_type_length]                       = "length",
    [musvg_type_color]                        = "color",
    [musvg_type_transform]                    = "transform",
    [musvg_type_dasharray]                    = "dasharray",
    [musvg_type_float]                        = "float",
    [musvg_type_viewbox]                      = "viewbox",
    [musvg_type_aspectratio]                  = "aspectratio",
    [musvg_type_path]                         = "path",
    [musvg_type_points]                       = "points",
};

static const musvg_type_t musvg_attr_types[] =
{
    /* common attributes */
    [musvg_attr_display]                      = musvg_type_enum,
    [musvg_attr_fill]                         = musvg_type_color,
    [musvg_attr_fill_opacity]                 = musvg_type_float,
    [musvg_attr_fill_rule]                    = musvg_type_enum,
    [musvg_attr_font_size]                    = musvg_type_length,
    [musvg_attr_id]                           = musvg_type_id,
    [musvg_attr_stroke]                       = musvg_type_color,
    [musvg_attr_stroke_width]                 = musvg_type_length,
    [musvg_attr_stroke_dashoffset]            = musvg_type_length,
    [musvg_attr_stroke_dasharray]             = musvg_type_dasharray,
    [musvg_attr_stroke_opacity]               = musvg_type_float,
    [musvg_attr_stroke_linecap]               = musvg_type_enum,
    [musvg_attr_stroke_linejoin]              = musvg_type_enum,
    [musvg_attr_stroke_miterlimit]            = musvg_type_float,
    [musvg_attr_style]                        = 0,
    [musvg_attr_transform]                    = musvg_type_transform,
    /* element specific attributes */
    [musvg_attr_d]                            = musvg_type_path,
    [musvg_attr_points]                       = musvg_type_points,
    [musvg_attr_width]                        = musvg_type_length,
    [musvg_attr_height]                       = musvg_type_length,
    [musvg_attr_x]                            = musvg_type_length,
    [musvg_attr_y]                            = musvg_type_length,
    [musvg_attr_r]                            = musvg_type_length,
    [musvg_attr_rx]                           = musvg_type_length,
    [musvg_attr_ry]                           = musvg_type_length,
    [musvg_attr_cx]                           = musvg_type_length,
    [musvg_attr_cy]                           = musvg_type_length,
    [musvg_attr_x1]                           = musvg_type_length,
    [musvg_attr_y1]                           = musvg_type_length,
    [musvg_attr_x2]                           = musvg_type_length,
    [musvg_attr_y2]                           = musvg_type_length,
    [musvg_attr_fx]                           = musvg_type_length,
    [musvg_attr_fy]                           = musvg_type_length,
    [musvg_attr_offset]                       = musvg_type_length,
    [musvg_attr_stop_color]                   = musvg_type_color,
    [musvg_attr_stop_opacity]                 = musvg_type_float,
    [musvg_attr_gradient_units]               = musvg_type_enum,
    [musvg_attr_gradient_transform]           = musvg_type_transform,
    [musvg_attr_spread_method]                = musvg_type_enum,
    [musvg_attr_view_box]                     = musvg_type_viewbox,
    [musvg_attr_preserve_aspect_ratio]        = musvg_type_aspectratio,
    [musvg_attr_xmlns]                        = musvg_type_id,
    [musvg_attr_xmlns_xlink]                  = musvg_type_id,
    [musvg_attr_xlink_href]                   = musvg_type_id,
};

musvg_small musvg_parse_linecap(const char* str);
musvg_small musvg_parse_linejoin(const char* str);
musvg_small musvg_parse_fillrule(const char* str);
musvg_small musvg_parse_display(const char* str);
musvg_small musvg_parse_spread_method(const char* str);
musvg_small musvg_parse_gradient_units(const char* str);

static const musvg_typeinfo_enum musvg_type_info_enum[] =
{
    [musvg_attr_stroke_linejoin]    = { musvg_linejoin_names,        musvg_linejoin_limit,        musvg_parse_linejoin },
    [musvg_attr_stroke_linecap]     = { musvg_linecap_names,         musvg_linecap_limit,         musvg_parse_linecap  },
    [musvg_attr_fill_rule]          = { musvg_fillrule_names,        musvg_fillrule_limit,        musvg_parse_fillrule },
    [musvg_attr_display]            = { musvg_display_names,         musvg_display_limit,         musvg_parse_display  },
    [musvg_attr_spread_method]      = { musvg_spread_method_names,   musvg_spread_method_limit,   musvg_parse_spread_method },
    [musvg_attr_gradient_units]     = { musvg_gradient_unit_names,   musvg_gradient_unit_limit,   musvg_parse_gradient_units  },
};

typedef struct { size_t size, align; } musvg_type_meta;

static const musvg_type_meta musvg_type_storage[] = {
    [musvg_type_enum]        = { sizeof(char),              alignof(char)              },
    [musvg_type_id]          = { sizeof(musvg_id),          alignof(musvg_id)          },
    [musvg_type_length]      = { sizeof(musvg_length),      alignof(musvg_length)      },
    [musvg_type_color]       = { sizeof(musvg_color),       alignof(musvg_color)       },
    [musvg_type_transform]   = { sizeof(musvg_transform),   alignof(musvg_transform)   },
    [musvg_type_dasharray]   = { sizeof(musvg_dasharray),   alignof(musvg_dasharray)   },
    [musvg_type_float]       = { sizeof(float),             alignof(float)             },
    [musvg_type_viewbox]     = { sizeof(musvg_viewbox),     alignof(musvg_viewbox)     },
    [musvg_type_aspectratio] = { sizeof(musvg_aspectratio), alignof(musvg_aspectratio) },
    [musvg_type_path]        = { sizeof(musvg_path_d),      alignof(musvg_path_d)      },
    [musvg_type_points]      = { sizeof(musvg_points),      alignof(musvg_points)      },
};

static inline uint musvg_path_opcode_arg_count(uint opcode)
{
    static const int arg_counts[] = {
        0, 0, 2, 2, 2, 2, 6, 6, 4, 4, 7, 7, 1, 1, 1, 1, 4, 4, 2, 2,
    };
    return arg_counts[opcode];
}

static inline char musvg_path_opcode_cmd_char(uint opcode)
{
    static const int cmd_chars[] = {
        '\0', 'Z', 'M', 'm', 'L', 'l', 'C', 'c', 'Q', 'q',
        'A', 'a', 'H', 'h', 'V', 'v', 'S', 's', 'T', 't'
    };
    return cmd_chars[opcode];
}

static inline uint musvg_parse_opcode(char c)
{
    switch (c) {
    case 'Z': return musvg_path_closepath;
    case 'z': return musvg_path_closepath;
    case 'M': return musvg_path_moveto_abs;
    case 'm': return musvg_path_moveto_rel;
    case 'L': return musvg_path_lineto_abs;
    case 'l': return musvg_path_lineto_rel;
    case 'C': return musvg_path_curveto_cubic_abs;
    case 'c': return musvg_path_curveto_cubic_rel;
    case 'Q': return musvg_path_quadratic_curve_to_abs;
    case 'q': return musvg_path_quadratic_curve_to_rel;
    case 'A': return musvg_path_eliptical_arc_abs;
    case 'a': return musvg_path_eliptical_arc_rel;
    case 'H': return musvg_path_line_to_horizontal_abs;
    case 'h': return musvg_path_line_to_horizontal_rel;
    case 'V': return musvg_path_line_to_vertical_abs;
    case 'v': return musvg_path_line_to_vertical_rel;
    case 'S': return musvg_path_curveto_cubic_smooth_abs;
    case 's': return musvg_path_curveto_cubic_smooth_rel;
    case 'T': return musvg_path_curveto_quadratic_smooth_abs;
    case 't': return musvg_path_curveto_quadratic_smooth_rel;
    }
    return musvg_path_none;
}

// XML parser

#define TAG 1
#define CONTENT 2
#define MAX_ATTRIBS 256

static void musvg_parse_content(char* s,
                         void (*content_cb)(void* ud, const char* s),
                         void* ud)
{
    // Trim start white spaces
    while (*s && musvg_isspace(*s)) s++;
    if (!*s) return;

    if (content_cb) {
        (*content_cb)(ud, s);
    }
}

static void musvg_parse_element(char* s,
                         void (*startel_cb)(void* ud, const char* el, const char** attr),
                         void (*endel_cb)(void* ud, const char* el),
                         void* ud)
{
    const char* attr[MAX_ATTRIBS];
    int nattr = 0;
    char* name;
    int start = 0;
    int end = 0;

    // Skip white space after the '<'
    while (*s && musvg_isspace(*s)) s++;

    // Check if the tag is end tag
    if (*s == '/')
    {
        s++;
        end = 1;
    }
    else
    {
        start = 1;
    }

    // Skip comments, data and preprocessor stuff.
    if (!*s || *s == '?' || *s == '!')
        return;

    // Get tag name
    name = s;
    while (*s && !musvg_isspace(*s)) s++;
    if (*s) { *s++ = '\0'; }

    // Get attribs
    while (!end && *s && nattr < MAX_ATTRIBS-1)
    {
        // Skip white space before the attrib name
        while (*s && musvg_isspace(*s)) s++;
        if (!*s) break;
        if (*s == '/')
        {
            end = 1;
            break;
        }
        attr[nattr++] = s;
        // Find end of the attrib name.
        while (*s && !musvg_isspace(*s) && *s != '=') s++;
        if (*s) { *s++ = '\0'; }
        // Skip until the beginning of the value.
        while (*s && *s != '\"') s++;
        if (!*s) break;
        s++;
        // Store value and find the end of it.
        attr[nattr++] = s;
        while (*s && *s != '\"') s++;
        if (*s) { *s++ = '\0'; }
    }

    // List terminator
    attr[nattr++] = 0;
    attr[nattr++] = 0;

    // Call callbacks.
    if (start && startel_cb) {
        (*startel_cb)(ud, name, attr);
    }
    if (end && endel_cb) {
        (*endel_cb)(ud, name);
    }
}

static int musvg_parse_xml(char* input,
             void (*startel_cb)(void* ud, const char* el, const char** attr),
             void (*endel_cb)(void* ud, const char* el),
             void (*content_cb)(void* ud, const char* s),
             void* ud)
{
    char* s = input;
    char* mark = s;
    int state = CONTENT;
    while (*s)
    {
        if (*s == '<' && state == CONTENT)
        {
            // Start of a tag
            *s++ = '\0';
            musvg_parse_content(mark, content_cb, ud);
            mark = s;
            state = TAG;
        }
        else if (*s == '>' && state == TAG)
        {
            // Start of a content or new tag.
            *s++ = '\0';
            musvg_parse_element(mark, startel_cb, endel_cb, ud);
            mark = s;
            state = CONTENT;
        }
        else {
            s++;
        }
    }

    return 0;
}

// SVG transform

static void xformIdentity(float* t)
{
    t[0] = 1.0f; t[1] = 0.0f;
    t[2] = 0.0f; t[3] = 1.0f;
    t[4] = 0.0f; t[5] = 0.0f;
}

static void xformSetTranslation(float* t, float tx, float ty)
{
    t[0] = 1.0f; t[1] = 0.0f;
    t[2] = 0.0f; t[3] = 1.0f;
    t[4] = tx; t[5] = ty;
}

static void xformSetScale(float* t, float sx, float sy)
{
    t[0] = sx; t[1] = 0.0f;
    t[2] = 0.0f; t[3] = sy;
    t[4] = 0.0f; t[5] = 0.0f;
}

static void xformSetSkewX(float* t, float a)
{
    t[0] = 1.0f; t[1] = 0.0f;
    t[2] = tanf(a); t[3] = 1.0f;
    t[4] = 0.0f; t[5] = 0.0f;
}

static void xformSetSkewY(float* t, float a)
{
    t[0] = 1.0f; t[1] = tanf(a);
    t[2] = 0.0f; t[3] = 1.0f;
    t[4] = 0.0f; t[5] = 0.0f;
}

static void xformSetRotation(float* t, float a)
{
    float cs = cosf(a), sn = sinf(a);
    t[0] = cs; t[1] = sn;
    t[2] = -sn; t[3] = cs;
    t[4] = 0.0f; t[5] = 0.0f;
}

static void xformMultiply(float* t, float* s)
{
    float t0 = t[0] * s[0] + t[1] * s[2];
    float t2 = t[2] * s[0] + t[3] * s[2];
    float t4 = t[4] * s[0] + t[5] * s[2] + s[4];
    t[1] = t[0] * s[1] + t[1] * s[3];
    t[3] = t[2] * s[1] + t[3] * s[3];
    t[5] = t[4] * s[1] + t[5] * s[3] + s[5];
    t[0] = t0;
    t[2] = t2;
    t[4] = t4;
}

static void xformPremultiply(float* t, float* s)
{
    float s2[6];
    memcpy(s2, s, sizeof(float)*6);
    xformMultiply(s2, t);
    memcpy(t, s2, sizeof(float)*6);
}

// SVG color parsing

#define MSVG_RGB(r, g, b) (((unsigned int)r << 16) | ((unsigned int)g << 8) | ((unsigned int)b << 0))

musvg_named_color musvg_colors[] =
{
    { MSVG_RGB(255,   0,   0), "red" },
    { MSVG_RGB(  0, 128,   0), "green" },
    { MSVG_RGB(  0,   0, 255), "blue" },
    { MSVG_RGB(255, 255,   0), "yellow" },
    { MSVG_RGB(  0, 255, 255), "cyan" },
    { MSVG_RGB(255,   0, 255), "magenta" },
    { MSVG_RGB(  0,   0,   0), "black" },
    { MSVG_RGB(128, 128, 128), "grey" },
    { MSVG_RGB(128, 128, 128), "gray" },
    { MSVG_RGB(255, 255, 255), "white" },
    { MSVG_RGB(240, 248, 255), "aliceblue" },
    { MSVG_RGB(250, 235, 215), "antiquewhite" },
    { MSVG_RGB(  0, 255, 255), "aqua" },
    { MSVG_RGB(127, 255, 212), "aquamarine" },
    { MSVG_RGB(240, 255, 255), "azure" },
    { MSVG_RGB(245, 245, 220), "beige" },
    { MSVG_RGB(255, 228, 196), "bisque" },
    { MSVG_RGB(255, 235, 205), "blanchedalmond" },
    { MSVG_RGB(138,  43, 226), "blueviolet" },
    { MSVG_RGB(165,  42,  42), "brown" },
    { MSVG_RGB(222, 184, 135), "burlywood" },
    { MSVG_RGB( 95, 158, 160), "cadetblue" },
    { MSVG_RGB(127, 255,   0), "chartreuse" },
    { MSVG_RGB(210, 105,  30), "chocolate" },
    { MSVG_RGB(255, 127,  80), "coral" },
    { MSVG_RGB(100, 149, 237), "cornflowerblue" },
    { MSVG_RGB(255, 248, 220), "cornsilk" },
    { MSVG_RGB(220,  20,  60), "crimson" },
    { MSVG_RGB(  0,   0, 139), "darkblue" },
    { MSVG_RGB(  0, 139, 139), "darkcyan" },
    { MSVG_RGB(184, 134,  11), "darkgoldenrod" },
    { MSVG_RGB(169, 169, 169), "darkgray" },
    { MSVG_RGB(  0, 100,   0), "darkgreen" },
    { MSVG_RGB(169, 169, 169), "darkgrey" },
    { MSVG_RGB(189, 183, 107), "darkkhaki" },
    { MSVG_RGB(139,   0, 139), "darkmagenta" },
    { MSVG_RGB( 85, 107,  47), "darkolivegreen" },
    { MSVG_RGB(255, 140,   0), "darkorange" },
    { MSVG_RGB(153,  50, 204), "darkorchid" },
    { MSVG_RGB(139,   0,   0), "darkred" },
    { MSVG_RGB(233, 150, 122), "darksalmon" },
    { MSVG_RGB(143, 188, 143), "darkseagreen" },
    { MSVG_RGB( 72,  61, 139), "darkslateblue" },
    { MSVG_RGB( 47,  79,  79), "darkslategray" },
    { MSVG_RGB( 47,  79,  79), "darkslategrey" },
    { MSVG_RGB(  0, 206, 209), "darkturquoise" },
    { MSVG_RGB(148,   0, 211), "darkviolet" },
    { MSVG_RGB(255,  20, 147), "deeppink" },
    { MSVG_RGB(  0, 191, 255), "deepskyblue" },
    { MSVG_RGB(105, 105, 105), "dimgray" },
    { MSVG_RGB(105, 105, 105), "dimgrey" },
    { MSVG_RGB( 30, 144, 255), "dodgerblue" },
    { MSVG_RGB(178,  34,  34), "firebrick" },
    { MSVG_RGB(255, 250, 240), "floralwhite" },
    { MSVG_RGB( 34, 139,  34), "forestgreen" },
    { MSVG_RGB(255,   0, 255), "fuchsia" },
    { MSVG_RGB(220, 220, 220), "gainsboro" },
    { MSVG_RGB(248, 248, 255), "ghostwhite" },
    { MSVG_RGB(255, 215,   0), "gold" },
    { MSVG_RGB(218, 165,  32), "goldenrod" },
    { MSVG_RGB(173, 255,  47), "greenyellow" },
    { MSVG_RGB(240, 255, 240), "honeydew" },
    { MSVG_RGB(255, 105, 180), "hotpink" },
    { MSVG_RGB(205,  92,  92), "indianred" },
    { MSVG_RGB( 75,   0, 130), "indigo" },
    { MSVG_RGB(255, 255, 240), "ivory" },
    { MSVG_RGB(240, 230, 140), "khaki" },
    { MSVG_RGB(230, 230, 250), "lavender" },
    { MSVG_RGB(255, 240, 245), "lavenderblush" },
    { MSVG_RGB(124, 252,   0), "lawngreen" },
    { MSVG_RGB(255, 250, 205), "lemonchiffon" },
    { MSVG_RGB(173, 216, 230), "lightblue" },
    { MSVG_RGB(240, 128, 128), "lightcoral" },
    { MSVG_RGB(224, 255, 255), "lightcyan" },
    { MSVG_RGB(250, 250, 210), "lightgoldenrodyellow" },
    { MSVG_RGB(211, 211, 211), "lightgray" },
    { MSVG_RGB(144, 238, 144), "lightgreen" },
    { MSVG_RGB(211, 211, 211), "lightgrey" },
    { MSVG_RGB(255, 182, 193), "lightpink" },
    { MSVG_RGB(255, 160, 122), "lightsalmon" },
    { MSVG_RGB( 32, 178, 170), "lightseagreen" },
    { MSVG_RGB(135, 206, 250), "lightskyblue" },
    { MSVG_RGB(119, 136, 153), "lightslategray" },
    { MSVG_RGB(119, 136, 153), "lightslategrey" },
    { MSVG_RGB(176, 196, 222), "lightsteelblue" },
    { MSVG_RGB(255, 255, 224), "lightyellow" },
    { MSVG_RGB(  0, 255,   0), "lime" },
    { MSVG_RGB( 50, 205,  50), "limegreen" },
    { MSVG_RGB(250, 240, 230), "linen" },
    { MSVG_RGB(128,   0,   0), "maroon" },
    { MSVG_RGB(102, 205, 170), "mediumaquamarine" },
    { MSVG_RGB(  0,   0, 205), "mediumblue" },
    { MSVG_RGB(186,  85, 211), "mediumorchid" },
    { MSVG_RGB(147, 112, 219), "mediumpurple" },
    { MSVG_RGB( 60, 179, 113), "mediumseagreen" },
    { MSVG_RGB(123, 104, 238), "mediumslateblue" },
    { MSVG_RGB( 0,  250, 154), "mediumspringgreen" },
    { MSVG_RGB( 72, 209, 204), "mediumturquoise" },
    { MSVG_RGB(199,  21, 133), "mediumvioletred" },
    { MSVG_RGB( 25,  25, 112), "midnightblue" },
    { MSVG_RGB(245, 255, 250), "mintcream" },
    { MSVG_RGB(255, 228, 225), "mistyrose" },
    { MSVG_RGB(255, 228, 181), "moccasin" },
    { MSVG_RGB(255, 222, 173), "navajowhite" },
    { MSVG_RGB(  0,   0, 128), "navy" },
    { MSVG_RGB(253, 245, 230), "oldlace" },
    { MSVG_RGB(128, 128,   0), "olive" },
    { MSVG_RGB(107, 142,  35), "olivedrab" },
    { MSVG_RGB(255, 165,   0), "orange" },
    { MSVG_RGB(255,  69,   0), "orangered" },
    { MSVG_RGB(218, 112, 214), "orchid" },
    { MSVG_RGB(238, 232, 170), "palegoldenrod" },
    { MSVG_RGB(152, 251, 152), "palegreen" },
    { MSVG_RGB(175, 238, 238), "paleturquoise" },
    { MSVG_RGB(219, 112, 147), "palevioletred" },
    { MSVG_RGB(255, 239, 213), "papayawhip" },
    { MSVG_RGB(255, 218, 185), "peachpuff" },
    { MSVG_RGB(205, 133,  63), "peru" },
    { MSVG_RGB(255, 192, 203), "pink" },
    { MSVG_RGB(221, 160, 221), "plum" },
    { MSVG_RGB(176, 224, 230), "powderblue" },
    { MSVG_RGB(128,   0, 128), "purple" },
    { MSVG_RGB(188, 143, 143), "rosybrown" },
    { MSVG_RGB( 65, 105, 225), "royalblue" },
    { MSVG_RGB(139,  69,  19), "saddlebrown" },
    { MSVG_RGB(250, 128, 114), "salmon" },
    { MSVG_RGB(244, 164,  96), "sandybrown" },
    { MSVG_RGB( 46, 139,  87), "seagreen" },
    { MSVG_RGB(255, 245, 238), "seashell" },
    { MSVG_RGB(160,  82,  45), "sienna" },
    { MSVG_RGB(192, 192, 192), "silver" },
    { MSVG_RGB(135, 206, 235), "skyblue" },
    { MSVG_RGB(106,  90, 205), "slateblue" },
    { MSVG_RGB(112, 128, 144), "slategray" },
    { MSVG_RGB(112, 128, 144), "slategrey" },
    { MSVG_RGB(255, 250, 250), "snow" },
    { MSVG_RGB( 0,  255, 127), "springgreen" },
    { MSVG_RGB( 70, 130, 180), "steelblue" },
    { MSVG_RGB(210, 180, 140), "tan" },
    { MSVG_RGB( 0,  128, 128), "teal" },
    { MSVG_RGB(216, 191, 216), "thistle" },
    { MSVG_RGB(255,  99,  71), "tomato" },
    { MSVG_RGB( 64, 224, 208), "turquoise" },
    { MSVG_RGB(238, 130, 238), "violet" },
    { MSVG_RGB(245, 222, 179), "wheat" },
    { MSVG_RGB(245, 245, 245), "whitesmoke" },
    { MSVG_RGB(154, 205, 50 ), "yellowgreen" },
};

static inline musvg_color musvg_color_rgb(uint r, uint g, uint b)
{
    musvg_color color = {
        musvg_color_type_rgba, ((r << 16) | (g << 8) | (b << 0))
    };
    return color;
}

static inline musvg_color musvg_color_none()
{
    musvg_color color = { musvg_color_type_none, 0 };
    return color;
}

static musvg_color musvg_parse_color_name(const char* str)
{
    int i, ncolors = sizeof(musvg_colors) / sizeof(musvg_named_color);

    for (i = 0; i < ncolors; i++) {
        if (strcmp(musvg_colors[i].name, str) == 0) {
            musvg_color color = {
                musvg_color_type_rgba, musvg_colors[i].color
            };
            return color;
        }
    }

    return musvg_color_rgb(128, 128, 128);
}

static musvg_color musvg_parse_color_hex(const char* str)
{
    unsigned int c = 0, r = 0, g = 0, b = 0;
    int n = 0;
    str++; // skip #
    // Calculate number of characters.
    while(str[n] && !musvg_isspace(str[n]))
        n++;
    if (n == 6) {
        sscanf(str, "%x", &c);
    } else if (n == 3) {
        sscanf(str, "%x", &c);
        c = (c&0xf) | ((c&0xf0) << 4) | ((c&0xf00) << 8);
        c |= c<<4;
    }
    r = (c >> 16) & 0xff;
    g = (c >> 8) & 0xff;
    b = c & 0xff;
    return musvg_color_rgb(r,g,b);
}

static musvg_color musvg_parse_color_rgb(const char* str)
{
    int r = -1, g = -1, b = -1;
    char s1[32]="", s2[32]="";
    sscanf(str + 4, "%d%[%%, \t]%d%[%%, \t]%d", &r, s1, &g, s2, &b);
    if (strchr(s1, '%')) {
        return musvg_color_rgb((r*255)/100,(g*255)/100,(b*255)/100);
    } else {
        return musvg_color_rgb(r,g,b);
    }
}

static musvg_index alloc_string(musvg_parser *p, const char *str, size_t len);

static musvg_index musvg_parse_url(musvg_parser *p, const char* str)
{
    char buf[128];
    int len = 0;
    str += 4; // "url(";
    if (*str == '#')
        str++;
    while (len < (sizeof(buf)-1) && *str != ')') {
        buf[len++] = *str++;
    }
    buf[len] = '\0';
    return alloc_string(p, buf, len);
}

static musvg_color musvg_parse_color_url(musvg_parser *p, const char* str)
{
    musvg_color col = { musvg_color_type_url, musvg_parse_url(p, str) };
    return col;
}

static musvg_color musvg_parse_color(musvg_parser *p, const char* str)
{
    size_t len = 0;
    while(*str == ' ') ++str;
    len = strlen(str);
    if (strcmp(str, "none") == 0)
        return musvg_color_none();
    else if (strncmp(str, "url(", 4) == 0)
        return musvg_parse_color_url(p, str);
    else if (len >= 1 && *str == '#')
        return musvg_parse_color_hex(str);
    else if (len >= 4 && str[0] == 'r' && str[1] == 'g' && str[2] == 'b' && str[3] == '(')
        return musvg_parse_color_rgb(str);
    return musvg_parse_color_name(str);
}

// SVG number parsing

// We roll our own string to float because the std library one uses locale and messes things up.
static double musvg_atof(const char* s)
{
    char* cur = (char*)s;
    char* end = NULL;
    double res = 0.0, sign = 1.0;
    long long intPart = 0, fracPart = 0;
    char hasIntPart = 0, hasFracPart = 0;

    // Parse optional sign
    if (*cur == '+') {
        cur++;
    } else if (*cur == '-') {
        sign = -1;
        cur++;
    }

    // Parse integer part
    if (musvg_isdigit(*cur)) {
        // Parse digit sequence
        intPart = strtoll(cur, &end, 10);
        if (cur != end) {
            res = (double)intPart;
            hasIntPart = 1;
            cur = end;
        }
    }

    // Parse fractional part.
    if (*cur == '.') {
        cur++; // Skip '.'
        if (musvg_isdigit(*cur)) {
            // Parse digit sequence
            fracPart = strtoll(cur, &end, 10);
            if (cur != end) {
                res += (double)fracPart / pow(10.0, (double)(end - cur));
                hasFracPart = 1;
                cur = end;
            }
        }
    }

    // A valid number should have integer or fractional part.
    if (!hasIntPart && !hasFracPart)
        return 0.0;

    // Parse optional exponent
    if (*cur == 'e' || *cur == 'E') {
        long expPart = 0;
        cur++; // skip 'E'
        expPart = strtol(cur, &end, 10); // Parse digit sequence with sign
        if (cur != end) {
            res *= pow(10.0, (double)expPart);
        }
    }

    return res * sign;
}

static const char* musvg_parse_number(const char* s, char* it, const int size)
{
    const int last = size-1;
    int i = 0;

    // sign
    if (*s == '-' || *s == '+') {
        if (i < last) it[i++] = *s;
        s++;
    }
    // integer part
    while (*s && musvg_isdigit(*s)) {
        if (i < last) it[i++] = *s;
        s++;
    }
    if (*s == '.') {
        // decimal point
        if (i < last) it[i++] = *s;
        s++;
        // fraction part
        while (*s && musvg_isdigit(*s)) {
            if (i < last) it[i++] = *s;
            s++;
        }
    }
    // exponent
    if ((*s == 'e' || *s == 'E') && (s[1] != 'm' && s[1] != 'x')) {
        if (i < last) it[i++] = *s;
        s++;
        if (*s == '-' || *s == '+') {
            if (i < last) it[i++] = *s;
            s++;
        }
        while (*s && musvg_isdigit(*s)) {
            if (i < last) it[i++] = *s;
            s++;
        }
    }
    it[i] = '\0';

    return s;
}

static const char* musvg_get_next_path_item(const char* s, char* it)
{
    it[0] = '\0';
    // Skip white spaces and commas
    while (*s && (musvg_isspace(*s) || *s == ',')) s++;
    if (!*s) return s;
    if (*s == '-' || *s == '+' || *s == '.' || musvg_isdigit(*s)) {
        s = musvg_parse_number(s, it, 64);
    } else {
        // Parse command
        it[0] = *s++;
        it[1] = '\0';
        return s;
    }

    return s;
}

static float musvg_parse_opacity(const char* str)
{
    float val = (float)musvg_atof(str);
    if (val < 0.0f) val = 0.0f;
    if (val > 1.0f) val = 1.0f;
    return val;
}

static float musvg_parse_miterlimit(const char* str)
{
    float val = (float)musvg_atof(str);
    if (val < 0.0f) val = 0.0f;
    return val;
}

static float musvg_parse_float(const char* str)
{
    while (*str == ' ') ++str;
    return (float)musvg_atof(str);
}

static int musvg_is_length(const char* s)
{
    // optional sign
    if (*s == '-' || *s == '+')
        s++;
    // must have at least one digit, or start by a dot
    return (musvg_isdigit(*s) || *s == '.');
}

musvg_small musvg_parse_units(const char* units);

static musvg_length musvg_parse_length(const char* str)
{
    char buf[64];
    musvg_length length = { 0, musvg_unit_user };
    length.units = musvg_parse_units(musvg_parse_number(str, buf, 64));
    length.value = (float)musvg_atof(buf);
    return length;
}

static musvg_viewbox musvg_parse_viewbox(const char* s)
{
    musvg_viewbox viewbox = { 0, 0, 0, 0 };
    char buf[64];
    s = musvg_parse_number(s, buf, 64);
    viewbox.x = (float)musvg_atof(buf);
    while (*s && (musvg_isspace(*s) || *s == '%' || *s == ',')) s++;
    if (!*s) goto out;
    s = musvg_parse_number(s, buf, 64);
    viewbox.y = (float)musvg_atof(buf);
    while (*s && (musvg_isspace(*s) || *s == '%' || *s == ',')) s++;
    if (!*s) goto out;
    s = musvg_parse_number(s, buf, 64);
    viewbox.width = (float)musvg_atof(buf);
    while (*s && (musvg_isspace(*s) || *s == '%' || *s == ',')) s++;
    if (!*s) goto out;
    s = musvg_parse_number(s, buf, 64);
    viewbox.height = (float)musvg_atof(buf);
out:
    return viewbox;
}

static int musvg_viewbox_string(char *buf, size_t buflen, const musvg_viewbox *vb)
{
    return snprintf(buf, buflen, "%.8g %.8g %.8g %.8g", vb->x, vb->y, vb->width, vb->height);
}

// SVG transform parsing

static int musvg_parse_transform_args(const char* str, float* args, int maxNa, musvg_small* na)
{
    const char* end;
    const char* ptr;
    char it[64];

    *na = 0;
    ptr = str;
    while (*ptr && *ptr != '(') ++ptr;
    if (*ptr == 0)
        return 1;
    end = ptr;
    while (*end && *end != ')') ++end;
    if (*end == 0)
        return 1;

    while (ptr < end) {
        if (*ptr == '-' || *ptr == '+' || *ptr == '.' || musvg_isdigit(*ptr)) {
            if (*na >= maxNa) return 0;
            ptr = musvg_parse_number(ptr, it, 64);
            args[(*na)++] = (float)musvg_atof(it);
        } else {
            ++ptr;
        }
    }
    return (int)(end - str);
}

static int musvg_parse_matrix(musvg_transform* xf, const char* str)
{
    float t[6];
    xf->nargs = 0;
    xf->type = musvg_transform_matrix;
    memset(xf->args, 0, sizeof(xf->args));
    int len = musvg_parse_transform_args(str, t, 6, &xf->nargs);
    if (xf->nargs != 6) memset(xf->xform, 0, sizeof(xf->xform));
    else memcpy(xf->xform, t, sizeof(xf->xform));
    return len;
}

static int musvg_parse_translate(musvg_transform* xf, const char* str)
{
    float t[6];
    xf->nargs = 0;
    xf->type = musvg_transform_translate;
    memset(xf->args, 0, sizeof(xf->args));
    int len = musvg_parse_transform_args(str, xf->args, 2, &xf->nargs);
    xformSetTranslation(t, xf->args[0], xf->args[1]);
    memcpy(xf->xform, t, sizeof(xf->xform));
    return len;
}

static int musvg_parse_scale(musvg_transform* xf, const char* str)
{
    float t[6];
    xf->nargs = 0;
    xf->type = musvg_transform_scale;
    memset(xf->args, 0, sizeof(xf->args));
    int len = musvg_parse_transform_args(str, xf->args, 2, &xf->nargs);
    if (xf->nargs == 1) xf->args[1] = xf->args[0];
    xformSetScale(t, xf->args[0], xf->args[1]);
    memcpy(xf->xform, t, sizeof(xf->xform));
    return len;
}

static int musvg_parse_skew_x(musvg_transform* xf, const char* str)
{
    float t[6];
    xf->nargs = 0;
    xf->type = musvg_transform_skew_x;
    memset(xf->args, 0, sizeof(xf->args));
    int len = musvg_parse_transform_args(str, xf->args, 1, &xf->nargs);
    xformSetSkewX(t, xf->args[0]/180.0f*M_PI);
    memcpy(xf->xform, t, sizeof(xf->xform));
    return len;
}

static int musvg_parse_skew_y(musvg_transform* xf, const char* str)
{
    float t[6];
    xf->nargs = 0;
    xf->type = musvg_transform_skew_y;
    memset(xf->args, 0, sizeof(xf->args));
    int len = musvg_parse_transform_args(str, xf->args, 1, &xf->nargs);
    xformSetSkewY(t, xf->args[0]/180.0f*M_PI);
    memcpy(xf->xform, t, sizeof(xf->xform));
    return len;
}

static int musvg_parse_rotate(musvg_transform* xf, const char* str)
{
    float t[6];
    xf->nargs = 0;
    xf->type = musvg_transform_rotate;
    memset(xf->args, 0, sizeof(xf->args));
    float m[6];
    int len = musvg_parse_transform_args(str, xf->args, 3, &xf->nargs);
    if (xf->nargs == 1)
        xf->args[1] = xf->args[2] = 0.0f;
    xformIdentity(m);

    if (xf->nargs > 1) {
        xformSetTranslation(t, -xf->args[1], -xf->args[2]);
        xformMultiply(m, t);
    }

    xformSetRotation(t, xf->args[0]/180.0f*M_PI);
    xformMultiply(m, t);

    if (xf->nargs > 1) {
        xformSetTranslation(t, xf->args[1], xf->args[2]);
        xformMultiply(m, t);
    }

    memcpy(xf->xform, m, sizeof(xf->xform));

    return len;
}

int musvg_transform_string(char *buf, size_t buflen, const musvg_transform *xf)
{
    const float *v = xf->type == musvg_transform_matrix ? xf->xform : xf->args;
    const uint nargs = xf->type == musvg_transform_matrix ? 6 : xf->nargs;
    int len = snprintf(buf, buflen, "%s(", musvg_transform_names[xf->type]);
    for (uint i = 0; i < nargs; i++) {
        len += snprintf(buf+len, buflen-len, "%s%.8g", i > 0 ? "," : "", v[i]);
    }
    len += snprintf(buf+len, buflen-len, ")");
    return len;
}

static musvg_transform musvg_parse_transform(const char* str)
{
    musvg_transform transform, tmp;
    int ntrans = 0, len;

    xformIdentity(transform.xform);
    while (*str)
    {
        if (strncmp(str, "matrix", 6) == 0)
            len = musvg_parse_matrix(&tmp, str);
        else if (strncmp(str, "translate", 9) == 0)
            len = musvg_parse_translate(&tmp, str);
        else if (strncmp(str, "scale", 5) == 0)
            len = musvg_parse_scale(&tmp, str);
        else if (strncmp(str, "rotate", 6) == 0)
            len = musvg_parse_rotate(&tmp, str);
        else if (strncmp(str, "skewX", 5) == 0)
            len = musvg_parse_skew_x(&tmp, str);
        else if (strncmp(str, "skewY", 5) == 0)
            len = musvg_parse_skew_y(&tmp, str);
        else{
            ++str;
            continue;
        }
        if (len != 0) {
            str += len;
        } else {
            ++str;
            continue;
        }

        /* musvg_transform contains transform arguments and the
         * resulting matrix. a single transform is isomorphic,
         * but multiple transforms are converted to matrix. */
        if (ntrans++ == 0) {
            memcpy(&transform, &tmp, sizeof(transform));
        } else {
            memset(&transform, 0, sizeof(transform));
            transform.type = musvg_transform_matrix;
            xformPremultiply(transform.xform, tmp.xform);
        }
    }

    return transform;
}

// SVG enumeration parsing

musvg_small musvg_parse_format(const char *format)
{
    if (strcmp(format, "text") == 0)
        return musvg_format_text;
    else if (strcmp(format, "xml") == 0)
        return musvg_format_xml;
    else if (strcmp(format, "binary-vf") == 0)
        return musvg_format_binary_vf;
    else if (strcmp(format, "svgv") == 0)
        return musvg_format_binary_vf;
    else if (strcmp(format, "binary-ieee") == 0)
        return musvg_format_binary_ieee;
    else if (strcmp(format, "svgb") == 0)
        return musvg_format_binary_ieee;
    return musvg_format_none;
}

musvg_small musvg_parse_units(const char* units)
{
    if (units[0] == 'p' && units[1] == 'x')
        return musvg_unit_px;
    else if (units[0] == 'p' && units[1] == 't')
        return musvg_unit_pt;
    else if (units[0] == 'p' && units[1] == 'c')
        return musvg_unit_pc;
    else if (units[0] == 'm' && units[1] == 'm')
        return musvg_unit_mm;
    else if (units[0] == 'c' && units[1] == 'm')
        return musvg_unit_cm;
    else if (units[0] == 'i' && units[1] == 'n')
        return musvg_unit_in;
    else if (units[0] == '%')
        return musvg_unit_percent;
    else if (units[0] == 'e' && units[1] == 'm')
        return musvg_unit_em;
    else if (units[0] == 'e' && units[1] == 'x')
        return musvg_unit_ex;
    return musvg_unit_user;
}

musvg_small musvg_parse_linecap(const char* str)
{
    if (strcmp(str, "butt") == 0)
        return musvg_linecap_butt;
    else if (strcmp(str, "round") == 0)
        return musvg_linecap_round;
    else if (strcmp(str, "square") == 0)
        return musvg_linecap_square;
    return musvg_linecap_default;
}

musvg_small musvg_parse_linejoin(const char* str)
{
    if (strcmp(str, "miter") == 0)
        return musvg_linejoin_miter;
    else if (strcmp(str, "round") == 0)
        return musvg_linejoin_round;
    else if (strcmp(str, "bevel") == 0)
        return musvg_linejoin_bevel;
    return musvg_linejoin_default;
}

musvg_small musvg_parse_fillrule(const char* str)
{
    if (strcmp(str, "nonzero") == 0)
        return musvg_fillrule_nonzero;
    else if (strcmp(str, "evenodd") == 0)
        return musvg_fillrule_evenodd;
    return musvg_fillrule_default;
}

musvg_small musvg_parse_display(const char* str)
{
    if (strcmp(str, "none") == 0)
        return musvg_display_none;
    else if (strcmp(str, "inline") == 0)
        return musvg_display_inline;
    return musvg_display_default;
}

musvg_small musvg_parse_spread_method(const char* str)
{
    if (strcmp(str, "pad") == 0)
        return musvg_spread_method_pad;
    else if (strcmp(str, "reflect") == 0)
        return musvg_spread_method_reflect;
    else if (strcmp(str, "repeat") == 0)
        return musvg_spread_method_repeat;
    return musvg_spread_method_default;
}

musvg_small musvg_parse_gradient_units(const char* str)
{
    if (strcmp(str, "userSpaceOnUse") == 0)
        return musvg_gradient_unit_user;
    else if (strcmp(str, "objectBoundingBox") == 0)
        return musvg_gradient_unit_obb;
    return musvg_gradient_unit_default;
}

musvg_small musvg_parse_aspectratio_align(const char* str, int isx)
{
    if (strcmp(str, "none") == 0)
        return musvg_align_none;
    else if (strstr(str, isx ? "xMid" : "yMid") != 0)
        return musvg_align_mid;
    else if (strstr(str, isx ? "xMin" : "yMin") != 0)
        return musvg_align_min;
    else if (strstr(str, isx ? "xMax" : "yMax") != 0)
        return musvg_align_max;
    return musvg_align_default;
}

musvg_small musvg_parse_aspectratio_crop(const char* str)
{
    if (strcmp(str, "none") == 0)
        return musvg_align_none;
    else if (strstr(str, "meet") != 0)
        return musvg_crop_meet;
    else if (strstr(str, "slice") != 0)
        return  musvg_crop_slice;
    return musvg_crop_default;
}

static musvg_aspectratio musvg_parse_aspectratio(const char* str)
{
    musvg_aspectratio aspectratio;
    // Parse X align
    aspectratio.alignX = musvg_parse_aspectratio_align(str, 1);
    // Parse Y align
    aspectratio.alignY = musvg_parse_aspectratio_align(str, 0);
    // Parse meet/slice
    aspectratio.alignType = musvg_parse_aspectratio_crop(str);
    return aspectratio;
}

static int musvg_aspectratio_string(char *buf, size_t buflen, const musvg_aspectratio *ar)
{
    int len = 0;
    if (ar->alignX == musvg_align_none ||
        ar->alignY == musvg_align_none ||
        ar->alignType == musvg_crop_none) {
        len += snprintf(buf+len, buflen-len, "none");
    } else {
        len += snprintf(buf+len, buflen-len, "x%sy%s %s",
            musvg_align_names[ar->alignX],
            musvg_align_names[ar->alignY],
            musvg_crop_names[ar->alignType]);
    }
    return len;
}

static const char* musvg_get_next_dash_item(const char* s, char* it)
{
    int n = 0;
    it[0] = '\0';
    // Skip white spaces and commas
    while (*s && (musvg_isspace(*s) || *s == ',')) s++;
    // Advance until whitespace, comma or end.
    while (*s && (!musvg_isspace(*s) && *s != ',')) {
        if (n < 63)
            it[n++] = *s;
        s++;
    }
    it[n++] = '\0';
    return s;
}

static musvg_dasharray musvg_parse_stroke_dasharray(const char* str)
{
    char item[64];
    float sum = 0.0f;
    musvg_dasharray r = { { 0 }, 0 };

    // Handle "none"
    if (str[0] == 'n')
        return r;

    // Parse dashes
    while (*str) {
        str = musvg_get_next_dash_item(str, item);
        if (!*item) break;
        if (r.count < array_size(r.dashes)) {
            r.dashes[r.count++] = fabsf((float)musvg_atof(item));
        }
    }

    for (uint i = 0; i < r.count; i++)
        sum += r.dashes[i];
    if (sum <= 1e-6f)
        r.count = 0;

    return r;
}

static int musvg_dasharray_string(char *buf, size_t buflen, const musvg_dasharray *da)
{
    int len = 0;
    if (!da->count) {
        len += snprintf(buf+len, buflen-len, "none");
    } else {
        for (size_t i = 0; i < da->count; i++) {
            len += snprintf(buf+len, buflen-len, "%s%.8g", i > 0 ? "," : "", da->dashes[i]);
        }
    }
    return len;
}

static musvg_id musvg_parse_id(musvg_parser *p, const char* str)
{
    musvg_index string_offset = alloc_string(p, str, strlen(str));
    musvg_id id = { (uint)string_offset };
    return id;
}

static musvg_points musvg_parse_points(musvg_parser *p, const char *s)
{
    char item[64];

    musvg_points points = { points_count(p) };

    while (*s) {
        s = musvg_get_next_path_item(s, item);
        if (!*item) break;
        float value = (float)musvg_atof(item);
        points_add(p, &value);
    }

    points.point_count = points_count(p) - points.point_offset;

    return points;
}

static musvg_path_d musvg_parse_path_ops(musvg_parser *p, const char *s)
{
    int nargs, opc;
    uint argc;
    musvg_small code;
    float args[7];
    char item[64];

    musvg_path_d ops = { path_ops_count(p) };

    nargs = 0;
    while (*s) {
        s = musvg_get_next_path_item(s, item);
        if (!*item) break;
        int is_length = musvg_is_length(item);
        if (nargs == 0 && !is_length) {
            opc = item[0];
            code = musvg_parse_opcode(opc);
            argc = musvg_path_opcode_arg_count(code);
            if (code != 0 && argc == 0) {
                musvg_path_op op = { code };
                musvg_points points = { 0, 0 };
                path_ops_add(p, &op);
                path_points_add(p, &points);
            }
            continue;
        }
        float value = (float)musvg_atof(item);
        args[nargs] = value;
        if (nargs == argc - 1) {
            uint points_offset = points_count(p);
            for (uint i = 0; i < argc; i++) {
                points_add(p, args + i);
            }
            musvg_path_op op = { code };
            musvg_points points = { points_offset, argc };
            path_ops_add(p, &op);
            path_points_add(p, &points);
        }
        nargs = (nargs + 1) % argc;
    }

    ops.op_count = path_ops_count(p) - ops.op_offset;

    return ops;
}

// SVG node stack

llong mnu_int48_get(mnu_int48 p)
{
    llong v = ((llong)p.d[0]) | ((llong)p.d[1] << 16) | ((llong)p.d[2] << 32);
    return v << 16 >> 16;
}

mnu_int48 mnu_int48_set(llong v)
{
    mnu_int48 x = { (ushort)v, (ushort)(v >> 16), (ushort)(v >> 32) };
    return x;
}

static musvg_attr slot_type(musvg_parser *p, musvg_index idx)
{
    return (musvg_attr)slots_get(p, idx)->type;
}

static musvg_index slot_left(musvg_parser *p, musvg_index idx)
{
    return mnu_int48_get(slots_get(p, idx)->left);
}

static musvg_index slot_storage(musvg_parser *p, musvg_index idx)
{
    return mnu_int48_get(slots_get(p, idx)->storage);
}

static musvg_element node_type(musvg_parser *p, musvg_index idx)
{
    return (musvg_element)nodes_get(p, idx)->type;
}

static musvg_index node_left(musvg_parser *p, musvg_index idx)
{
    return mnu_int48_get(nodes_get(p, idx)->left);
}

static musvg_index node_down(musvg_parser *p, musvg_index idx)
{
    return mnu_int48_get(nodes_get(p, idx)->down);
}

static musvg_index node_up(musvg_parser *p, musvg_index idx)
{
    return mnu_int48_get(nodes_get(p, idx)->up);
}

static musvg_index node_attr(musvg_parser *p, musvg_index idx)
{
    return mnu_int48_get(nodes_get(p, idx)->attr);
}

static void node_set_type(musvg_parser *p, musvg_index idx, musvg_element type)
{
    nodes_get(p, idx)->type = type;
}

static void node_set_left(musvg_parser *p, musvg_index idx, musvg_index left)
{
    nodes_get(p, idx)->left = mnu_int48_set(left);
}

static void node_set_down(musvg_parser *p, musvg_index idx, musvg_index down)
{
    nodes_get(p, idx)->down = mnu_int48_set(down);
}

static void node_set_up(musvg_parser *p, musvg_index idx, musvg_index up)
{
    nodes_get(p, idx)->up = mnu_int48_set(up);
}

static void node_set_attr(musvg_parser *p, musvg_index idx, musvg_index attr)
{
    nodes_get(p, idx)->attr = mnu_int48_set(attr);
}

static musvg_index musvg_node_add(musvg_parser *p, musvg_element type)
{
    musvg_index node_idx = nodes_alloc(p, 1);
    musvg_node *node = nodes_get(p, node_idx);

    uint depth = p->node_depth++;
    if (depth == musvg_max_depth) abort();

    musvg_index left_idx = p->node_stack[depth];
    p->node_stack[depth] = node_idx;
    musvg_index parent_idx = depth > 0 ? p->node_stack[depth-1] : 0;

    node_set_type(p, node_idx, type);
    node_set_left(p, node_idx, left_idx);
    node_set_down(p, node_idx, 0);
    node_set_up  (p, node_idx, parent_idx);
    node_set_attr(p, node_idx, 0);
    node_set_down(p, parent_idx, node_idx);

    return node_idx;
}

static void musvg_stack_pop(musvg_parser *p)
{
    if (p->node_depth == 0) abort();
    uint depth = p->node_depth--;
    p->node_stack[depth] = 0;
}

// SVG attribute storage

static inline musvg_index alloc_string(musvg_parser *p, const char *str, size_t len)
{
    musvg_index storage = strings_alloc(p, len + 1, 1);
    char *buffer = (char*)strings_get(p, storage);
    memcpy(buffer, str, len);
    buffer[len] = '\0';
    return storage;
}

static inline char* fetch_string(musvg_parser *p, musvg_index storage)
{
    return (char*)strings_get(p, storage);
}

static inline musvg_index find_attr(musvg_parser *p, const musvg_index node_idx, musvg_attr attr)
{
    musvg_index slot_idx = node_attr(p, node_idx);
    while (slot_idx) {
        if (slot_type(p, slot_idx) == attr) return slot_storage(p, slot_idx);
        slot_idx = slot_left(p, slot_idx);
    }
    /* zero offset is reserved and means not found */
    return 0;
}

static musvg_index find_attr_parent(musvg_parser *p, musvg_index node_idx, musvg_attr attr)
{
    /* find node attribute, if not found retry with parent */
    musvg_index storage;
    while ((storage = find_attr(p, node_idx, attr)) == 0) {
        if ((node_idx = node_up(p, node_idx)) == 0) break;
    }
    return storage;
}

static inline musvg_index alloc_attr(musvg_parser *p, musvg_index node_idx, musvg_attr attr)
{
    /* allocate aligned storage space */
    size_t type = musvg_attr_types[attr];
    size_t size = musvg_type_storage[type].size;
    size_t align = musvg_type_storage[type].align;
    musvg_index slot_idx = node_attr(p, node_idx);
    musvg_index storage = storage_alloc(p, size, align);
    musvg_slot o = { attr, mnu_int48_set(storage), mnu_int48_set(slot_idx) };
    node_set_attr(p, node_idx, slots_add(p,&o));
    return storage;
}

static inline musvg_small* attr_pointer(musvg_parser *p, musvg_index node_idx, musvg_attr attr)
{
    /*
     * search for the attribute in this nodes attr slot table, and if not
     * found, allocate aligned storage and record the slot in the table.
     *
     * alloc attr assumes constraint that attributes are written contiguously
     * such as the case when parsing xml or binary, but not random writes.
     *
     * note: fetching multiple attribute pointers in one scope is not
     * supported because an allocation can cause a previously fetched pointer
     * to be invalidated due to a call to realloc in alloc_attr.
     */
    musvg_index storage = find_attr(p, node_idx, attr);
    if (storage == 0) {
        storage = alloc_attr(p, node_idx, attr);
    }
    return (musvg_small*)storage_get(p, storage);
}

static inline musvg_small* attr_pointer_parent(musvg_parser *p, musvg_index node_idx, musvg_attr attr)
{
    musvg_index storage = find_attr_parent(p, node_idx, attr);
    if (storage == 0) {
        return NULL;
    }
    return (musvg_small*)storage_get(p, storage);
}

static inline musvg_small parse_enum(musvg_attr attr, const char *s)
{
    return musvg_type_info_enum[attr].parse(s);
}

static inline musvg_small enum_modulus(musvg_attr attr)
{
    return musvg_type_info_enum[attr].limit + 1;
}

// binary readers

int musvg_read_binary_enum(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    musvg_small *enum_value = (musvg_small*)attr_pointer(p, node_idx, attr);
    assert(mu_buf_read_i8(buf, enum_value));
    *enum_value = *enum_value  % enum_modulus(attr);
    return 0;
}

int musvg_read_binary_id(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    musvg_id *id = (musvg_id*)attr_pointer(p, node_idx, attr);
    char id_str[128] = { 0 };
    ullong id_len = 0;
    assert(!mu_leb_u64_read(buf, &id_len));
    assert(id_len < sizeof(id_str));
    assert(mu_buf_read_bytes(buf, id_str, id_len) == id_len);
    id->name = alloc_string(p, id_str, id_len);
    return 0;
}

int musvg_read_binary_length(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    musvg_length *length = (musvg_length*)attr_pointer(p, node_idx, attr);
    assert(!p->f32_read(buf, &length->value));
    assert(mu_buf_read_i8(buf, &length->units));
    return 0;
}

int musvg_read_binary_color(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    musvg_color *color = (musvg_color*)attr_pointer(p, node_idx, attr);
    uint8_t type;
    assert(mu_buf_read_i8(buf, (int8_t*)&type));
    color->type = type;
    if (color->type == musvg_color_type_rgba) {
        int32_t col;
        assert(mu_buf_read_i32(buf, &col));
        color->data = col;
    } else if (color->type == musvg_color_type_url) {
        ullong url_len = 0;
        char url_str[128];
        assert(!mu_leb_u64_read(buf, &url_len));
        assert(url_len < sizeof(url_str));
        assert(mu_buf_read_bytes(buf, url_str, url_len) == url_len);
        color->data = alloc_string(p, url_str, url_len);
    }
    return 0;
}

int musvg_read_binary_transform(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    musvg_transform *xf = (musvg_transform*)attr_pointer(p, node_idx, attr);
    assert(mu_buf_read_i8(buf, (int8_t*)&xf->type));
    if (xf->type == musvg_transform_matrix) {
        xf->nargs = 0;
        assert(!p->f32_read_vec(buf, xf->xform, 6));
    } else {
        assert(mu_buf_read_i8(buf, (int8_t*)&xf->nargs));
        assert(!p->f32_read_vec(buf, xf->args, xf->nargs));
    }
    return 0;
}

int musvg_read_binary_dasharray(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    musvg_dasharray *da = (musvg_dasharray*)attr_pointer(p, node_idx, attr);
    assert(mu_buf_read_i8(buf, (int8_t*)&da->count));
    assert(!p->f32_read_vec(buf, da->dashes, da->count));
    return 0;
}

int musvg_read_binary_float(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    float *value = (float*)attr_pointer(p, node_idx, attr);
    assert(!p->f32_read(buf, value));
    return 0;
}

int musvg_read_binary_viewbox(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    musvg_viewbox *vb = (musvg_viewbox*)attr_pointer(p, node_idx, attr);
    assert(!p->f32_read(buf, &vb->x));
    assert(!p->f32_read(buf, &vb->y));
    assert(!p->f32_read(buf, &vb->width));
    assert(!p->f32_read(buf, &vb->height));
    return 0;
}

int musvg_read_binary_aspectratio(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    musvg_aspectratio *ar = (musvg_aspectratio*)attr_pointer(p, node_idx, attr);
    assert(mu_buf_read_i8(buf, &ar->alignX));
    assert(mu_buf_read_i8(buf, &ar->alignY));
    assert(mu_buf_read_i8(buf, &ar->alignType));
    return 0;
}

int musvg_read_binary_path(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    musvg_path_d *pd = (musvg_path_d*)attr_pointer(p, node_idx, attr);
    ullong count = 0;
    assert(!mu_leb_u64_read(buf, &count));
    musvg_path_d ops = { path_ops_count(p), count };
    *pd = ops;
    for (uint j = 0; j < ops.op_count; j++) {
        ullong count = 0; musvg_small code = 0;
        assert(mu_buf_read_i8(buf, (int8_t*)&code));
        assert(!mu_leb_u64_read(buf, &count));
        musvg_path_op op = { code };
        musvg_points points = { points_count(p), count };
        path_ops_add(p, &op);
        path_points_add(p, &points);
        ullong points_count = points.point_count;
        ullong points_idx = points_alloc(p, points_count);
        if (points_linear(p, points_idx, points.point_count)) {
            assert(!p->f32_read_vec(buf, points_get(p, points_idx), points_count));
        } else {
            for (size_t k = 0; k < points.point_count; k++) {
                assert(!p->f32_read(buf, points_get(p, points_idx + k)));
            }
        }
    }
    return 0;
}

int musvg_read_binary_points(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    musvg_points *pp = (musvg_points*)attr_pointer(p, node_idx, attr);
    ullong count = 0;
    assert(!mu_leb_u64_read(buf, &count));
    musvg_points points = { points_count(p), count };
    *pp = points;
    ullong points_count = points.point_count;
    ullong points_idx = points_alloc(p, points_count);
    if (points_linear(p, points_idx, points.point_count)) {
        assert(!p->f32_read_vec(buf, points_get(p, points_idx), points_count));
    } else {
        for (size_t k = 0; k < points.point_count; k++) {
            assert(!p->f32_read(buf, points_get(p, points_idx + k)));
        }
    }
    return 0;
}

// binary writers

int musvg_write_binary_enum(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    const musvg_small value = *attr_pointer(p, node_idx, attr) % enum_modulus(attr);
    assert(mu_buf_write_i8(buf, value));
    return 0;
}

int musvg_write_binary_id(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    const musvg_id id = *(musvg_id*)attr_pointer(p, node_idx, attr);
    const char* id_str = fetch_string(p, id.name);
    const ullong id_len = strlen(id_str);
    assert(!mu_leb_u64_write(buf, &id_len));
    assert(mu_buf_write_bytes(buf, id_str, id_len) == id_len);
    return 0;
}

int musvg_write_binary_length(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    const musvg_length length = *(musvg_length*)attr_pointer(p, node_idx, attr);
    assert(!p->f32_write(buf, length.value));
    assert(mu_buf_write_i8(buf, length.units) == 1);
    return 0;
}

int musvg_write_binary_color(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    const musvg_color color = *(musvg_color*)attr_pointer(p, node_idx, attr);
    assert(mu_buf_write_i8(buf, (int8_t)color.type));
    if (color.type == musvg_color_type_rgba) {
        assert(mu_buf_write_i32(buf, (int32_t)color.data));
    } else if (color.type == musvg_color_type_url) {
        const char *url_str = fetch_string(p, color.data);
        const ullong url_len = strlen(url_str);
        assert(!mu_leb_u64_write(buf, &url_len));
        assert(mu_buf_write_bytes(buf, url_str, url_len) == url_len);
    }
    return 0;
}

int musvg_write_binary_transform(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    const musvg_transform xf = *(musvg_transform*)attr_pointer(p, node_idx, attr);
    assert(mu_buf_write_i8(buf, (int8_t)xf.type));
    if (xf.type == musvg_transform_matrix) {
        assert(!p->f32_write_vec(buf, xf.xform, 6));
    } else {
        assert(mu_buf_write_i8(buf, (int8_t)xf.nargs));
        assert(!p->f32_write_vec(buf, xf.args, xf.nargs));
    }
    return 0;
}

int musvg_write_binary_dasharray(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    const musvg_dasharray da = *(musvg_dasharray*)attr_pointer(p, node_idx, attr);
    assert(mu_buf_write_i8(buf, (int8_t)da.count));
    assert(!p->f32_write_vec(buf, da.dashes, da.count));
    return 0;
}

int musvg_write_binary_float(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    const float value = *(float*)attr_pointer(p, node_idx, attr);
    assert(!p->f32_write(buf, value));
    return 0;
}

int musvg_write_binary_viewbox(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    const musvg_viewbox vb = *(musvg_viewbox*)attr_pointer(p, node_idx, attr);
    assert(!p->f32_write(buf, vb.x));
    assert(!p->f32_write(buf, vb.y));
    assert(!p->f32_write(buf, vb.width));
    assert(!p->f32_write(buf, vb.height));
    return 0;
}

int musvg_write_binary_aspectratio(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    const musvg_aspectratio ar = *(musvg_aspectratio*)attr_pointer(p, node_idx, attr);
    assert(mu_buf_write_i8(buf, ar.alignX));
    assert(mu_buf_write_i8(buf, ar.alignY));
    assert(mu_buf_write_i8(buf, ar.alignType));
    return 0;
}

int musvg_write_binary_path(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    musvg_path_d ops = *(musvg_path_d*)attr_pointer(p, node_idx, attr);
    ullong count = ops.op_count;
    assert(!mu_leb_u64_write(buf, &count));
    for (musvg_index j = 0; j < ops.op_count; j++) {
        const  musvg_path_op *op = path_ops_get(p, ops.op_offset + j);
        const  musvg_points *points = path_points_get(p, ops.op_offset + j);
        musvg_small code = op->code;
        ullong points_count = points->point_count;
        ullong points_idx = points->point_offset;
        assert(mu_buf_write_i8(buf, (int8_t)code));
        assert(!mu_leb_u64_write(buf, &points_count));
        if (points_linear(p, points_idx, points_count)) {
            assert(!p->f32_write_vec(buf, points_get(p, points_idx), points_count));
        } else {
            for (size_t k = 0; k < points_count; k++) {
                assert(!p->f32_write(buf, *points_get(p, points_idx + k)));
            }
        }
    }
    return 0;
}

int musvg_write_binary_points(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    musvg_points points = *(musvg_points*)attr_pointer(p, node_idx, attr);
    ullong points_count = points.point_count;
    ullong points_idx = points.point_offset;
    assert(!mu_leb_u64_write(buf, &points_count));
    if (points_linear(p, points_idx, points_count)) {
        assert(!p->f32_write_vec(buf, points_get(p, points_idx), points_count));
    } else {
        for (size_t k = 0; k < points_count; k++) {
            assert(!p->f32_write(buf, *points_get(p, points_idx + k)));
        }
    }
    return 0;
}

// attribute parsers

int musvg_read_text_enum(musvg_parser *p, const char *s, musvg_index node_idx, musvg_attr attr)
{
    musvg_small val = parse_enum(attr, s);
    musvg_small *ptr = (musvg_small*)attr_pointer(p, node_idx, attr);
    *ptr = val;
    return 0;
}

int musvg_read_text_id(musvg_parser *p, const char *s, musvg_index node_idx, musvg_attr attr)
{
    musvg_id val = musvg_parse_id(p, s);
    musvg_id *ptr = (musvg_id*)attr_pointer(p, node_idx, attr);
    *ptr = val;
    return 0;
}

int musvg_read_text_length(musvg_parser *p, const char *s, musvg_index node_idx, musvg_attr attr)
{
    musvg_length val = musvg_parse_length(s);
    musvg_length *ptr = (musvg_length*)attr_pointer(p, node_idx, attr);
    *ptr = val;
    return 0;
}

int musvg_read_text_color(musvg_parser *p, const char *s, musvg_index node_idx, musvg_attr attr)
{
    musvg_color val = musvg_parse_color(p, s);
    musvg_color *ptr = (musvg_color*)attr_pointer(p, node_idx, attr);
    *ptr = val;
    return 0;
}

int musvg_read_text_transform(musvg_parser *p, const char *s, musvg_index node_idx, musvg_attr attr)
{
    musvg_transform val = musvg_parse_transform(s);
    musvg_transform *ptr = (musvg_transform*)attr_pointer(p, node_idx, attr);
    *ptr = val;
    return 0;
}

int musvg_read_text_dasharray(musvg_parser *p, const char *s, musvg_index node_idx, musvg_attr attr)
{
    musvg_dasharray val = musvg_parse_stroke_dasharray(s);
    musvg_dasharray *ptr = (musvg_dasharray*)attr_pointer(p, node_idx, attr);
    *ptr = val;
    return 0;
}

int musvg_read_text_float(musvg_parser *p, const char *s, musvg_index node_idx, musvg_attr attr)
{
    float val = (float)musvg_atof(s);
    float *ptr = (float*)attr_pointer(p, node_idx, attr);
    *ptr = val;
    return 0;
}

int musvg_read_text_viewbox(musvg_parser *p, const char *s, musvg_index node_idx, musvg_attr attr)
{
    musvg_viewbox val = musvg_parse_viewbox(s);
    musvg_viewbox *ptr = (musvg_viewbox*)attr_pointer(p, node_idx, attr);
    *ptr = val;
    return 0;
}

int musvg_read_text_aspectratio(musvg_parser *p, const char *s, musvg_index node_idx, musvg_attr attr)
{
    musvg_aspectratio val = musvg_parse_aspectratio(s);
    musvg_aspectratio *ptr = (musvg_aspectratio*)attr_pointer(p, node_idx, attr);
    *ptr = val;
    return 0;
}

int musvg_read_text_path(musvg_parser *p, const char *s, musvg_index node_idx, musvg_attr attr)
{
    musvg_path_d val = musvg_parse_path_ops(p, s);
    musvg_path_d *ptr = (musvg_path_d*)attr_pointer(p, node_idx, attr);
    *ptr = val;
    return 0;
}

int musvg_read_text_points(musvg_parser *p, const char *s, musvg_index node_idx, musvg_attr attr)
{
    musvg_points val = musvg_parse_points(p, s);
    musvg_points *ptr = (musvg_points*)attr_pointer(p, node_idx, attr);
    *ptr = val;
    return 0;
}

// text writers

int musvg_write_text_enum(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    const musvg_small enum_value = *attr_pointer(p, node_idx, attr) % enum_modulus(attr);
    const char *enum_name = musvg_type_info_enum[attr].names[enum_value];
    size_t enum_len = strlen(enum_name);
    assert(mu_buf_write_bytes(buf, enum_name, enum_len) == enum_len);
    return 0;
}

int musvg_write_text_id(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    const musvg_id id = *(musvg_id*)attr_pointer(p, node_idx, attr);
    const char* id_str = fetch_string(p, id.name);
    const ullong id_len = strlen(id_str);
    assert(mu_buf_write_bytes(buf, id_str, id_len) == id_len);
    return 0;
}

int musvg_write_text_length(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    const musvg_length length = *(musvg_length*)attr_pointer(p, node_idx, attr);
    char str[64];
    int len = snprintf(str, sizeof(str), "%.8g", length.value);
    if (length.units != musvg_unit_default) {
        len += snprintf(str + len, sizeof(str) - len, "%s",
            musvg_unit_names[length.units]);
    }
    assert(mu_buf_write_bytes(buf, str, len) == len);
    return 0;
}

int musvg_write_text_color(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    char str[128];
    const musvg_color color = *(musvg_color*)attr_pointer(p, node_idx, attr);
    if (color.type == musvg_color_type_url) {
        const char* url_str = fetch_string(p, color.data);
        int len = snprintf(str, sizeof(str), "url(#%s)", url_str);
        assert(mu_buf_write_bytes(buf, str, len) == len);
    } else if (color.type == musvg_color_type_rgba) {
        int len = snprintf(str, sizeof(str), "#%06x", (int32_t)color.data);
        assert(mu_buf_write_bytes(buf, str, len) == len);
    } else {
        assert(mu_buf_write_bytes(buf, "none", 4) == 4);
    }
    return 0;
}

int musvg_write_text_transform(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    const musvg_transform xf = *(musvg_transform*)attr_pointer(p, node_idx, attr);
    char str[128];
    int len = musvg_transform_string(str, sizeof(str), &xf);
    assert(mu_buf_write_bytes(buf, str, len) == len);
    return 0;
}

int musvg_write_text_dasharray(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    const musvg_dasharray da = *(musvg_dasharray*)attr_pointer(p, node_idx, attr);
    char str[128];
    int len = musvg_dasharray_string(str, sizeof(str), &da);
    assert(mu_buf_write_bytes(buf, str, len) == len);
    return 0;
}

int musvg_write_text_float(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    const float value = *(float*)attr_pointer(p, node_idx, attr);
    char str[128];
    int len = snprintf(str, sizeof(str), "%.8f", value);
    assert(mu_buf_write_bytes(buf, str, len) == len);
    return 0;
}

int musvg_write_text_viewbox(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    const musvg_viewbox vb = *(musvg_viewbox*)attr_pointer(p, node_idx, attr);
    char str[128];
    int len = musvg_viewbox_string(str, sizeof(str), &vb);
    assert(mu_buf_write_bytes(buf, str, len) == len);
    return 0;
}

int musvg_write_text_aspectratio(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    const musvg_aspectratio ar = *(musvg_aspectratio*)attr_pointer(p, node_idx, attr);
    char str[128];
    int len = musvg_aspectratio_string(str, sizeof(str), &ar);
    assert(mu_buf_write_bytes(buf, str, len) == len);
    return 0;
}

int musvg_write_text_path(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    musvg_path_d ops = *(musvg_path_d*)attr_pointer(p, node_idx, attr);
    char last_code = 0;
    for (musvg_index j = 0; j < ops.op_count; j++) {
        char str[128];
        const musvg_path_op *op = path_ops_get(p, ops.op_offset + j);
        const musvg_points *points = path_points_get(p, ops.op_offset + j);
        int8_t code = musvg_path_opcode_cmd_char(op->code);
        assert(mu_buf_write_i8(buf, code != last_code ? code : ' '));
        for (musvg_index k = 0; k < points->point_count; k++) {
            if (k > 0) assert(mu_buf_write_i8(buf, ','));
            float v = *points_get(p, points->point_offset + k);
            int len = snprintf(str, sizeof(str), "%.8g", v);
            assert(mu_buf_write_bytes(buf, str, len) == len);
        }
        last_code = code;
    }
    return 0;
}

int musvg_write_text_points(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr)
{
    musvg_points points = *(musvg_points*)attr_pointer(p, node_idx, attr);
    for (musvg_index j = 0; j < points.point_count; j++) {
        char str[128];
        if (j > 0) assert(mu_buf_write_i8(buf, j % 2 ? ',' : ' '));
        float v = *points_get(p, points.point_offset + j);
        int len = snprintf(str, sizeof(str), "%.8g", v);
        assert(mu_buf_write_bytes(buf, str, len) == len);
    }
    return 0;
}

// type metadata

typedef int (*musvg_attr_str_fn)(musvg_parser *p, const char *s, musvg_index node_idx, musvg_attr attr);

static const musvg_attr_str_fn musvg_text_parsers[] = {
    [musvg_type_enum]        = &musvg_read_text_enum,
    [musvg_type_id]          = &musvg_read_text_id,
    [musvg_type_length]      = &musvg_read_text_length,
    [musvg_type_color]       = &musvg_read_text_color,
    [musvg_type_transform]   = &musvg_read_text_transform,
    [musvg_type_dasharray]   = &musvg_read_text_dasharray,
    [musvg_type_float]       = &musvg_read_text_float,
    [musvg_type_viewbox]     = &musvg_read_text_viewbox,
    [musvg_type_aspectratio] = &musvg_read_text_aspectratio,
    [musvg_type_path]        = &musvg_read_text_path,
    [musvg_type_points]      = &musvg_read_text_points,
};

typedef int (*musvg_attr_buf_fn)(musvg_parser *p, mu_buf *buf, musvg_index node_idx, musvg_attr attr);

static const musvg_attr_buf_fn musvg_binary_parsers[] = {
    [musvg_type_enum]        = &musvg_read_binary_enum,
    [musvg_type_id]          = &musvg_read_binary_id,
    [musvg_type_length]      = &musvg_read_binary_length,
    [musvg_type_color]       = &musvg_read_binary_color,
    [musvg_type_transform]   = &musvg_read_binary_transform,
    [musvg_type_dasharray]   = &musvg_read_binary_dasharray,
    [musvg_type_float]       = &musvg_read_binary_float,
    [musvg_type_viewbox]     = &musvg_read_binary_viewbox,
    [musvg_type_aspectratio] = &musvg_read_binary_aspectratio,
    [musvg_type_path]        = &musvg_read_binary_path,
    [musvg_type_points]      = &musvg_read_binary_points,
};

static const musvg_attr_buf_fn musvg_binary_emitters[] = {
    [musvg_type_enum]        = &musvg_write_binary_enum,
    [musvg_type_id]          = &musvg_write_binary_id,
    [musvg_type_length]      = &musvg_write_binary_length,
    [musvg_type_color]       = &musvg_write_binary_color,
    [musvg_type_transform]   = &musvg_write_binary_transform,
    [musvg_type_dasharray]   = &musvg_write_binary_dasharray,
    [musvg_type_float]       = &musvg_write_binary_float,
    [musvg_type_viewbox]     = &musvg_write_binary_viewbox,
    [musvg_type_aspectratio] = &musvg_write_binary_aspectratio,
    [musvg_type_path]        = &musvg_write_binary_path,
    [musvg_type_points]      = &musvg_write_binary_points,
};

static const musvg_attr_buf_fn musvg_text_emitters[] = {
    [musvg_type_enum]        = &musvg_write_text_enum,
    [musvg_type_id]          = &musvg_write_text_id,
    [musvg_type_length]      = &musvg_write_text_length,
    [musvg_type_color]       = &musvg_write_text_color,
    [musvg_type_transform]   = &musvg_write_text_transform,
    [musvg_type_dasharray]   = &musvg_write_text_dasharray,
    [musvg_type_float]       = &musvg_write_text_float,
    [musvg_type_viewbox]     = &musvg_write_text_viewbox,
    [musvg_type_aspectratio] = &musvg_write_text_aspectratio,
    [musvg_type_path]        = &musvg_write_text_path,
    [musvg_type_points]      = &musvg_write_text_points,
};

// SVG attribute parsing

static void musvg_parse_style(musvg_parser* p, musvg_index node_idx, const char* str);

static int musvg_parse_attr(musvg_parser* p, musvg_index node_idx,
    const char* name, const char* value)
{
    if (strcmp(name, "style") == 0) {
        musvg_parse_style(p, node_idx, value);
    } else {
        for (size_t attr = 0; attr < array_size(musvg_attribute_names); attr++) {
            const char *attr_name = musvg_attribute_names[attr];
            if (attr_name && strcmp(name, attr_name) == 0) {
                musvg_attr_str_fn fn = musvg_text_parsers[musvg_attr_types[attr]];
                debugf("musvg_parse_attr: %s := %s\n", name, value);
                return fn(p, value, node_idx, as_attr(attr));
            }
        }
    }
    return 1;
}

static int musvg_parse_name_value(musvg_parser* p, musvg_index node_idx,
    const char* start, const char* end)
{
    const char* str;
    const char* val;
    char name[128];
    char value[384];
    int n;

    str = start;
    while (str < end && *str != ':') ++str;

    val = str;

    // Right Trim
    while (str > start &&  (*str == ':' || musvg_isspace(*str))) --str;
    ++str;

    n = (int)(str - start);
    if (n >= sizeof(name)) n = sizeof(name) - 1;
    if (n) memcpy(name, start, n);
    name[n] = 0;

    while (val < end && (*val == ':' || musvg_isspace(*val))) ++val;

    n = (int)(end - val);
    if (n >= sizeof(value)) n = sizeof(value) - 1;
    if (n) memcpy(value, val, n);
    value[n] = 0;

    return musvg_parse_attr(p, node_idx, name, value);
}

static void musvg_parse_style(musvg_parser* p, musvg_index node_idx, const char* str)
{
    const char *start, *end;

    debugf("musvg_parse_style: [%s]\n", str);

    while (*str)
    {
        // Left Trim
        while(*str && musvg_isspace(*str)) ++str;
        start = str;
        while(*str && *str != ';') ++str;
        end = str;

        // Right Trim
        while (end > start &&  (*end == ';' || musvg_isspace(*end))) --end;
        ++end;

        musvg_parse_name_value(p, node_idx, start, end);
        if (*str) ++str;
    }
}

// SVG emitters

void musvg_emit_text_begin(musvg_parser *p, void *userdata, musvg_index node_idx, uint depth, uint close)
{
    mu_buf *buf = (mu_buf *)userdata;
    for (int d = 0; d < depth; d++) mu_buf_write_string(buf, "\t");
    mu_buf_write_format(buf, "node %s {\n",
        musvg_element_names[node_type(p, node_idx)]);
    musvg_index slots[64];
    size_t sz = array_size(slots);
    musvg_node_attr_slots(p, node_idx, slots, &sz);
    for (size_t i = 0; i < sz; i++) {
        musvg_attr attr = slot_type(p, slots[i]);
        musvg_attr_buf_fn fn = musvg_text_emitters[musvg_attr_types[attr]];
        for (int d = 0; d < depth + 1; d++) mu_buf_write_string(buf, "\t");
        mu_buf_write_format(buf, "attr %s \"", musvg_attribute_names[attr]);
        fn(p, buf, node_idx, attr);
        mu_buf_write_string(buf, "\";\n");
    }
}

void musvg_emit_text_end(musvg_parser *p, void *userdata, musvg_index node_idx, uint depth, uint close)
{
    mu_buf *buf = (mu_buf *)userdata;
    for (int d = 0; d < depth; d++) mu_buf_write_string(buf, "\t");
    mu_buf_write_string(buf, "};\n");
}

void musvg_emit_xml_begin(musvg_parser *p, void *userdata, musvg_index node_idx, uint depth, uint close)
{
    mu_buf *buf = (mu_buf *)userdata;
    for (int d = 0; d < depth; d++) mu_buf_write_string(buf, "\t");
    mu_buf_write_i8(buf, '<');
    mu_buf_write_string(buf, musvg_element_names[node_type(p, node_idx)]);
    musvg_index slots[64];
    size_t sz = array_size(slots);
    musvg_node_attr_slots(p, node_idx, slots, &sz);
    for (size_t i = 0; i < sz; i++) {
        musvg_attr attr = slot_type(p, slots[i]);
        musvg_attr_buf_fn fn = musvg_text_emitters[musvg_attr_types[attr]];
        mu_buf_write_i8(buf, ' ');
        mu_buf_write_string(buf, musvg_attribute_names[attr]);
        mu_buf_write_string(buf, "=\"");
        fn(p, buf, node_idx, attr);
        mu_buf_write_i8(buf, '"');
    }
    if (close) mu_buf_write_i8(buf, '/');
    mu_buf_write_string(buf, ">\n\0");
}

void musvg_emit_xml_end(musvg_parser *p, void *userdata, musvg_index node_idx, uint depth, uint close)
{
    mu_buf *buf = (mu_buf *)userdata;
    if (close) return;
    for (int d = 0; d < depth; d++) mu_buf_write_string(buf, "\t");
    mu_buf_write_format(buf, "</%s>\n", musvg_element_names[node_type(p, node_idx)]);
}

void musvg_emit_binary_begin(musvg_parser *p, void *userdata, musvg_index node_idx, uint depth, uint close)
{
    mu_buf *buf = (mu_buf *)userdata;
    mu_buf_write_i8(buf, (char)node_type(p, node_idx));

    musvg_index slots[64];
    size_t sz = array_size(slots);
    musvg_node_attr_slots(p, node_idx, slots, &sz);
    for (size_t i = 0; i < sz; i++) {
        musvg_attr attr = slot_type(p, slots[i]);
        musvg_attr_buf_fn fn = musvg_binary_emitters[musvg_attr_types[attr]];
        mu_buf_write_i8(buf, attr);
        fn(p, buf, node_idx, attr);
    }
    mu_buf_write_i8(buf, musvg_attr_none);
}

void musvg_emit_binary_end(musvg_parser *p, void *userdata, musvg_index node_idx, uint depth, uint close)
{
    mu_buf *buf = (mu_buf *)userdata;
    mu_buf_write_i8(buf, musvg_element_none);
}

void musvg_visit_recurse(musvg_parser* p, void *userdata, musvg_index node_idx, uint d,
    musvg_node_visit_fn begin_fn, musvg_node_visit_fn end_fn)
{
    musvg_index down_idx, left_idx = node_idx;
    llong count = 0, i = 0;
    do {
        count++;
        left_idx = node_left(p, left_idx);
    } while (left_idx);
    musvg_index node_indices[count];
    left_idx = node_idx;
    do {
        left_idx = node_left(p, (node_indices[i++] = left_idx));
    } while (left_idx);
    while (i-- > 0) {
        left_idx = node_indices[i];
        down_idx = node_down(p, left_idx);
        int has_depth = !!down_idx;
        if (begin_fn) begin_fn(p, userdata, left_idx, d, !has_depth);
        if (has_depth) {
            musvg_visit_recurse(p, userdata, down_idx, d + 1, begin_fn, end_fn);
        }
        if (end_fn) end_fn(p, userdata, left_idx, d, !has_depth);
    }
}

void musvg_visit(musvg_parser* p, void *userdata, musvg_node_visit_fn begin_fn, musvg_node_visit_fn end_fn)
{
    /*
     * currently we construct the entire output in memory, however, it will be
     * possible to use the buffer size check callback to incrementally flush.
     */
    musvg_visit_recurse(p, userdata, 0, 0, begin_fn, end_fn);
}

void musvg_emit_text(musvg_parser* p, mu_buf *buf)
{
    musvg_visit(p, buf, musvg_emit_text_begin, musvg_emit_text_end);
}

void musvg_emit_xml(musvg_parser* p, mu_buf *buf)
{
    musvg_visit(p, buf, musvg_emit_xml_begin, musvg_emit_xml_end);
}

void musvg_emit_binary_vf(musvg_parser* p, mu_buf *buf)
{
    p->f32_write = mu_vf128_f32_write_byval;
    p->f32_write_vec = mu_vf128_f32_write_vec;
    musvg_visit(p, buf, musvg_emit_binary_begin, musvg_emit_binary_end);
}

void musvg_emit_binary_ieee(musvg_parser* p, mu_buf *buf)
{
    p->f32_write = mu_ieee754_f32_write_byval;
    p->f32_write_vec = mu_ieee754_f32_write_vec;
    musvg_visit(p, buf, musvg_emit_binary_begin, musvg_emit_binary_end);
}

int musvg_emit_buffer(musvg_parser* p, musvg_format_t format, mu_buf *buf)
{
    switch (format) {
    case musvg_format_text:        musvg_emit_text(p, buf);        break;
    case musvg_format_xml:         musvg_emit_xml(p, buf);         break;
    case musvg_format_binary_vf:   musvg_emit_binary_vf(p, buf);   break;
    case musvg_format_binary_ieee: musvg_emit_binary_ieee(p, buf); break;
    default: break;
    }
    return 0;
}

int musvg_emit_file(musvg_parser* p, musvg_format_t format, const char *filename)
{
    if (strcmp(filename,"-") == 0) {
        return musvg_emit_fd(p, format, fileno(stdout));
    }

    mu_buf *buf = mu_buffered_writer_new(filename);
    int ret = musvg_emit_buffer(p, format, buf);
    mu_buf_destroy(buf);
    return ret;
}

int musvg_emit_fd(musvg_parser* p, musvg_format_t format, int fd)
{
    mu_buf *buf = mu_buffered_writer_fd(fd);
    int ret = musvg_emit_buffer(p, format, buf);
    mu_buf_destroy(buf);
    return ret;
}

// SVG XML element callbacks

static void musvg_start_element(void* ud, const char* el, const char** a)
{
    musvg_parser* p = (musvg_parser*)ud;

    debugf("musvg_start_element: %s\n", el);

    for (size_t i = 0; i < array_size(musvg_element_names); i++) {
        const char *name = musvg_element_names[i];
        if (name && strcmp(el, name) == 0) {
            musvg_index node_idx = musvg_node_add(p, i);
            for (uint i = 0; a[i]; i += 2)
            {
                if (!musvg_parse_attr(p, node_idx, a[i], a[i + 1]))
                {
                    // todo
                }
            }
            return;
        }
    }
}

static void musvg_end_element(void* ud, const char* el)
{
    musvg_parser* p = (musvg_parser*)ud;

    debugf("musvg_end_element: %s\n", el);

    for (size_t i = 0; i < array_size(musvg_element_names); i++) {
        const char *name = musvg_element_names[i];
        if (name && strcmp(el, name) == 0) {
            musvg_stack_pop(p);
            return;
        }
    }
}

static void musvg_content(void* ud, const char* s)
{
    // empty
}

// SVG parsers

int musvg_parse_svg_xml(musvg_parser* p, mu_buf *buf)
{
    /* copy the source buffer due to xml parse modifying the
     * buffer to allow in-place zero-termination of attributes.
     * also make it look like we read from the source buffer. */
    mu_buf *tmp = mu_buf_new(buf->write_marker + 1);
    mu_buf_write_bytes(tmp, buf->data, buf->write_marker);
    mu_buf_write_i8(buf, 0);
    int ret = musvg_parse_xml(tmp->data, musvg_start_element,
                              musvg_end_element, musvg_content, p);
    buf->read_marker = buf->write_marker;
    mu_buf_destroy(tmp);
    return ret;
}

int musvg_parse_binary(musvg_parser *p, mu_buf *buf)
{
    musvg_small element, attr;

    for (;;) {
        if (!mu_buf_read_i8(buf, &element)) return 0;
        element = element % (musvg_element_limit + 1);
        if (element == musvg_element_none) {
            musvg_stack_pop(p);
            continue;
        }

        musvg_index node_idx = musvg_node_add(p, element);

        for (;;) {
            if (!mu_buf_read_i8(buf, &attr)) return -1;
            attr = attr % (musvg_attr_limit + 1);
            if (attr == musvg_attr_none) break;

            musvg_attr_buf_fn read_fn = musvg_binary_parsers[musvg_attr_types[attr]];
            int ret = read_fn(p, buf, node_idx, as_attr(attr));
        }
    }

    musvg_hash_sum(p);

    return 0;
}

int musvg_parse_binary_vf(musvg_parser* p, mu_buf *buf)
{
    p->f32_read = mu_vf128_f32_read;
    p->f32_read_vec = mu_vf128_f32_read_vec;
    return musvg_parse_binary(p, buf);
}

int  musvg_parse_binary_ieee(musvg_parser* p, mu_buf *buf)
{
    p->f32_read = mu_ieee754_f32_read;
    p->f32_read_vec = mu_ieee754_f32_read_vec;
    return musvg_parse_binary(p, buf);
}

int musvg_parse_buffer(musvg_parser* p, musvg_format_t format, mu_buf *buf)
{
    switch (format) {
    case musvg_format_xml:         return musvg_parse_svg_xml(p, buf);
    case musvg_format_binary_vf:   return musvg_parse_binary_vf(p, buf);
    case musvg_format_binary_ieee: return musvg_parse_binary_ieee(p, buf);
    default: return -1;
    }
}

int musvg_parse_file(musvg_parser* p, musvg_format_t format, const char *filename)
{
    if (strcmp(filename,"-") == 0) {
        return musvg_parse_fd(p, format, fileno(stdin));
    }

    musvg_span span = musvg_read_file(filename);
    mu_buf *buf = mu_buf_memory_new(span.data, span.size);
    int ret = musvg_parse_buffer(p, format, buf);
    mu_buf_destroy(buf);
    free(span.data);
    return ret;
}

int musvg_parse_fd(musvg_parser* p, musvg_format_t format, int fd)
{
    musvg_span span = musvg_read_fd(fd);
    mu_buf *buf = mu_buf_memory_new(span.data, span.size);
    int ret = musvg_parse_buffer(p, format, buf);
    mu_buf_destroy(buf);
    free(span.data);
    return ret;
}

// SVG parser ctor/dtor

void musvg_hash_work_fn(void *arg, size_t thr_idx, size_t item_idx);

musvg_parser* musvg_parser_create()
{
    musvg_parser* p = (musvg_parser*)malloc(sizeof(musvg_parser));
    memset(p,0,sizeof(musvg_parser));

    points_init(p);
    path_ops_init(p);
    path_points_init(p);
    brushes_init(p);
    nodes_init(p);
    slots_init(p);
    storage_init(p);
    strings_init(p);

    /* reserve element 0 */
    musvg_slot slot = { 0 };
    slots_add(p,&slot);
    storage_alloc(p,1,1);
    strings_alloc(p,1,1);

    assert(slots_count(p) == 1);
    assert(storage_size(p) == 1);
    assert(strings_size(p) == 1);

    mule_init(&p->mule, 1, musvg_hash_work_fn, p);
    mule_start(&p->mule);

    return p;
}

void musvg_parser_destroy(musvg_parser *p)
{
    points_destroy(p);
    path_ops_destroy(p);
    path_points_destroy(p);
    brushes_destroy(p);
    nodes_destroy(p);
    slots_destroy(p);
    storage_destroy(p);
    strings_destroy(p);

    mule_destroy(&p->mule);

    free(p);
}

// SVG parser stats

static void print_stats_titles()
{
    printf("%-15s %5s %10s %10s %10s %10s\n",
        "name",       "size",       "count",
        "capacity",   "used(B)",    "alloc(B)");
}
static void print_stats_lines()
{
    printf("%-15s %5s %10s %10s %10s %10s\n",
        "---------------", "-----", "----------",
        "----------", "----------", "----------");
}

static void print_array_stats(vec *ab, size_t stride, const char *name)
{
    printf("%-15s %5zu %10zu %10zu %10zu %10zu\n",
        name, stride, ab->count, ab->capacity,
        ab->count * stride, ab->capacity * stride);
}

static void print_storage_stats(storage_buffer *sb, const char *name)
{
    printf("%-15s %5u %10zu %10zu %10zu %10zu\n",
        name, 1, sb->offset, sb->capacity,
        sb->offset, sb->capacity);
}

static void print_summary_totals(musvg_parser *p)
{
    size_t capacity = nodes_capacity(p) + points_capacity(p) +
        path_ops_capacity(p) + path_points_capacity(p) +
        slots_capacity(p) + storage_capacity(p);
    size_t size = nodes_size(p) + points_size(p) +
        path_ops_size(p) + path_points_size(p) +
        slots_size(p) + storage_size(p);
    printf("%-15s %5s %10s %10s %10zu %10zu\n",
        "totals", "", "", "", size, capacity);
}

void musvg_parser_stats(musvg_parser* p)
{
    print_stats_titles();
    print_stats_lines();
    print_array_stats(&p->nodes, sizeof(musvg_node), "nodes");
    print_array_stats(&p->hashes, sizeof(musvg_hash), "hashes");
    print_array_stats(&p->slots, sizeof(musvg_slot), "slots");
    print_storage_stats(&p->storage, "storage");
    print_array_stats(&p->path_ops, sizeof(musvg_path_op), "path_ops");
    print_array_stats(&p->path_points, sizeof(musvg_points), "path_points");
    print_array_stats(&p->points, sizeof(float), "points");
    print_storage_stats(&p->strings, "strings");
    //print_array_stats(&p->brushes, sizeof(musvg_brush), "brushes");
    print_stats_lines();
    print_summary_totals(p);
}

// dump

#define _PRIDX MUSVG_INDEX_FORMAT
#define _PRTYPE "d"

void musvg_parser_dump(musvg_parser* p)
{
    printf("%7s%7s%5s%7s%7s%7s%5s%7s%7s%5s %s\n",
        "node", "parent", "type", "left", "down", "attr", "type", "left", "disp", "size", "value");
    printf("%7s%7s%5s%7s%7s%7s%5s%7s%7s%5s %s\n",
        "------", "------", "----", "------", "------", "------", "----", "------", "------", "----",
        "------------------------------------");
    for (musvg_index node_idx = 0; node_idx < nodes_count(p); node_idx++) {
        printf("%7" _PRIDX "%7" _PRIDX "%5" _PRTYPE "%7" _PRIDX "%7" _PRIDX "%7" _PRIDX "%7s%5s%7s%5s <%s>\n",
            node_idx, node_up(p, node_idx), node_type(p, node_idx), node_left(p, node_idx),
            node_down(p, node_idx), node_attr(p, node_idx), "", "", "", "",
            musvg_element_names[node_type(p, node_idx)]);
        musvg_index slot_idx = node_attr(p, node_idx);
        while (slot_idx) {
            musvg_attr attr = slot_type(p, slot_idx);
            musvg_type_t type = musvg_attr_types[attr];
            const char *type_name = musvg_type_names[type];
            size_t type_size = musvg_type_storage[type].size;
            musvg_attr_buf_fn fn = musvg_text_emitters[type];
            mu_buf *buf = mu_resizable_buf_new();
            fn(p, buf, node_idx, as_attr(attr));
            if (buf->write_marker > 21) {
                buf->data[19] = '.';
                buf->data[20] = '.';
                buf->data[21] = '\0';
            }
            mu_buf_write_i8(buf, 0);
            printf("%7s%7s%5s%7s%7s%7" _PRIDX "%5" _PRTYPE "%7" _PRIDX "%7" _PRIDX "%5zu  %s: %s(\"%s\")\n",
                "", "", "", "", "", slot_idx, attr, slot_left(p, slot_idx), slot_storage(p, slot_idx),
                type_size, musvg_attribute_names[attr], type_name, buf->data);
            mu_buf_destroy(buf);
            slot_idx = slot_left(p, slot_idx);
        }
    }
}

void musvg_parser_types()
{
    printf("%-14s %5s\n", "type", "size");
    printf("%-14s %5s\n", "--------------", "-----");
    for (size_t i = 0; i < array_size(musvg_type_names); i++) {
        printf("%-14s %5zu\n", musvg_type_names[i], musvg_type_storage[i].size * 8);
    }
}

// file io helper functions

musvg_span musvg_read_file(const char* filename)
{
    FILE* fp;
    musvg_span span;

    assert((fp = fopen(filename, "rb")));
    assert(!fseek(fp, 0, SEEK_END));
    assert((span.size = ftell(fp)));
    assert(!fseek(fp, 0, SEEK_SET));
    assert((span.data = (char*)malloc(span.size + 1)));
    assert(fread(span.data, 1, span.size, fp) == span.size);
    span.data[span.size] = '\0';
    assert(!fclose(fp));

    return span;
}

musvg_span musvg_read_fd(int fd)
{
    FILE* fp;
    musvg_span span;
    mu_buf *buf;
    char temp[4096];
    size_t nread;

    buf = mu_resizable_buf_new();
    buf->retain = 1; /* keep the buffer */
    assert((fp = fdopen(fd, "rb")));

    do {
        nread = fread(temp, 1, sizeof(temp), fp);
        if (nread > 1) {
            assert(mu_buf_write_bytes(buf, temp, nread) == nread);
        }
    } while (nread > 0);

    mu_buf_write_i8(buf, 0);

    span.data = buf->data;
    span.size = buf->write_marker;
    mu_buf_destroy(buf);

    return span;
}

int musvg_node_attr_types(musvg_parser *p, musvg_index node_idx, musvg_attr *types, size_t *count)
{
    size_t input_count = *count, i = 0, j = 0;

    musvg_index slot_idx = node_attr(p, node_idx);
    while (slot_idx) {
        slot_idx = slot_left(p, slot_idx);
        i++;
    }

    if (types) {
        musvg_index slot_idx = node_attr(p, node_idx);
        while (slot_idx) {
            if (i-j-1 < input_count) {
                types[i-j-1] = slot_type(p, slot_idx);
            }
            slot_idx = slot_left(p, slot_idx);
            j++;
        }
    }

    *count = i;
    return 0;
}

int musvg_node_attr_slots(musvg_parser *p, musvg_index node_idx, musvg_index *slots, size_t *count)
{
    size_t input_count = *count, i = 0, j = 0;

    musvg_index slot_idx = node_attr(p, node_idx);
    while (slot_idx) {
        slot_idx = slot_left(p, slot_idx);
        i++;
    }

    if (slots) {
        musvg_index slot_idx = node_attr(p, node_idx);
        while (slot_idx) {
            if (i-j-1 < input_count) {
                slots[i-j-1] = slot_idx;
            }
            slot_idx = slot_left(p, slot_idx);
            j++;
        }
    }

    *count = i;
    return 0;
}

int musvg_attr_value_set_value(musvg_parser *p, musvg_index node_idx, musvg_attr attr, const char *value, size_t len)
{
    musvg_index storage = find_attr(p, node_idx, attr);
    if (storage == 0) {
        /* node attribute offset entries must all be adjacent so if the last
         * entry on the node is not the last entry in the array then we need
         * to copy all the attribute map entries. */
        return -1;
    }
    musvg_attr_str_fn fn = musvg_text_parsers[musvg_attr_types[attr]];
    int ret = fn(p, value, node_idx, attr);
    return 0;
}

int musvg_attr_value_get(musvg_parser *p, musvg_index node_idx, musvg_attr attr, char *value, size_t *len)
{
    mu_buf *buf = mu_resizable_buf_new();
    musvg_attr_buf_fn fn = musvg_text_emitters[musvg_attr_types[attr]];
    fn(p, buf, node_idx, attr);
    mu_buf_write_i8(buf, 0);
    size_t input_len = *len;
    size_t output_len = input_len - 1 < buf->write_marker ? input_len - 1 : buf->write_marker;
    memcpy(value, buf->data, output_len);
    value[output_len] = '\0';
    *len = output_len;
    mu_buf_destroy(buf);
    return 0;
}

/* hashing */

static const char* hex_string(char *buf, size_t buflen, const uint8_t *data, size_t sz)
{
    size_t len = 0;
    for (size_t i = 0; i < sz; i++) {
        len += snprintf(buf + len, buflen - len, "%02hhx", data[i]);
    }
    buf[len] = '\0';
    return buf;
}

void musvg_hash_sum_begin(musvg_parser *p, void *userdata, musvg_index node_idx, uint depth, uint close)
{
    musvg_hash *hash = hashes_get(p, node_idx);
    mu_hash_init(&p->hash_ctx);
    mu_buf_reset(p->hash_buf);
    mu_buf_write_i8(p->hash_buf, (char)node_type(p, node_idx));
    musvg_index slot_idx = node_attr(p, node_idx);
    while (slot_idx) {
        musvg_attr attr = slot_type(p, slot_idx);
        musvg_type_t type = musvg_attr_types[attr];
        musvg_attr_buf_fn fn = musvg_binary_emitters[musvg_attr_types[attr]];
        mu_buf_write_i8(p->hash_buf, attr);
        fn(p, p->hash_buf, node_idx, attr);
        slot_idx = slot_left(p, slot_idx);
    }
    mu_buf_write_i8(p->hash_buf, musvg_attr_none);
    mu_hash_update(&p->hash_ctx, p->hash_buf->data, p->hash_buf->write_marker);
    mu_hash_final(&p->hash_ctx, (unsigned char*)hash->sum);
}

void musvg_hash_work_fn(void *arg, size_t thr_idx, size_t item_idx)
{
    musvg_parser *p = (musvg_parser*)arg;
    musvg_hash_sum_begin(p, NULL, item_idx, 0, 0);
}

void musvg_hash_sum(musvg_parser* p)
{
    p->hash_buf = mu_resizable_buf_new();
    hashes_resize(p, nodes_count(p));
    p->f32_write = mu_ieee754_f32_write_byval;
    p->f32_write_vec = mu_ieee754_f32_write_vec;
    musvg_visit(p, NULL, musvg_hash_sum_begin, NULL);
    mu_buf_destroy(p->hash_buf);
}

void musvg_hash_dump_begin(musvg_parser *p, void *userdata, musvg_index node_idx, uint depth, uint close)
{
    char hexbuf[257];
    musvg_hash *hash = hashes_get(p, node_idx);
    printf("node %7" _PRIDX " %56s\n", node_idx,
        hex_string(hexbuf, sizeof(hexbuf), hash->sum, mu_hash_len));
}

void musvg_hash_dump(musvg_parser* p)
{
    musvg_visit(p, NULL, musvg_hash_dump_begin, NULL);
}
