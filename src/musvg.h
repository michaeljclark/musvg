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
typedef unsigned long long ullong;
typedef signed char musvg_small;

#ifndef __cplusplus
typedef enum musvg_path_opcode musvg_path_opcode;
typedef enum musvg_brush_type musvg_brush_type;
typedef enum musvg_spread_type musvg_spread_type;
typedef enum musvg_linejoin_type musvg_linejoin_type;
typedef enum musvg_linecap_type musvg_linecap_type;
typedef enum musvg_fillrule_type musvg_fillrule_type;
typedef enum musvg_unit musvg_unit;
typedef enum musvg_display musvg_display;
typedef enum musvg_align musvg_align;
typedef enum musvg_crop musvg_crop;
typedef enum musvg_gradient_unit musvg_gradient_unit;
typedef enum musvg_type musvg_type;
typedef enum musvg_attr musvg_attr;
#endif

typedef struct musvg_span musvg_span;
typedef struct musvg_id musvg_id;
typedef struct musvg_length musvg_length;
typedef struct musvg_color musvg_color;
typedef struct musvg_viewbox musvg_viewbox;
typedef struct musvg_aspectratio musvg_aspectratio;
typedef struct musvg_transform musvg_transform;
typedef struct musvg_dasharray musvg_dasharray;
typedef struct musvg_path_op musvg_path_op;
typedef struct musvg_attribute musvg_attribute;
typedef struct musvg_typeinfo_attr musvg_typeinfo_attr;
typedef struct musvg_typeinfo_enum musvg_typeinfo_enum;
typedef struct musvg_node_svg musvg_node_svg;
typedef struct musvg_node_path musvg_node_path;
typedef struct musvg_node_rect musvg_node_rect;
typedef struct musvg_node_circle musvg_node_circle;
typedef struct musvg_node_ellipse musvg_node_ellipse;
typedef struct musvg_node_line musvg_node_line;
typedef struct musvg_node_polyline musvg_node_polyline;
typedef struct musvg_node_polygon musvg_node_polygon;
typedef struct musvg_node_lgradient musvg_node_lgradient;
typedef struct musvg_node_rgradient musvg_node_rgradient;
typedef struct musvg_node musvg_node;
typedef struct musvg_gradient_stop musvg_gradient_stop;
typedef struct musvg_linear_gradient musvg_linear_gradient;
typedef struct musvg_radial_gradient musvg_radial_gradient;
typedef struct musvg_brush musvg_brush;
typedef struct musvg_named_color musvg_named_color;
typedef struct musvg_parser musvg_parser;

// SVG enums

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
    musvg_element_lgradient,
    musvg_element_rgradient,
    musvg_element_stop,
    musvg_element_LIMIT = musvg_element_stop
};
enum musvg_attr {
    musvg_attr_none,
    musvg_attr_display,
    musvg_attr_fill,
    musvg_attr_fill_opacity,
    musvg_attr_fill_rule,
    musvg_attr_font_size,
    musvg_attr_id,
    musvg_attr_offset,
    musvg_attr_stop_color,
    musvg_attr_stop_opacity,
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
    musvg_attr_svg_width,
    musvg_attr_svg_height,
    musvg_attr_svg_viewbox,
    musvg_attr_svg_aspectratio,
    musvg_attr_path_d,
    musvg_attr_poly_points,
    musvg_attr_rect_x,
    musvg_attr_rect_y,
    musvg_attr_rect_width,
    musvg_attr_rect_height,
    musvg_attr_rect_rx,
    musvg_attr_rect_ry,
    musvg_attr_circle_cx,
    musvg_attr_circle_cy,
    musvg_attr_circle_r,
    musvg_attr_ellipse_cx,
    musvg_attr_ellipse_cy,
    musvg_attr_ellipse_rx,
    musvg_attr_ellipse_ry,
    musvg_attr_line_x1,
    musvg_attr_line_y1,
    musvg_attr_line_x2,
    musvg_attr_line_y2,
    musvg_attr_lgradient_x1,
    musvg_attr_lgradient_y1,
    musvg_attr_lgradient_x2,
    musvg_attr_lgradient_y2,
    musvg_attr_rgradient_cx,
    musvg_attr_rgradient_cy,
    musvg_attr_rgradient_r,
    musvg_attr_rgradient_fx,
    musvg_attr_rgradient_fy,
    musvg_attr_gradient_units,
    musvg_attr_gradient_transform,
    musvg_attr_gradient_spread,
    musvg_attr_gradient_href,
    musvg_attr_LIMIT = musvg_attr_gradient_href
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
enum musvg_path_opcode {
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
enum musvg_brush_type {
    musvg_brush_default,
    musvg_brush_color,
    musvg_brush_linear_gradient,
    musvg_brush_radial_gradient
};
enum musvg_linecap_type {
    musvg_linecap_default,
    musvg_linecap_butt,
    musvg_linecap_round,
    musvg_linecap_square,

    musvg_linecap_LIMIT = musvg_linecap_square,
    musvg_linecap_DEFAULT = musvg_linecap_butt
};
enum musvg_linejoin_type {
    musvg_linejoin_default,
    musvg_linejoin_miter,
    musvg_linejoin_round,
    musvg_linejoin_bevel,

    musvg_linejoin_LIMIT = musvg_linejoin_bevel,
    musvg_linejoin_DEFAULT = musvg_linejoin_miter
};
enum musvg_fillrule_type {
    musvg_fillrule_default,
    musvg_fillrule_nonzero,
    musvg_fillrule_evenodd,

    musvg_fillrule_LIMIT = musvg_fillrule_evenodd,
    musvg_fillrule_DEFAULT = musvg_fillrule_nonzero
};
enum musvg_unit {
    musvg_unit_default,
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

    musvg_unit_LIMIT = musvg_unit_ex,
    musvg_unit_DEFAULT = musvg_unit_user
};
enum musvg_display {
    musvg_display_default,
    musvg_display_inline,
    musvg_display_none,

    musvg_display_LIMIT = musvg_display_none,
    musvg_display_DEFAULT = musvg_display_inline
};
enum musvg_align {
    musvg_align_default,
    musvg_align_none,
    musvg_align_min,
    musvg_align_mid,
    musvg_align_max,
    musvg_align_DEFAULT = musvg_align_mid
};
enum musvg_crop {
    musvg_crop_default,
    musvg_crop_none,
    musvg_crop_meet,
    musvg_crop_slice,
    musvg_crop_DEFAULT = musvg_crop_meet
};
enum musvg_gradient_spread_type {
    musvg_gradient_spread_default,
    musvg_gradient_spread_pad,
    musvg_gradient_spread_reflect,
    musvg_gradient_spread_repeat,
    musvg_gradient_spread_LIMIT = musvg_gradient_spread_repeat,
    musvg_gradient_spread_DEFAULT = musvg_gradient_spread_pad
};
enum musvg_gradient_unit {
    musvg_gradient_unit_default,
    musvg_gradient_unit_user,
    musvg_gradient_unit_obb,
    musvg_gradient_unit_LIMIT = musvg_gradient_unit_obb,
    musvg_gradient_unit_DEFAULT = musvg_gradient_unit_user
};
enum musvg_transform_type {
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
    char name[64];
};

struct musvg_length
{
    float value;
    musvg_small units;
};

struct musvg_color
{
    uint color;
    musvg_small present;
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

// SVG path operation

struct musvg_path_op
{
    musvg_small code;
    uint point_offset;
    uint point_count;
};

// SVG common attributes

struct musvg_attribute
{
    ullong bitmap;
    musvg_id id;
    musvg_transform xform;
    musvg_color fill_color;
    musvg_color stroke_color;
    float fill_opacity;
    float stroke_opacity;
    float stroke_miterlimit;
    musvg_length stroke_width;
    musvg_length stroke_dashoffset;
    musvg_dasharray stroke_dasharray;
    musvg_small stroke_linejoin;
    musvg_small stroke_linecap;
    musvg_small fill_rule;
    musvg_small display;
    musvg_length font_size;
    musvg_color stop_color;
    float stop_opacity;
    musvg_length stop_offset;
};

// SVG accessor value

enum musvg_type
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

struct musvg_typeinfo_attr
{
    musvg_type type;
    size_t offset;
};

struct musvg_typeinfo_enum
{
    const char ** names;
    size_t limit;
    size_t defalt;
};

// SVG node

struct musvg_node_svg {
    musvg_viewbox viewbox;
    musvg_aspectratio aspectratio;
    musvg_length width;
    musvg_length height;
};
struct musvg_node_path {
    uint op_offset, op_count;
};
struct musvg_node_rect {
    musvg_length x, y, width, height, rx, ry;
};
struct musvg_node_circle {
    musvg_length cx, cy, r;
};
struct musvg_node_ellipse {
    musvg_length cx, cy, rx, ry;
};
struct musvg_node_line {
    musvg_length x1, y1, x2, y2;
};
struct musvg_node_polyline {
    uint point_offset, point_count;
};
struct musvg_node_polygon {
    uint point_offset, point_count;
};
struct musvg_node_lgradient {
    uint gradient_id;
    musvg_id ref;
    musvg_transform xform;
    musvg_small spread, units;
    musvg_length x1, y1, x2, y2;
};
struct musvg_node_rgradient {
    uint gradient_id;
    musvg_id ref;
    musvg_transform xform;
    musvg_small spread, units;
    musvg_length cx, cy, r, fx, fy;
};

enum { musvg_node_sentinel = -1 };

struct musvg_node
{
    uint type;
    int next;
    int parent;
    musvg_attribute attr;
    union {
        musvg_node_svg svg;
        musvg_node_path path;
        musvg_node_rect rect;
        musvg_node_circle circle;
        musvg_node_ellipse ellipse;
        musvg_node_line line;
        musvg_node_polyline polyline;
        musvg_node_polygon polygon;
        musvg_node_lgradient lgradient;
        musvg_node_rgradient rgradient;
    };
    musvg_parser *p;
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
    musvg_brush_type type;
    uint flat_color;
    uint point_offset, point_count;
    uint stop_offset, stop_count;
};

// SVG API

musvg_parser* musvg_parser_create();
void musvg_emit_text(musvg_parser* musvg);
void musvg_emit_xml(musvg_parser* musvg);
void musvg_emit_binary_vf(musvg_parser* musvg);
void musvg_emit_binary_ieee(musvg_parser* musvg);
void musvg_parser_destroy(musvg_parser* musvg);
musvg_span musvg_read_file(const char* filename);
musvg_parser* musvg_parse_xml_data(char* data, size_t length);
musvg_parser* musvg_parse_binary_vf_data(char* data, size_t length);
musvg_parser* musvg_parse_binary_ieee_data(char* data, size_t length);
musvg_parser* musvg_parse_xml_file(const char* filename);
musvg_parser* musvg_parse_binary_vf_file(const char* filename);
musvg_parser* musvg_parse_binary_ieee_file(const char* filename);

#ifdef __cplusplus
}
#endif
