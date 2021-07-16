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
#include <stdlib.h>
#include <stdarg.h>
#include <stdalign.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <ctype.h>

#include "stdbits.h"
#include "vf128.h"
#include "musvg.h"

#ifndef M_PI
    #define M_PI 3.14159265358979323846264338327
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
    memset(sb->data, 0, stride * sb->capacity);
}

static void array_buffer_destroy(array_buffer *sb)
{
    free(sb->data);
    sb->data = NULL;
}

static uint array_buffer_count(array_buffer *sb)
{
    return sb->count;
}

static void* array_buffer_data(array_buffer *sb)
{
    return sb->data;
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

static void array_buffer_resize(array_buffer *sb, size_t stride, size_t count)
{
    if (sb->count + count > sb->capacity) {
        size_t new_capacity = pow2_ge(sb->count + count);
        sb->data = (char*)realloc(sb->data, stride * new_capacity);
        memset(sb->data + stride * sb->capacity, 0, stride * (new_capacity - sb->capacity));
        sb->capacity = new_capacity;
    }
}

static uint array_buffer_add(array_buffer *sb, size_t stride, void *ptr)
{
    array_buffer_resize(sb, stride, 1);
    uint idx = sb->count++;
    memcpy(sb->data + (idx * stride), ptr, stride);
    return idx;
}

static void* array_buffer_alloc(array_buffer *sb, size_t stride, size_t count)
{
    array_buffer_resize(sb, stride, count);
    uint idx = sb->count;
    sb->count += count;
    return sb->data + (idx * stride);
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
    memset(sb->data, 0, sb->capacity);
}

static void storage_buffer_destroy(storage_buffer *sb)
{
    free(sb->data);
    sb->data = NULL;
}

static void* storage_buffer_data(storage_buffer *sb)
{
    return sb->data;
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
        memset(sb->data + sb->capacity, 0, new_size - sb->capacity);
        sb->capacity = new_size;
    }
}

static uint storage_buffer_alloc(storage_buffer *sb, size_t size, size_t align)
{
    size_t offset = sb->offset, max_align = align > 8 ? 8 : align;
    uint our_offset = (offset + max_align - 1) & ~(max_align - 1);
    uint align_size = (size   + max_align - 1) & ~(max_align - 1);
    storage_buffer_resize(sb, our_offset + align_size);
    sb->offset = our_offset + align_size;
    return our_offset;
}

// SVG parser init

#define points_init(p) array_buffer_init(&p->points,sizeof(float),16)
#define points_count(p) array_buffer_count(&p->points)
#define points_data(p) array_buffer_data(&p->points)
#define points_size(p) array_buffer_size(&p->points,sizeof(float))
#define points_capacity(p) array_buffer_capacity(&p->points,sizeof(float))
#define points_get(p,idx) ((float*)array_buffer_get(&p->points,sizeof(float),idx))
#define points_resize(p) array_buffer_resize(&p->points,sizeof(float))
#define points_add(p,ptr) array_buffer_add(&p->points,sizeof(float),ptr)
#define points_alloc(p,count) array_buffer_alloc(&p->points,sizeof(float),count)
#define points_destroy(p) array_buffer_destroy(&p->points)

#define path_ops_init(p) array_buffer_init(&p->path_ops,sizeof(musvg_path_op),16)
#define path_ops_count(p) array_buffer_count(&p->path_ops)
#define path_ops_data(p) array_buffer_data(&p->path_ops)
#define path_ops_size(p) array_buffer_size(&p->path_ops,sizeof(musvg_path_op))
#define path_ops_capacity(p) array_buffer_capacity(&p->path_ops,sizeof(musvg_path_op))
#define path_ops_get(p,idx) ((musvg_path_op*)array_buffer_get(&p->path_ops,sizeof(musvg_path_op),idx))
#define path_ops_resize(p) array_buffer_resize(&p->path_ops,sizeof(musvg_path_op))
#define path_ops_add(p,ptr) array_buffer_add(&p->path_ops,sizeof(musvg_path_op),ptr)
#define path_ops_destroy(p) array_buffer_destroy(&p->path_ops)

#define brushes_init(p) array_buffer_init(&p->brushes,sizeof(musvg_brush),16)
#define brushes_count(p) array_buffer_count(&p->brushes)
#define brushes_data(p) array_buffer_data(&p->brushes)
#define brushes_size(p) array_buffer_size(&p->brushes,sizeof(musvg_brush))
#define brushes_capacity(p) array_buffer_capacity(&p->brushes,sizeof(musvg_brush))
#define brushes_get(p,idx) ((musvg_brush*)array_buffer_get(&p->brushes,sizeof(musvg_brush),idx))
#define brushes_resize(p) array_buffer_resize(&p->brushes,sizeof(musvg_brushes))
#define brushes_add(p,ptr) array_buffer_add(&p->brushes,sizeof(musvg_brush),ptr)
#define brushes_destroy(p) array_buffer_destroy(&p->brushes)

#define nodes_init(p) array_buffer_init(&p->nodes,sizeof(musvg_node),512)
#define nodes_count(p) array_buffer_count(&p->nodes)
#define nodes_data(p) array_buffer_data(&p->nodes)
#define nodes_size(p) array_buffer_size(&p->nodes,sizeof(musvg_node))
#define nodes_capacity(p) array_buffer_capacity(&p->nodes,sizeof(musvg_node))
#define nodes_get(p,idx) ((musvg_node*)array_buffer_get(&p->nodes,sizeof(musvg_node),idx))
#define nodes_resize(p) array_buffer_resize(&p->nodes,sizeof(musvg_node))
#define nodes_alloc(p,count) array_buffer_alloc(&p->nodes,sizeof(musvg_node),count)
#define nodes_destroy(p) array_buffer_destroy(&p->nodes)

#define offsets_init(p) array_buffer_init(&p->offsets,sizeof(musvg_offset),512)
#define offsets_count(p) array_buffer_count(&p->offsets)
#define offsets_data(p) array_buffer_data(&p->offsets)
#define offsets_size(p) array_buffer_size(&p->offsets,sizeof(musvg_offset))
#define offsets_capacity(p) array_buffer_capacity(&p->offsets,sizeof(musvg_offset))
#define offsets_get(p,idx) ((musvg_offset*)array_buffer_get(&p->offsets,sizeof(musvg_offset),idx))
#define offsets_resize(p) array_buffer_resize(&p->offsets,sizeof(musvg_offset))
#define offsets_add(p,ptr) array_buffer_add(&p->offsets,sizeof(musvg_offset),ptr)
#define offsets_destroy(p) array_buffer_destroy(&p->offsets)

#define storage_init(p) storage_buffer_init(&p->storage,512)
#define storage_data(p) storage_buffer_data(&p->storage)
#define storage_size(p) storage_buffer_size(&p->storage)
#define storage_capacity(p) storage_buffer_capacity(&p->storage)
#define storage_get(p,idx) ((char*)storage_buffer_get(&p->storage,idx))
#define storage_resize(p,size) storage_buffer_resize(&p->storage,size)
#define storage_alloc(p,size,align) storage_buffer_alloc(&p->storage,size,align)
#define storage_destroy(p) storage_buffer_destroy(&p->storage)

enum { musvg_max_depth = 256 };

struct musvg_parser
{
    array_buffer points;
    array_buffer path_ops;
    array_buffer brushes;
    array_buffer nodes;
    array_buffer offsets;
    storage_buffer storage;

    uint node_stack[musvg_max_depth];
    uint node_depth;
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
    [musvg_element_svg]                    = "svg",
    [musvg_element_g]                      = "g",
    [musvg_element_defs]                   = "defs",
    [musvg_element_path]                   = "path",
    [musvg_element_rect]                   = "rect",
    [musvg_element_circle]                 = "circle",
    [musvg_element_ellipse]                = "ellipse",
    [musvg_element_line]                   = "line",
    [musvg_element_polyline]               = "polyline",
    [musvg_element_polygon]                = "polygon",
    [musvg_element_lgradient]              = "linearGradient",
    [musvg_element_rgradient]              = "radialGradient",
    [musvg_element_stop]                   = "stop",
};

static const char * musvg_attribute_names[] = {
    [musvg_attr_display]                   = "display",
    [musvg_attr_fill]                      = "fill",
    [musvg_attr_fill_opacity]              = "fill-opacity",
    [musvg_attr_fill_rule]                 = "fill-rule",
    [musvg_attr_font_size]                 = "font-size",
    [musvg_attr_id]                        = "id",
    [musvg_attr_offset]                    = "offset",
    [musvg_attr_stop_color]                = "stop-color",
    [musvg_attr_stop_opacity]              = "stop-opacity",
    [musvg_attr_stroke]                    = "stroke",
    [musvg_attr_stroke_width]              = "stroke-width",
    [musvg_attr_stroke_dasharray]          = "stroke-dasharray",
    [musvg_attr_stroke_dashoffset]         = "stroke-dashoffset",
    [musvg_attr_stroke_opacity]            = "stroke-opacity",
    [musvg_attr_stroke_linecap]            = "stroke-linecap",
    [musvg_attr_stroke_linejoin]           = "stroke-linejoin",
    [musvg_attr_stroke_miterlimit]         = "stroke-miterlimit",
    [musvg_attr_style]                     = "style",
    [musvg_attr_transform]                 = "transform",
    [musvg_attr_svg_viewbox]               = "viewBox",
    [musvg_attr_svg_aspectratio]           = "preserveAspectRatio",
    [musvg_attr_path_d]                    = "d",
    [musvg_attr_poly_points]               = "points",
    [musvg_attr_width]                     = "width",
    [musvg_attr_height]                    = "height",
    [musvg_attr_x]                         = "x",
    [musvg_attr_y]                         = "y",
    [musvg_attr_r]                         = "r",
    [musvg_attr_rx]                        = "rx",
    [musvg_attr_ry]                        = "ry",
    [musvg_attr_cx]                        = "cx",
    [musvg_attr_cy]                        = "cy",
    [musvg_attr_x1]                        = "x1",
    [musvg_attr_y1]                        = "y1",
    [musvg_attr_x2]                        = "x2",
    [musvg_attr_y2]                        = "y2",
    [musvg_attr_fx]                        = "fx",
    [musvg_attr_fy]                        = "fy",
    [musvg_attr_gradient_units]            = "gradientUnits",
    [musvg_attr_gradient_transform]        = "gradientTransform",
    [musvg_attr_gradient_spread]           = "spreadMethod",
    [musvg_attr_gradient_href]             = "xlink:href",
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

static inline uint musvg_path_opcode_t_arg_count(uint opcode)
{
    static const int arg_counts[] = {
        0, 0, 2, 2, 2, 2, 6, 6, 4, 4, 7, 7, 1, 1, 1, 1, 4, 4, 2, 2,
    };
    return arg_counts[opcode];
}

static inline char musvg_path_opcode_t_cmd_char(uint opcode)
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

static const char * musvg_brush_names[] = {
    [musvg_brush_default]         = "default",
    [musvg_brush_color]           = "color",
    [musvg_brush_linear_gradient] = "linearGradient",
    [musvg_brush_radial_gradient] = "radialGradient",
};

static const char * musvg_align_names[] = {
    [musvg_align_default] = "default",
    [musvg_align_none] = "none",
    [musvg_align_min] = "Min",
    [musvg_align_mid] = "Mid",
    [musvg_align_max] = "Max",
};

static const char * musvg_crop_names[] = {
    [musvg_crop_default] = "default",
    [musvg_crop_none] = "none",
    [musvg_crop_meet] = "meet",
    [musvg_crop_slice] = "slice",
};

static const char * musvg_gradient_spread_names[] = {
    [musvg_gradient_spread_default] = "default",
    [musvg_gradient_spread_pad]     = "pad",
    [musvg_gradient_spread_reflect] = "reflect",
    [musvg_gradient_spread_repeat]  = "repeat",
};

static const char * musvg_gradient_unit_names[] = {
    [musvg_gradient_unit_default] = "default",
    [musvg_gradient_unit_user]    = "userSpaceOnUse",
    [musvg_gradient_unit_obb]     = "objectBoundingBox",
};

static const char * musvg_linecap_names[] = {
    [musvg_linecap_default] = "default",
    [musvg_linecap_butt]    = "butt",
    [musvg_linecap_round]   = "round",
    [musvg_linecap_square]  = "square",
};

static const char * musvg_linejoin_names[] = {
    [musvg_linejoin_default] = "default",
    [musvg_linejoin_miter]   = "miter",
    [musvg_linejoin_round]   = "round",
    [musvg_linejoin_bevel]   = "bevel",
};

static const char * musvg_fillrule_names[] = {
    [musvg_fillrule_default] = "default",
    [musvg_fillrule_nonzero] = "nonzero",
    [musvg_fillrule_evenodd] = "evenodd",
};

static const char * musvg_display_names[] = {
    [musvg_display_default] = "default",
    [musvg_display_none]    = "none",
    [musvg_display_inline]  = "inline",
};

static const char * musvg_unit_names[] = {
    [musvg_unit_default] = "default",
    [musvg_unit_user]    = "user",
    [musvg_unit_px]      = "px",
    [musvg_unit_pt]      = "pt",
    [musvg_unit_pc]      = "pc",
    [musvg_unit_mm]      = "mm",
    [musvg_unit_cm]      = "cm",
    [musvg_unit_in]      = "in",
    [musvg_unit_percent] = "%",
    [musvg_unit_em]      = "em",
    [musvg_unit_ex]      = "ex",
};

static const char * musvg_transform_names[] = {
    [musvg_transform_matrix]    = "matrix",
    [musvg_transform_translate] = "translate",
    [musvg_transform_scale]     = "scale",
    [musvg_transform_rotate]    = "rotate",
    [musvg_transform_skew_x]    = "skewX",
    [musvg_transform_skew_y]    = "skeyY",
};

static const musvg_typeinfo_attr musvg_type_info_attr[] =
{
    /* common attributes */
    [musvg_attr_id]                 = { musvg_type_id },
    [musvg_attr_transform]          = { musvg_type_transform },
    [musvg_attr_fill]               = { musvg_type_color },
    [musvg_attr_stroke]             = { musvg_type_color },
    [musvg_attr_fill_opacity]       = { musvg_type_float },
    [musvg_attr_stroke_opacity]     = { musvg_type_float },
    [musvg_attr_stroke_miterlimit]  = { musvg_type_float },
    [musvg_attr_stroke_width]       = { musvg_type_length },
    [musvg_attr_stroke_dashoffset]  = { musvg_type_length },
    [musvg_attr_stroke_dasharray]   = { musvg_type_dasharray },
    [musvg_attr_stroke_linejoin]    = { musvg_type_enum },
    [musvg_attr_stroke_linecap]     = { musvg_type_enum },
    [musvg_attr_fill_rule]          = { musvg_type_enum },
    [musvg_attr_display]            = { musvg_type_enum },
    [musvg_attr_font_size]          = { musvg_type_length },
    [musvg_attr_stop_color]         = { musvg_type_color },
    [musvg_attr_stop_opacity]       = { musvg_type_float },
    [musvg_attr_offset]             = { musvg_type_length },
    /* element specific attributes */
    [musvg_attr_svg_viewbox]        = { musvg_type_viewbox },
    [musvg_attr_svg_aspectratio]    = { musvg_type_aspectratio },
    [musvg_attr_path_d]             = { musvg_type_path },
    [musvg_attr_poly_points]        = { musvg_type_points },
    [musvg_attr_width]              = { musvg_type_length },
    [musvg_attr_height]             = { musvg_type_length },
    [musvg_attr_x]                  = { musvg_type_length },
    [musvg_attr_y]                  = { musvg_type_length },
    [musvg_attr_r]                  = { musvg_type_length },
    [musvg_attr_rx]                 = { musvg_type_length },
    [musvg_attr_ry]                 = { musvg_type_length },
    [musvg_attr_cx]                 = { musvg_type_length },
    [musvg_attr_cy]                 = { musvg_type_length },
    [musvg_attr_x1]                 = { musvg_type_length },
    [musvg_attr_y1]                 = { musvg_type_length },
    [musvg_attr_x2]                 = { musvg_type_length },
    [musvg_attr_y2]                 = { musvg_type_length },
    [musvg_attr_fx]                 = { musvg_type_length },
    [musvg_attr_fy]                 = { musvg_type_length },
    [musvg_attr_gradient_spread]    = { musvg_type_enum },
    [musvg_attr_gradient_units]     = { musvg_type_enum },
    [musvg_attr_gradient_transform] = { musvg_type_transform },
    [musvg_attr_gradient_href]      = { musvg_type_id },
};

static const musvg_typeinfo_enum musvg_type_info_enum[] =
{
    [musvg_attr_stroke_linejoin]    = { musvg_linejoin_names,        musvg_linejoin_LIMIT,        musvg_linejoin_DEFAULT,       musvg_parse_linejoin },
    [musvg_attr_stroke_linecap]     = { musvg_linecap_names,         musvg_linecap_LIMIT,         musvg_linecap_DEFAULT,        musvg_parse_linecap  },
    [musvg_attr_fill_rule]          = { musvg_fillrule_names,        musvg_fillrule_LIMIT,        musvg_fillrule_DEFAULT,       musvg_parse_fillrule },
    [musvg_attr_display]            = { musvg_display_names,         musvg_display_LIMIT,         musvg_display_DEFAULT,        musvg_parse_display  },
    [musvg_attr_gradient_spread]    = { musvg_gradient_spread_names, musvg_gradient_spread_LIMIT, musvg_gradient_spread_DEFAULT, musvg_parse_gradient_spread },
    [musvg_attr_gradient_units]     = { musvg_gradient_unit_names,   musvg_gradient_unit_LIMIT,   musvg_gradient_unit_DEFAULT,   musvg_parse_gradient_units  },
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
    musvg_color color = { (((unsigned int)r << 16) | ((unsigned int)g << 8) | ((unsigned int)b << 0)), 1 };
    return color;
}

static inline musvg_color musvg_color_none()
{
    musvg_color color = { 0, 0 };
    return color;
}

static musvg_color musvg_parse_color_name(const char* str)
{
    int i, ncolors = sizeof(musvg_colors) / sizeof(musvg_named_color);

    for (i = 0; i < ncolors; i++) {
        if (strcmp(musvg_colors[i].name, str) == 0) {
            musvg_color color = { musvg_colors[i].color, 1 };
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

static musvg_color musvg_parse_color(const char* str)
{
    size_t len = 0;
    while(*str == ' ') ++str;
    len = strlen(str);
    if (strcmp(str, "none") == 0)
        return musvg_color_none();
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
    else if (strcmp(format, "binary-ieee") == 0)
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

musvg_small musvg_parse_gradient_spread(const char* str)
{
    if (strcmp(str, "pad") == 0)
        return musvg_gradient_spread_pad;
    else if (strcmp(str, "reflect") == 0)
        return musvg_gradient_spread_reflect;
    else if (strcmp(str, "repeat") == 0)
        return musvg_gradient_spread_repeat;
    return musvg_gradient_spread_default;
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
    else if (strstr(str, isx ? "xMin" : "yMin") != 0)
        return musvg_align_min;
    else if (strstr(str, isx ? "xMid" : "yMid") != 0)
        return musvg_align_mid;
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
    if (ar->alignX == musvg_align_none || ar->alignY == musvg_align_none || ar->alignType == musvg_crop_none) {
        len += snprintf(buf+len, buflen-len, "none");
    } else {
        int alignX = ar->alignX == musvg_align_default ? musvg_align_DEFAULT : ar->alignX;
        int alignY = ar->alignY == musvg_align_default ? musvg_align_DEFAULT : ar->alignY;
        int alignType = ar->alignType == musvg_crop_default ? musvg_crop_DEFAULT : ar->alignType;
        len += snprintf(buf+len, buflen-len, "x%sy%s %s",
            musvg_align_names[alignX], musvg_align_names[alignY], musvg_crop_names[alignType]);
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
    for (size_t i = 0; i < da->count; i++) {
        len += snprintf(buf+len, buflen-len, "%s%.8g", i > 0 ? "," : "", da->dashes[i]);
    }
    return len;
}

static musvg_id musvg_parse_id(const char* str)
{
    musvg_id id;
    strncpy(id.name, str, sizeof(id.name) - 1);
    id.name[sizeof(id.name) - 1] = '\0';
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
    int nargs, opc, code, argc;
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
            argc = musvg_path_opcode_t_arg_count(code);
            if (code != 0 && argc == 0) {
                musvg_path_op path_op = { code, 0, 0 };
                path_ops_add(p, &path_op);
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
            musvg_path_op path_op = { code, points_offset, argc };
            path_ops_add(p, &path_op);
        }
        nargs = (nargs + 1) % argc;
    }

    ops.op_count = path_ops_count(p) - ops.op_offset;

    return ops;
}

// SVG attribute parsing

static void musvg_parse_style(musvg_parser* p, musvg_node* node, const char* str);
static int musvg_parse_attr(musvg_parser* p, musvg_node* node, const char* name, const char* value);

static int musvg_parse_name_value(musvg_parser* p, musvg_node* node, const char* start, const char* end)
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

    return musvg_parse_attr(p, node, name, value);
}

static void musvg_parse_style(musvg_parser* p, musvg_node* node, const char* str)
{
    debugf("musvg_parse_style: [%s]\n", str);

    const char* start;
    const char* end;

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

        musvg_parse_name_value(p, node, start, end);
        if (*str) ++str;
    }
}

// SVG node stack

static int musvg_stack_top(musvg_parser *p)
{
    if (p->node_depth == 0) return musvg_node_sentinel;
    else return p->node_stack[p->node_depth - 1];
}

static void musvg_stack_push(musvg_parser *p)
{
    if (p->node_depth == musvg_max_depth) abort();
    int parent = musvg_stack_top(p);
    int depth = p->node_depth++;
    int previous = p->node_stack[depth];
    int current = nodes_count(p) - 1;
    musvg_node *adjacent = nodes_get(p, previous);
    if (adjacent->parent == parent && parent != musvg_node_sentinel) {
        adjacent->next = current;
    }
    p->node_stack[depth] = current;
}

static void musvg_stack_pop(musvg_parser *p)
{
    if (p->node_depth == 0) abort();
    p->node_depth--;
}

static musvg_node* musvg_node_add(musvg_parser *p, uint type)
{
    musvg_node *node = (musvg_node*)nodes_alloc(p, 1);
    node->type = type;
    node->next = musvg_node_sentinel;
    node->parent = musvg_stack_top(p);
    return node;
}

// SVG attribute storage

static inline int node_find_attr(musvg_parser *p, const musvg_node *node, musvg_attr_t attr)
{
    /* search attribute list backwards as a temporal affinity optimization */
    for (int i = node->attr_count - 1; i >= 0; i--) {
        musvg_offset *offset = offsets_get(p, node->attr_offset + i);
        if (offset->attr_type == attr) return node->attr_offset + i;
    }

    return -1;
}

static inline musvg_offset alloc_attr(musvg_parser *p, musvg_node *node, musvg_attr_t attr)
{
    size_t type = musvg_type_info_attr[attr].type;
    size_t size = musvg_type_storage[type].size;
    size_t align = musvg_type_storage[type].align;
    musvg_offset o = { attr, storage_alloc(p, size, align) };

    uint idx = offsets_add(p,&o);
    if (node->attr_count == 0) {
        node->attr_count++;
        node->attr_offset = idx;
    } else {
        node->attr_count++;
        assert(node->attr_offset + node->attr_count - 1 == idx);
    }

    return o;
}

static inline musvg_small* attr_pointer(musvg_parser *p, musvg_node *node, musvg_attr_t attr)
{
    /*
     * search for the attribute in this nodes offset table, and if not
     * found, allocate aligned storage and record the offset in the table.
     *
     * alloc attr assumes constraint that attributes are written contiguously
     * such as the case when parsing xml or binary, but not random writes.
     */
    musvg_offset o;
    int attr_idx = node_find_attr(p, node, attr);
    if (attr_idx == -1) {
        o = alloc_attr(p, node, attr);
    } else {
        o = *offsets_get(p, attr_idx);
    }
    return (musvg_small*)storage_get(p, o.attr_offset);
}

static inline musvg_small parse_enum(musvg_attr_t attr, const char *s)
{
    return musvg_type_info_enum[attr].parse(s);
}

static inline musvg_small enum_modulus(musvg_attr_t attr)
{
    return musvg_type_info_enum[attr].limit + 1;
}

// SVG element callbacks

static void musvg_start_element(void* ud, const char* el, const char** a)
{
    musvg_parser* p = (musvg_parser*)ud;

    debugf("musvg_start_element: %s\n", el);

    for (size_t i = 0; i < array_size(musvg_element_names); i++) {
        const char *name = musvg_element_names[i];
        if (name && strcmp(el, name) == 0) {
            musvg_node *node = musvg_node_add(p, i);
            for (uint i = 0; a[i]; i += 2)
            {
                if (!musvg_parse_attr(p, node, a[i], a[i + 1]))
                {
                    // todo
                }
            }
            musvg_stack_push(p);
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

musvg_parser* musvg_parser_create()
{
    musvg_parser* p = (musvg_parser*)malloc(sizeof(musvg_parser));
    memset(p,0,sizeof(musvg_parser));
    points_init(p);
    path_ops_init(p);
    brushes_init(p);
    nodes_init(p);
    offsets_init(p);
    storage_init(p);

    storage_alloc(p,1,1);
    assert(p->storage.offset == 1);

    return p;
}

// binary readers

int (*f32_read)(vf_buf *buf, float *value);
int (*f32_write_byval)(vf_buf *buf, const float value);

int musvg_read_binary_enum(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    musvg_small *enum_value = (musvg_small*)attr_pointer(p, node, attr);
    assert(vf_buf_read_i8(buf, enum_value));
    *enum_value = *enum_value  % enum_modulus(attr);
    return 0;
}

int musvg_read_binary_id(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    musvg_id *id = (musvg_id*)attr_pointer(p, node, attr);
    ullong id_name_len = 0;
    assert(!vlu_u64_read(buf, &id_name_len));
    assert(id_name_len < sizeof(id->name));
    assert(vf_buf_read_bytes(buf, id->name, id_name_len));
    return 0;
}

int musvg_read_binary_length(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    musvg_length *length = (musvg_length*)attr_pointer(p, node, attr);
    assert(!f32_read(buf, &length->value));
    assert(vf_buf_read_i8(buf, &length->units));
    return 0;
}

int musvg_read_binary_color(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    musvg_color *color = (musvg_color*)attr_pointer(p, node, attr);
    assert(vf_buf_read_i8(buf, (int8_t*)&color->present));
    if (color->present) {
        assert(vf_buf_read_i32(buf, (int32_t*)&color->color));
    }
    return 0;
}

int musvg_read_binary_transform(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    musvg_transform *xf = (musvg_transform*)attr_pointer(p, node, attr);
    assert(vf_buf_read_i8(buf, (int8_t*)&xf->type));
    if (xf->type == musvg_transform_matrix) {
        xf->nargs = 0;
        for (size_t i = 0; i < 6; i++) {
            assert(!f32_read(buf, &xf->xform[i]));
        }
    } else {
        assert(vf_buf_read_i8(buf, (int8_t*)&xf->nargs));
        for (size_t i = 0; i < xf->nargs; i++) {
            assert(!f32_read(buf, &xf->args[i]));
        }
    }
    return 0;
}

int musvg_read_binary_dasharray(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    musvg_dasharray *da = (musvg_dasharray*)attr_pointer(p, node, attr);
    assert(vf_buf_read_i8(buf, (int8_t*)&da->count));
    for (size_t i = 0; i < da->count; i++) {
        assert(!f32_read(buf, &da->dashes[i]));
    }
    return 0;
}

int musvg_read_binary_float(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    float *value = (float*)attr_pointer(p, node, attr);
    assert(!f32_read(buf, value));
    return 0;
}

int musvg_read_binary_viewbox(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    musvg_viewbox *vb = (musvg_viewbox*)attr_pointer(p, node, attr);
    assert(!f32_read(buf, &vb->x));
    assert(!f32_read(buf, &vb->y));
    assert(!f32_read(buf, &vb->width));
    assert(!f32_read(buf, &vb->height));
    return 0;
}

int musvg_read_binary_aspectratio(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    musvg_aspectratio *ar = (musvg_aspectratio*)attr_pointer(p, node, attr);
    assert(vf_buf_read_i8(buf, &ar->alignX));
    assert(vf_buf_read_i8(buf, &ar->alignY));
    assert(vf_buf_read_i8(buf, &ar->alignType));
    return 0;
}

int musvg_read_binary_path(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    musvg_path_d *pd = (musvg_path_d*)attr_pointer(p, node, attr);
    ullong count = 0;
    assert(!vlu_u64_read(buf, &count));
    musvg_path_d ops = { path_ops_count(p), count };
    *pd = ops;
    for (uint j = 0; j < ops.op_count; j++) {
        ullong count = 0; musvg_small code = 0;
        assert(vf_buf_read_i8(buf, (int8_t*)&code));
        assert(!vlu_u64_read(buf, &count));
        musvg_path_op path_op = { code, points_count(p), count };
        path_ops_add(p, &path_op);
        float *points_arr = points_alloc(p, path_op.point_count);
        for (uint k = 0; k < path_op.point_count; k++) {
            assert(!f32_read(buf, points_arr + k));
        }
    }
    return 0;
}

int musvg_read_binary_points(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    musvg_points *pp = (musvg_points*)attr_pointer(p, node, attr);
    ullong count = 0;
    assert(!vlu_u64_read(buf, &count));
    musvg_points points = { points_count(p), count };
    *pp = points;
    float *points_arr = points_alloc(p, points.point_count);
    for (uint j = 0; j < points.point_count; j++) {
        assert(!f32_read(buf, points_arr + j));
    }
    return 0;
}

// binary writers

int musvg_write_binary_enum(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    const musvg_small value = *attr_pointer(p, node, attr) % enum_modulus(attr);
    assert(vf_buf_write_i8(buf, value));
    return 0;
}

int musvg_write_binary_id(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    const musvg_id id = *(musvg_id*)attr_pointer(p, node, attr);
    const ullong id_name_len = strlen(id.name);
    assert(!vlu_u64_write(buf, &id_name_len));
    assert(vf_buf_write_bytes(buf, id.name, id_name_len));
    return 0;
}

int musvg_write_binary_length(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    const musvg_length length = *(musvg_length*)attr_pointer(p, node, attr);
    assert(!f32_write_byval(buf, length.value));
    assert(vf_buf_write_i8(buf, length.units));
    return 0;
}

int musvg_write_binary_color(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    const musvg_color color = *(musvg_color*)attr_pointer(p, node, attr);
    assert(vf_buf_write_i8(buf, (int8_t)color.present));
    if (color.present) {
        assert(vf_buf_write_i32(buf, (int32_t)color.color));
    }
    return 0;
}

int musvg_write_binary_transform(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    const musvg_transform xf = *(musvg_transform*)attr_pointer(p, node, attr);
    assert(vf_buf_write_i8(buf, (int8_t)xf.type));
    if (xf.type == musvg_transform_matrix) {
        for (size_t i = 0; i < 6; i++) {
            assert(!f32_write_byval(buf, xf.xform[i]));
        }
    } else {
        assert(vf_buf_write_i8(buf, (int8_t)xf.nargs));
        for (size_t i = 0; i < xf.nargs; i++) {
            assert(!f32_write_byval(buf, xf.args[i]));
        }
    }
    return 0;
}

int musvg_write_binary_dasharray(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    const musvg_dasharray da = *(musvg_dasharray*)attr_pointer(p, node, attr);
    assert(vf_buf_write_i8(buf, (int8_t)da.count));
    for (size_t i = 0; i < da.count; i++) {
        assert(!f32_write_byval(buf, da.dashes[i]));
    }
    return 0;
}

int musvg_write_binary_float(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    const float value = *(float*)attr_pointer(p, node, attr);
    assert(!f32_write_byval(buf, value));
    return 0;
}

int musvg_write_binary_viewbox(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    const musvg_viewbox vb = *(musvg_viewbox*)attr_pointer(p, node, attr);
    assert(!f32_write_byval(buf, vb.x));
    assert(!f32_write_byval(buf, vb.y));
    assert(!f32_write_byval(buf, vb.width));
    assert(!f32_write_byval(buf, vb.height));
    return 0;
}

int musvg_write_binary_aspectratio(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    const musvg_aspectratio ar = *(musvg_aspectratio*)attr_pointer(p, node, attr);
    assert(vf_buf_write_i8(buf, ar.alignX));
    assert(vf_buf_write_i8(buf, ar.alignY));
    assert(vf_buf_write_i8(buf, ar.alignType));
    return 0;
}

int musvg_write_binary_path(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    musvg_path_d ops = *(musvg_path_d*)attr_pointer(p, node, attr);
    ullong count = ops.op_count;
    assert(!vlu_u64_write(buf, &count));
    for (uint j = 0; j < ops.op_count; j++) {
        const  musvg_path_op *path_op = path_ops_get(p, ops.op_offset + j);
        musvg_small code = path_op->code;
        ullong count = path_op->point_count;
        assert(vf_buf_write_i8(buf, (int8_t)code));
        assert(!vlu_u64_write(buf, &count));
        const float *v = points_get(p, path_op->point_offset);
        for (uint k = 0; k < path_op->point_count; k++) {
            assert(!f32_write_byval(buf, v[k]));
        }
    }
    return 0;
}

int musvg_write_binary_points(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    musvg_points points = *(musvg_points*)attr_pointer(p, node, attr);
    const float *v = points_get(p, points.point_offset);
    ullong count = points.point_count;
    assert(!vlu_u64_write(buf, &count));
    for (uint j = 0; j < points.point_count; j++) {
        assert(!f32_write_byval(buf, v[j]));
    }
    return 0;
}

// attribute parsers

int musvg_read_text_enum(musvg_parser *p, const char *s, musvg_node *node, musvg_attr_t attr)
{
    musvg_small val = parse_enum(attr, s);
    musvg_small *ptr = (musvg_small*)attr_pointer(p, node, attr);
    *ptr = val;
    return 0;
}

int musvg_read_text_id(musvg_parser *p, const char *s, musvg_node *node, musvg_attr_t attr)
{
    musvg_id val = musvg_parse_id(s);
    musvg_id *ptr = (musvg_id*)attr_pointer(p, node, attr);
    *ptr = val;
    return 0;
}

int musvg_read_text_length(musvg_parser *p, const char *s, musvg_node *node, musvg_attr_t attr)
{
    musvg_length val = musvg_parse_length(s);
    musvg_length *ptr = (musvg_length*)attr_pointer(p, node, attr);
    *ptr = val;
    return 0;
}

int musvg_read_text_color(musvg_parser *p, const char *s, musvg_node *node, musvg_attr_t attr)
{
    musvg_color val = musvg_parse_color(s);
    musvg_color *ptr = (musvg_color*)attr_pointer(p, node, attr);
    *ptr = val;
    return 0;
}

int musvg_read_text_transform(musvg_parser *p, const char *s, musvg_node *node, musvg_attr_t attr)
{
    musvg_transform val = musvg_parse_transform(s);
    musvg_transform *ptr = (musvg_transform*)attr_pointer(p, node, attr);
    *ptr = val;
    return 0;
}

int musvg_read_text_dasharray(musvg_parser *p, const char *s, musvg_node *node, musvg_attr_t attr)
{
    musvg_dasharray val = musvg_parse_stroke_dasharray(s);
    musvg_dasharray *ptr = (musvg_dasharray*)attr_pointer(p, node, attr);
    *ptr = val;
    return 0;
}

int musvg_read_text_float(musvg_parser *p, const char *s, musvg_node *node, musvg_attr_t attr)
{
    float val = (float)musvg_atof(s);
    float *ptr = (float*)attr_pointer(p, node, attr);
    *ptr = val;
    return 0;
}

int musvg_read_text_viewbox(musvg_parser *p, const char *s, musvg_node *node, musvg_attr_t attr)
{
    musvg_viewbox val = musvg_parse_viewbox(s);
    musvg_viewbox *ptr = (musvg_viewbox*)attr_pointer(p, node, attr);
    *ptr = val;
    return 0;
}

int musvg_read_text_aspectratio(musvg_parser *p, const char *s, musvg_node *node, musvg_attr_t attr)
{
    musvg_aspectratio val = musvg_parse_aspectratio(s);
    musvg_aspectratio *ptr = (musvg_aspectratio*)attr_pointer(p, node, attr);
    *ptr = val;
    return 0;
}

int musvg_read_text_path(musvg_parser *p, const char *s, musvg_node *node, musvg_attr_t attr)
{
    musvg_path_d val = musvg_parse_path_ops(p, s);
    musvg_path_d *ptr = (musvg_path_d*)attr_pointer(p, node, attr);
    *ptr = val;
    return 0;
}

int musvg_read_text_points(musvg_parser *p, const char *s, musvg_node *node, musvg_attr_t attr)
{
    musvg_points val = musvg_parse_points(p, s);
    musvg_points *ptr = (musvg_points*)attr_pointer(p, node, attr);
    *ptr = val;
    return 0;
}

// text writers

int musvg_write_text_enum(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    const musvg_small enum_value = *attr_pointer(p, node, attr) % enum_modulus(attr);
    const char *enum_name = musvg_type_info_enum[attr].names[enum_value];
    assert(vf_buf_write_bytes(buf, enum_name, strlen(enum_name)));
    return 0;
}

int musvg_write_text_id(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    const musvg_id id = *(musvg_id*)attr_pointer(p, node, attr);
    const ullong id_name_len = strlen(id.name);
    assert(vf_buf_write_bytes(buf, id.name, id_name_len));
    return 0;
}

int musvg_write_text_length(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    const musvg_length length = *(musvg_length*)attr_pointer(p, node, attr);
    char str[64];
    int len = snprintf(str, sizeof(str), "%.8g", length.value);
    if (length.units != musvg_unit_DEFAULT) {
        len += snprintf(str + len, sizeof(str) - len, "%s",
            musvg_unit_names[length.units]);
    }
    assert(vf_buf_write_bytes(buf, str, len));
    return 0;
}

int musvg_write_text_color(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    const musvg_color color = *(musvg_color*)attr_pointer(p, node, attr);
    if (color.present) {
        char str[64];
        int len = snprintf(str, sizeof(str), "#%06x", color.color);
        assert(vf_buf_write_bytes(buf, str, len));
    } else {
        assert(vf_buf_write_bytes(buf, "none", 4));
    }
    return 0;
}

int musvg_write_text_transform(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    const musvg_transform xf = *(musvg_transform*)attr_pointer(p, node, attr);
    char str[128];
    int len = musvg_transform_string(str, sizeof(str), &xf);
    assert(vf_buf_write_bytes(buf, str, len));
    return 0;
}

int musvg_write_text_dasharray(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    const musvg_dasharray da = *(musvg_dasharray*)attr_pointer(p, node, attr);
    char str[128];
    int len = musvg_dasharray_string(str, sizeof(str), &da);
    assert(vf_buf_write_bytes(buf, str, len));
    return 0;
}

int musvg_write_text_float(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    const float value = *(float*)attr_pointer(p, node, attr);
    char str[128];
    int len = snprintf(str, sizeof(str), "%.8f", value);
    assert(vf_buf_write_bytes(buf, str, len));
    return 0;
}

int musvg_write_text_viewbox(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    const musvg_viewbox vb = *(musvg_viewbox*)attr_pointer(p, node, attr);
    char str[128];
    int len = musvg_viewbox_string(str, sizeof(str), &vb);
    assert(vf_buf_write_bytes(buf, str, len));
    return 0;
}

int musvg_write_text_aspectratio(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    const musvg_aspectratio ar = *(musvg_aspectratio*)attr_pointer(p, node, attr);
    char str[128];
    int len = musvg_aspectratio_string(str, sizeof(str), &ar);
    assert(vf_buf_write_bytes(buf, str, len));
    return 0;
}

int musvg_write_text_path(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    musvg_path_d ops = *(musvg_path_d*)attr_pointer(p, node, attr);
    char last_code = 0;
    for (uint j = 0; j < ops.op_count; j++) {
        char str[128];
        const musvg_path_op *path_op = path_ops_get(p, ops.op_offset + j);
        int8_t code = musvg_path_opcode_t_cmd_char(path_op->code);
        assert(vf_buf_write_i8(buf, code != last_code ? code : ' '));
        const float *v = points_get(p, path_op->point_offset);
        for (uint k = 0; k < path_op->point_count; k++) {
            if (k > 0) assert(vf_buf_write_i8(buf, ','));
            int len = snprintf(str, sizeof(str), "%.8g", v[k]);
            assert(vf_buf_write_bytes(buf, str, len));
        }
        last_code = code;
    }
    return 0;
}

int musvg_write_text_points(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr)
{
    musvg_points points = *(musvg_points*)attr_pointer(p, node, attr);
    const float *v = points_get(p, points.point_offset);
    for (uint j = 0; j < points.point_count; j++) {
        char str[128];
        if (j > 0) assert(vf_buf_write_i8(buf, j % 2 ? ',' : ' '));
        int len = snprintf(str, sizeof(str), "%.8g", v[j]);
        assert(vf_buf_write_bytes(buf, str, len));
    }
    return 0;
}

// type metadata

typedef int (*musvg_attr_str_fn)(musvg_parser *p, const char *s, musvg_node *node, musvg_attr_t attr);

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

typedef int (*musvg_attr_buf_fn)(musvg_parser *p, musvg_buf *buf, musvg_node *node, musvg_attr_t attr);

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

// attribute parsing

static int musvg_parse_attr(musvg_parser* p, musvg_node* node, const char* name, const char* value)
{
    if (strcmp(name, "style") == 0) {
        musvg_parse_style(p, node, value);
    } else {
        for (size_t i = 0; i < array_size(musvg_attribute_names); i++) {
            const char *attr_name = musvg_attribute_names[i];
            if (attr_name && strcmp(name, attr_name) == 0) {
                const musvg_typeinfo_attr *ti = musvg_type_info_attr + i;
                musvg_attr_str_fn fn = musvg_text_parsers[ti->type];
                debugf("musvg_parse_attr: %s := %s\n", name, value);
                return fn(p, value, node, i);
            }
        }
    }
    return 1;
}

// emiters

void musvg_emit_text_begin(musvg_parser *p, musvg_buf *buf, musvg_node *node, uint depth, uint close)
{
    for (int d = 0; d < depth; d++) vf_buf_write_string(buf, "\t");
    vf_buf_write_format(buf, "node %s {\n",
        musvg_element_names[node->type]);
    for (int i = 0; i < node->attr_count; i++) {
        musvg_offset *offset = offsets_get(p, node->attr_offset + i);
        int attr = offset->attr_type;
        const musvg_typeinfo_attr *ti = musvg_type_info_attr + attr;
        musvg_attr_buf_fn fn = musvg_text_emitters[ti->type];
        for (int d = 0; d < depth + 1; d++) vf_buf_write_string(buf, "\t");
        vf_buf_write_format(buf, "attr %s \"", musvg_attribute_names[attr]);
        fn(p, buf, node, attr);
        vf_buf_write_string(buf, "\";\n");
    }
}

void musvg_emit_text_end(musvg_parser *p, musvg_buf *buf, musvg_node *node, uint depth, uint close)
{
    for (int d = 0; d < depth; d++) vf_buf_write_string(buf, "\t");
    vf_buf_write_string(buf, "};\n");
}

void musvg_emit_xml_begin(musvg_parser *p, musvg_buf *buf, musvg_node *node, uint depth, uint close)
{
    for (int d = 0; d < depth; d++) vf_buf_write_string(buf, "\t");
    vf_buf_write_i8(buf, '<');
    vf_buf_write_string(buf, musvg_element_names[node->type]);
    for (int i = 0; i < node->attr_count; i++) {
        musvg_offset *offset = offsets_get(p, node->attr_offset + i);
        int attr = offset->attr_type;
        const musvg_typeinfo_attr *ti = musvg_type_info_attr + attr;
        musvg_attr_buf_fn fn = musvg_text_emitters[ti->type];
        vf_buf_write_i8(buf, ' ');
        vf_buf_write_string(buf, musvg_attribute_names[attr]);
        vf_buf_write_string(buf, "=\"");
        fn(p, buf, node, attr);
        vf_buf_write_i8(buf, '"');
    }
    if (close) vf_buf_write_i8(buf, '/');
    vf_buf_write_string(buf, ">\n\0");
}

void musvg_emit_xml_end(musvg_parser *p, musvg_buf *buf, musvg_node *node, uint depth, uint close)
{
    if (close) return;
    for (int d = 0; d < depth; d++) vf_buf_write_string(buf, "\t");
    vf_buf_write_format(buf, "</%s>\n", musvg_element_names[node->type]);
}

void musvg_emit_binary_begin(musvg_parser *p, musvg_buf *buf, musvg_node *node, uint depth, uint close)
{
    vf_buf_write_i8(buf, node->type);
    for (int i = 0; i < node->attr_count; i++) {
        musvg_offset *offset = offsets_get(p, node->attr_offset + i);
        int attr = offset->attr_type;
        const musvg_typeinfo_attr *ti = musvg_type_info_attr + attr;
        musvg_attr_buf_fn fn = musvg_binary_emitters[ti->type];
        vf_buf_write_i8(buf, attr);
        fn(p, buf, node, attr);
    }
    vf_buf_write_i8(buf, musvg_attr_none);
}

void musvg_emit_binary_end(musvg_parser *p, musvg_buf *buf, musvg_node *node, uint depth, uint close)
{
    vf_buf_write_i8(buf, musvg_element_none);
}

typedef void (*musvg_node_fn)(musvg_parser *p, musvg_buf *buf, musvg_node *node, uint depth, uint close);

void musvg_emit_recurse(musvg_parser* p, musvg_buf *buf, uint i, uint j, uint d,
    musvg_node_fn begin_fn, musvg_node_fn end_fn)
{
    while (i != musvg_node_sentinel && i < nodes_count(p))
    {
        musvg_node *node = nodes_get(p, i);
        int k = node->next != musvg_node_sentinel ? node->next : j;
        int has_depth = i + 1 < k;
        if (begin_fn) begin_fn(p, buf, nodes_get(p, i), d, !has_depth);
        if (has_depth) {
            musvg_emit_recurse(p, buf, i + 1, k, d + 1, begin_fn, end_fn);
        }
        if (end_fn) end_fn(p, buf, nodes_get(p, i), d, !has_depth);
        i = node->next;
    }
}

void musvg_emit(musvg_parser* p, musvg_buf *buf, musvg_node_fn begin, musvg_node_fn end)
{
    /*
     * currently we construct the entire output in memory, however, it will be
     * possible to use the buffer size check callback to incrementally flush.
     */
    musvg_emit_recurse(p, buf, 0, nodes_count(p), 0, begin, end);
}

void musvg_emit_text(musvg_parser* p, musvg_buf *buf)
{
    musvg_emit(p, buf, musvg_emit_text_begin, musvg_emit_text_end);
}

void musvg_emit_xml(musvg_parser* p, musvg_buf *buf)
{
    musvg_emit(p, buf, musvg_emit_xml_begin, musvg_emit_xml_end);
}

void musvg_emit_binary_vf(musvg_parser* p, musvg_buf *buf)
{
    f32_write_byval = vf_f32_write_byval; /* todo - thread safety */
    musvg_emit(p, buf, musvg_emit_binary_begin, musvg_emit_binary_end);
}

void musvg_emit_binary_ieee(musvg_parser* p, musvg_buf *buf)
{
    f32_write_byval = ieee754_f32_write_byval; /* todo - thread safety */
    musvg_emit(p, buf, musvg_emit_binary_begin, musvg_emit_binary_end);
}

int musvg_emit_buffer(musvg_parser* p, musvg_format_t format, musvg_buf *buf)
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
    musvg_buf *buf = vf_buffered_writer_new(filename);
    int ret = musvg_emit_buffer(p, format, buf);
    vf_buf_destroy(buf);
    return ret;
}

int musvg_parse_binary(musvg_parser *p, musvg_buf *buf)
{
    musvg_small element, attr;

    for (;;) {
        if (!vf_buf_read_i8(buf, &element)) return 0;
        element = element % (musvg_element_LIMIT + 1);
        if (element == musvg_element_none) {
            musvg_stack_pop(p);
            continue;
        }

        musvg_node *node = musvg_node_add(p, element);
        musvg_stack_push(p);

        for (;;) {
            if (!vf_buf_read_i8(buf, &attr)) return -1;
            attr = attr % (musvg_attr_LIMIT + 1);
            if (attr == musvg_attr_none) break;

            const musvg_typeinfo_attr *ti = musvg_type_info_attr + attr;
            musvg_attr_buf_fn read_fn = musvg_binary_parsers[ti->type];
            int ret = read_fn(p, buf, node, attr);
        }
    }

    return 0;
}

static void print_stats_titles()
{
    printf("%-10s %10s %10s %10s %10s %10s\n",
        "name", "size", "count", "capacity", "used(B)", "alloc(B)");
}
static void print_stats_lines()
{
    printf("%-10s %10s %10s %10s %10s %10s\n",
        "----------", "----------", "----------", "----------", "----------", "----------");
}

static void print_array_stats(array_buffer *ab, size_t stride, const char *name)
{
    printf("%-10s %10zu %10zu %10zu %10zu %10zu\n",
        name, stride, ab->count, ab->capacity, ab->count * stride, ab->capacity * stride);
}

static void print_storage_stats(storage_buffer *sb, const char *name)
{
    printf("%-10s %10s %10s %10s %10zu %10zu\n",
        name, "", "", "", sb->offset, sb->capacity);
}

static void print_summary_totals(musvg_parser *p)
{
    size_t capacity = nodes_capacity(p) + points_capacity(p) +
        path_ops_capacity(p) + offsets_capacity(p) + storage_capacity(p);
    size_t size = nodes_size(p) + points_size(p) + path_ops_size(p) +
        offsets_size(p) + storage_size(p);
    printf("%-10s %10s %10s %10s %10zu %10zu\n",
        "totals", "", "", "", size, capacity);
}

void musvg_parser_stats(musvg_parser* p)
{
    print_stats_titles();
    print_stats_lines();
    print_array_stats(&p->nodes, sizeof(musvg_node), "nodes");
    print_array_stats(&p->points, sizeof(float), "points");
    print_array_stats(&p->path_ops, sizeof(musvg_path_op), "path_ops");
    print_array_stats(&p->offsets, sizeof(musvg_offset), "attr_map");
    //print_array_stats(&p->brushes, sizeof(musvg_brush), "brushes");
    print_storage_stats(&p->storage, "storage");
    print_stats_lines();
    print_summary_totals(p);
}

void musvg_parser_destroy(musvg_parser *p)
{
    points_destroy(p);
    path_ops_destroy(p);
    brushes_destroy(p);
    nodes_destroy(p);
    offsets_destroy(p);
    storage_destroy(p);
    free(p);
}

int musvg_parse_svg_xml(musvg_parser* p, musvg_buf *buf)
{
    return musvg_parse_xml(buf->data, musvg_start_element, musvg_end_element, musvg_content, p);
}

int musvg_parse_binary_vf(musvg_parser* p, musvg_buf *buf)
{
    f32_read = vf_f32_read;
    return musvg_parse_binary(p, buf);
}

int  musvg_parse_binary_ieee(musvg_parser* p, musvg_buf *buf)
{
    f32_read = ieee754_f32_read;
    return musvg_parse_binary(p, buf);
}

int musvg_parse_buffer(musvg_parser* p, musvg_format_t format, musvg_buf *buf)
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
    musvg_span span = musvg_read_file(filename);
    musvg_buf *buf = vf_buf_memory_new(span.data, span.size);
    int ret = musvg_parse_buffer(p, format, buf);
    vf_buf_destroy(buf);
    free(span.data);
    return ret;
}

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
