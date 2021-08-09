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
 * The SVG parser is derviced from nanosvg and Anti-Grain Geometry 2.4 SVG example
 * Copyright (C) 2002-2004 Maxim Shemanarev (McSeem) (http://www.antigrain.com/)
 * Copyright (c) 2013-14 Mikko Mononen memon@inside.org (https://github.com/memononen/nanosvg)
 * Copyright (c) 2021 Michael Clark <michaeljclark@mac.com>
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// SVG forward decls

typedef unsigned uint;
typedef long long llong;
typedef unsigned long long ullong;
typedef signed char musvg_small;

#ifdef __SIZEOF_POINTER__
# define MUSVG_POINTER_SIZE __SIZEOF_POINTER__
#elif defined(_WIN64)
# define MUSVG_POINTER_SIZE 8
#elif defined(_WIN32)
# define MUSVG_POINTER_SIZE 4
#elif defined(_MSC_VER)
# error "could not determine MUSVG_POINTER_SIZE constant for MSVC"
#else
# define MUSVG_POINTER_SIZE sizeof(void *)
#endif

#if MUSVG_POINTER_SIZE == 4
typedef int musvg_index;
#define MUSVG_INDEX_FORMAT "d"
#elif MUSVG_POINTER_SIZE == 8
typedef llong musvg_index;
#define MUSVG_INDEX_FORMAT "lld"
#endif

#ifndef __cplusplus
typedef enum musvg_path_opcode_t musvg_path_opcode_t;
typedef enum musvg_format_t musvg_format_t;
typedef enum musvg_brush_t musvg_brush_t;
typedef enum musvg_spread_t musvg_spread_t;
typedef enum musvg_linejoin_t musvg_linejoin_t;
typedef enum musvg_linecap_t musvg_linecap_t;
typedef enum musvg_fillrule_t musvg_fillrule_t;
typedef enum musvg_unit_t musvg_unit_t;
typedef enum musvg_display_t musvg_display_t;
typedef enum musvg_align_t musvg_align_t;
typedef enum musvg_crop_t musvg_crop_t;
typedef enum musvg_color_type_t musvg_color_type_t;
typedef enum musvg_gradient_unit_t musvg_gradient_unit_t;
typedef enum musvg_spread_method_t musvg_spread_method_t;
typedef enum musvg_element musvg_element;
typedef enum musvg_attr musvg_attr;
typedef enum musvg_type_t musvg_type_t;
#endif

typedef struct musvg_span musvg_span;
typedef struct musvg_id musvg_id;
typedef struct musvg_length musvg_length;
typedef struct musvg_color musvg_color;
typedef struct musvg_viewbox musvg_viewbox;
typedef struct musvg_aspectratio musvg_aspectratio;
typedef struct musvg_points musvg_points;
typedef struct musvg_path_d musvg_path_d;
typedef struct musvg_transform musvg_transform;
typedef struct musvg_dasharray musvg_dasharray;
typedef struct musvg_path_op musvg_path_op;
typedef struct musvg_typeinfo_enum musvg_typeinfo_enum;
typedef struct musvg_gradient_stop musvg_gradient_stop;
typedef struct musvg_linear_gradient musvg_linear_gradient;
typedef struct musvg_radial_gradient musvg_radial_gradient;
typedef struct musvg_brush musvg_brush;
typedef struct musvg_named_color musvg_named_color;
typedef struct musvg_parser musvg_parser;
typedef struct musvg_node musvg_node;

// SVG enums

enum musvg_format_t {
    musvg_format_none,
    musvg_format_text,
    musvg_format_xml,
    musvg_format_binary_vf,
    musvg_format_binary_ieee,
};
enum musvg_element {
    musvg_element_none,
    musvg_element_svg,
    musvg_element_g,
    musvg_element_defs,
    musvg_element_path,
    musvg_element_rect,
    musvg_element_circle,
    musvg_element_ellipse,
    musvg_element_line,
    musvg_element_polyline,
    musvg_element_polygon,
    musvg_element_linear_gradient,
    musvg_element_radial_gradient,
    musvg_element_stop,
    musvg_element_limit = musvg_element_stop
};
enum musvg_attr {
    musvg_attr_none,
    musvg_attr_display,
    musvg_attr_fill,
    musvg_attr_fill_opacity,
    musvg_attr_fill_rule,
    musvg_attr_font_size,
    musvg_attr_id,
    musvg_attr_stroke,
    musvg_attr_stroke_width,
    musvg_attr_stroke_dasharray,
    musvg_attr_stroke_dashoffset,
    musvg_attr_stroke_opacity,
    musvg_attr_stroke_linecap,
    musvg_attr_stroke_linejoin,
    musvg_attr_stroke_miterlimit,
    musvg_attr_style,
    musvg_attr_transform,
    musvg_attr_d,
    musvg_attr_points,
    musvg_attr_width,
    musvg_attr_height,
    musvg_attr_x,
    musvg_attr_y,
    musvg_attr_r,
    musvg_attr_rx,
    musvg_attr_ry,
    musvg_attr_cx,
    musvg_attr_cy,
    musvg_attr_x1,
    musvg_attr_y1,
    musvg_attr_x2,
    musvg_attr_y2,
    musvg_attr_fx,
    musvg_attr_fy,
    musvg_attr_offset,
    musvg_attr_stop_color,
    musvg_attr_stop_opacity,
    musvg_attr_gradient_units,
    musvg_attr_gradient_transform,
    musvg_attr_spread_method,
    musvg_attr_view_box,
    musvg_attr_preserve_aspect_ratio,
    musvg_attr_xmlns,
    musvg_attr_xmlns_xlink,
    musvg_attr_xlink_href,
    musvg_attr_limit = musvg_attr_xlink_href
};
/*
 * SVG path instructions from the 'd' attribute:
 *
 * closepath                    Zz ()
 * moveto_abs                   M (x y)+
 * moveto_rel                   m (x y)+
 * lineto_abs                   L (x y)+
 * lineto_rel                   l (x y)+
 * curveto_cubic_abs            C (x1 y1 x2 y2 x y)+
 * curveto_cubic_rel            c (x1 y1 x2 y2 x y)+
 * quadratic_curve_to_abs       Q (x2 y2 x y)+
 * quadratic_curve_to_rel       q (x2 y2 x y)+
 * eliptical_arc_abs            A (rx ry xr af sf x y)+
 * eliptical_arc_rel            a (rx ry xr af sf x y)+
 * line_to_horizontal_abs       H (x)+
 * line_to_horizontal_rel       h (x)+
 * line_to_vertical_abs         V (y)+
 * line_to_vertical_rel         v (y)+
 * curveto_cubic_smooth_abs     S (x2 y2 x y)+
 * curveto_cubic_smooth_rel     s (x2 y2 x y)+
 * curveto_quadratic_smooth_abs T (x y)+
 * curveto_quadratic_smooth_rel t (x y)+
 */
enum musvg_path_opcode_t {
    musvg_path_none,
    musvg_path_closepath,
    musvg_path_moveto_abs,
    musvg_path_moveto_rel,
    musvg_path_lineto_abs,
    musvg_path_lineto_rel,
    musvg_path_curveto_cubic_abs,
    musvg_path_curveto_cubic_rel,
    musvg_path_quadratic_curve_to_abs,
    musvg_path_quadratic_curve_to_rel,
    musvg_path_eliptical_arc_abs,
    musvg_path_eliptical_arc_rel,
    musvg_path_line_to_horizontal_abs,
    musvg_path_line_to_horizontal_rel,
    musvg_path_line_to_vertical_abs,
    musvg_path_line_to_vertical_rel,
    musvg_path_curveto_cubic_smooth_abs,
    musvg_path_curveto_cubic_smooth_rel,
    musvg_path_curveto_quadratic_smooth_abs,
    musvg_path_curveto_quadratic_smooth_rel,
};
enum musvg_brush_t {
    musvg_brush_color,
    musvg_brush_linear_gradient,
    musvg_brush_radial_gradient
};
enum musvg_linecap_t {
    musvg_linecap_butt,
    musvg_linecap_round,
    musvg_linecap_square,
    musvg_linecap_limit = musvg_linecap_square,
    musvg_linecap_default = musvg_linecap_butt
};
enum musvg_linejoin_t {
    musvg_linejoin_miter,
    musvg_linejoin_round,
    musvg_linejoin_bevel,
    musvg_linejoin_limit = musvg_linejoin_bevel,
    musvg_linejoin_default = musvg_linejoin_miter
};
enum musvg_fillrule_t {
    musvg_fillrule_nonzero,
    musvg_fillrule_evenodd,
    musvg_fillrule_limit = musvg_fillrule_evenodd,
    musvg_fillrule_default = musvg_fillrule_nonzero
};
enum musvg_unit_t {
    musvg_unit_user,
    musvg_unit_px,
    musvg_unit_pt,
    musvg_unit_pc,
    musvg_unit_mm,
    musvg_unit_cm,
    musvg_unit_in,
    musvg_unit_percent,
    musvg_unit_em,
    musvg_unit_ex,
    musvg_unit_limit = musvg_unit_ex,
    musvg_unit_default = musvg_unit_user
};
enum musvg_display_t {
    musvg_display_inline,
    musvg_display_none,
    musvg_display_limit = musvg_display_none,
    musvg_display_default = musvg_display_inline
};
enum musvg_align_t {
    musvg_align_mid,
    musvg_align_min,
    musvg_align_max,
    musvg_align_none,
    musvg_align_default = musvg_align_mid
};
enum musvg_crop_t {
    musvg_crop_meet,
    musvg_crop_slice,
    musvg_crop_none,
    musvg_crop_default = musvg_crop_meet
};
enum musvg_color_type_t {
    musvg_color_type_none,
    musvg_color_type_rgba,
    musvg_color_type_url,
};
enum musvg_spread_method_t {
    musvg_spread_method_pad,
    musvg_spread_method_reflect,
    musvg_spread_method_repeat,
    musvg_spread_method_limit = musvg_spread_method_repeat,
    musvg_spread_method_default = musvg_spread_method_pad
};
enum musvg_gradient_unit_t {
    musvg_gradient_unit_user,
    musvg_gradient_unit_obb,
    musvg_gradient_unit_limit = musvg_gradient_unit_obb,
    musvg_gradient_unit_default = musvg_gradient_unit_user
};
enum musvg_transform_t {
    musvg_transform_matrix,
    musvg_transform_translate,
    musvg_transform_scale,
    musvg_transform_rotate,
    musvg_transform_skew_x,
    musvg_transform_skew_y,
};

// SVG primitives

struct musvg_span
{
    char* data;
    size_t size;
};

struct musvg_id
{
    musvg_index name;
};

struct musvg_length
{
    float value;
    musvg_small units;
};

struct musvg_color
{
    ullong type : 16;
    ullong data : 48;
};

struct musvg_transform
{
    musvg_small type;
    musvg_small nargs;
    float args[3];
    float xform[6];
};

struct musvg_dasharray
{
    float dashes[8];
    musvg_small count;
};

struct musvg_viewbox
{
    float x;
    float y;
    float width;
    float height;
};

struct musvg_aspectratio
{
    musvg_small alignX;
    musvg_small alignY;
    musvg_small alignType;
};

struct musvg_points
{
    musvg_index point_offset;
    musvg_index point_count;
};

struct musvg_path_op
{
    musvg_small code;
};

struct musvg_path_d
{
    musvg_index op_offset;
    musvg_index op_count;
};

// SVG type metadata

enum musvg_type_t
{
    musvg_type_enum,
    musvg_type_id,
    musvg_type_length,
    musvg_type_color,
    musvg_type_transform,
    musvg_type_dasharray,
    musvg_type_float,
    musvg_type_viewbox,
    musvg_type_aspectratio,
    musvg_type_path,
    musvg_type_points,
};

typedef musvg_small (*parse_enum_fn)(const char* units);

struct musvg_typeinfo_enum
{
    const char ** names;
    size_t limit;
    parse_enum_fn parse;
};

// SVG named color

struct musvg_named_color
{
    uint color;
    const char name[32];
};

// SVG (compiled) gradients

struct musvg_gradient_stop { uint color; float offset; };
struct musvg_linear_gradient { float x1, y1, x2, y2; };
struct musvg_radial_gradient { float cx, cy, r, fx, fy; };

struct musvg_brush
{
    musvg_brush_t type;
    uint flat_color;
    uint point_offset, point_count;
    uint stop_offset, stop_count;
};

// SVG IO

typedef struct mu_buf mu_buf;

musvg_parser* musvg_parser_create();
void musvg_parser_destroy(musvg_parser* p);
void musvg_parser_stats(musvg_parser* p);
void musvg_parser_dump(musvg_parser* p);
void musvg_parser_types();

musvg_small musvg_parse_format(const char *format);

int musvg_emit_buffer(musvg_parser* p, musvg_format_t format, mu_buf *buf);
int musvg_emit_file(musvg_parser* p, musvg_format_t format, const char *filename);
int musvg_emit_fd(musvg_parser* p, musvg_format_t format, int fd);

int musvg_parse_buffer(musvg_parser* p, musvg_format_t format, mu_buf *buf);
int musvg_parse_file(musvg_parser* p, musvg_format_t format, const char *filename);
int musvg_parse_fd(musvg_parser* p, musvg_format_t format, int fd);

typedef void (*musvg_node_visit_fn)(musvg_parser *, void *, musvg_node *, uint depth, uint close);
void musvg_visit(musvg_parser* p, void *userdata, musvg_node_visit_fn begin_fn, musvg_node_visit_fn end_fn);

musvg_span musvg_read_file(const char* filename);
musvg_span musvg_read_fd(int fd);

/* delta api */

int musvg_node_attr_types(musvg_parser *p, musvg_node *node, musvg_attr *types, size_t *count);
int musvg_node_attr_slots(musvg_parser *p, musvg_node *node, musvg_index *slots, size_t *count);
int musvg_attr_value_set(musvg_parser *p, musvg_node *node, musvg_attr attr, const char *value, size_t len);
int musvg_attr_value_get(musvg_parser *p, musvg_node *node, musvg_attr attr, char *value, size_t *len);

#ifdef __cplusplus
}
#endif
