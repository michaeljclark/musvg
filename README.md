# musvg

_musvg_ is a binary rendergraph protocol for the SVG imaging model.

_musvg_ implements a typed property graph with succinct storage for typed
elements and attributes, an SVG compatible vector graphics model and a
bidirectional projection from SVG XML to a succinct binary encoding.

_musvg_ is based on _nanosvg_. _nanosvg_ has a simple single-pass SVG XML
parse and linearization step using a SAX-like event stream. _nanosvg_ couples
the parse and linearization steps meaning an alternate projection of SVG
into binary instead of XML would require copying the linearization code.
_musvg_ decouples parse and emit to allow supporting multiple encodings.

# musvg progress

- [X] succinct typed property graph.
- [X] parser and emitter for SVG XML .
- [X] parser and emitter for a succinct binary encoding.
- [ ] parser and emitter for alternative text representation.
- [ ] succinct binary encoding of property graph deltas.
- [ ] OpenVG front-end API for programmatic construction.
- [ ] linearization passes to convert shapes to paths.
- [ ] front-ends for other formats such as [IconVG](https://github.com/google/iconvg/).
- [ ] back-ends for cairo, skia, core-graphics, blend2d, nanosvg, ...

## musvg binary encoding

_musvg_ implemented a typed property graph with succinct binary encoding.

elements are encoded with an element type symbol followed by an
attribute list, any child elements if present, then an end-element symbol.
attributes are encoded with an attribute type symbol followed by an
attribute type specific data structure. attribute lists are a sequence of
attributes followed by an end-attribute-list symbol.

```
element-tree     ::= element-spec* end-element-symbol
element-spec     ::= element-type-symbol attribute-list element-tree*
attribute-list   ::= attribute-spec* end-attribute-list-symbol
attribute-spec   ::= attribute-type-symbol attribute-struct-data
```

### element enum
```
enum element_type : i8 {
  element_none,
  element_svg,
  element_g,
  element_defs,
  element_path,
  element_rect,
  element_circle,
  element_ellipse,
  element_line,
  element_polyline,
  element_polygon,
  element_linear_gradient,
  element_radial_gradient,
  element_stop
};
```

### attribute enum
```
enum attr_type : i8 {
  attr_none,
  attr_display,
  attr_fill,
  attr_fill_opacity,
  attr_fill_rule,
  attr_font_size,
  attr_id,
  attr_stroke,
  attr_stroke_width,
  attr_stroke_dasharray,
  attr_stroke_dashoffset,
  attr_stroke_opacity,
  attr_stroke_linecap,
  attr_stroke_linejoin,
  attr_stroke_miterlimit,
  attr_style,
  attr_transform,
  attr_d,
  attr_points,
  attr_width,
  attr_height,
  attr_x,
  attr_y,
  attr_r,
  attr_rx,
  attr_ry,
  attr_cx,
  attr_cy,
  attr_x1,
  attr_y1,
  attr_x2,
  attr_y2,
  attr_fx,
  attr_fy,
  attr_offset,
  attr_stop_color,
  attr_stop_opacity,
  attr_gradient_units,
  attr_gradient_transform,
  attr_spread_method,
  attr_view_box,
  attr_preserve_aspect_ratio,
  attr_xmlns,
  attr_xmlns_xlink,
  attr_xlink_href
};
```

### attribute types

#### linecap
```
enum linecap : i8 {
  linecap_butt,
  linecap_round,
  linecap_square
};
```

#### linejoin
```
enum linejoin : i8 {
  linejoin_miter,
  linejoin_round,
  linejoin_bevel
};
```

#### fillrule
```
enum fillrule : i8 {
  fillrule_nonzero,
  fillrule_evenodd
};
```

#### display
```
enum display : i8 {
  display_inline,
  display_none
};
```

#### spread_method
```
enum spread_method : i8 {
  spread_method_pad,
  spread_method_reflect,
  spread_method_repeat
};
```

#### gradient_unit
```
enum gradient_unit : i8 {
  gradient_unit_user,
  gradient_unit_obb
};
```

#### id
```
struct id {
  leb128 length;
  char data[length];
};
```

#### length
```
enum unit_type : i8 {
  unit_user,
  unit_px,
  unit_pt,
  unit_pc,
  unit_mm,
  unit_cm,
  unit_in,
  unit_percent,
  unit_em,
  unit_ex
};

struct length {
  float32 length;
  unit_type lunits;
};
```

#### color
```
enum color_type : i8 {
  color_type_none,
  color_type_rgba,
  color_type_url
};

struct color {
  color_type type;
  union {
    i32 color;
    id url;
  };
};
```

#### transform
```
enum transform_type : i8 {
  transform_matrix,
  transform_translate,
  transform_scale,
  transform_rotate,
  transform_skew_x,
  transform_skew_y
};

struct transform {
  transform_type type;
  union {
    float32 matrix[6];
    struct {
      i8 nargs;
      float32 args[nargs];
    };
  };
};
```

#### viewbox
```
struct viewbox {
  float32 x;
  float32 y;
  float32 width;
  float32 height;
};
```

#### aspectratio
```
enum align_type : i8 {
  align_mid,
  align_min,
  align_max,
  align_none
};

struct aspectratio {
  float32 alignX;
  float32 alignY;
  align_type type;
};
```

#### path
```
enum path_opcode : i8 {
  path_none,
  path_closepath,
  path_moveto_abs,
  path_moveto_rel,
  path_lineto_abs,
  path_lineto_rel,
  path_curveto_cubic_abs,
  path_curveto_cubic_rel,
  path_quadratic_curve_to_abs,
  path_quadratic_curve_to_rel,
  path_eliptical_arc_abs,
  path_eliptical_arc_rel,
  path_line_to_horizontal_abs,
  path_line_to_horizontal_rel,
  path_line_to_vertical_abs,
  path_line_to_vertical_rel,
  path_curveto_cubic_smooth_abs,
  path_curveto_cubic_smooth_rel,
  path_curveto_quadratic_smooth_abs,
  path_curveto_quadratic_smooth_rel
};

struct path {
  leb128 ops_count;
  struct {
    path_opcode opcode;
    leb128 point_count;
    float32 points[point_count];
  } path_ops[ops_count];
};
```

#### points
```
struct points {
  leb128 points_count;
  float32 points[point_count];
};
```

### encoding example

succinct coding of SVG circle takes 21 bytes instead of 41 bytes as XML.
if we were able to encode the radius in a parent element then it would take 15 bytes.
if there were a polygon like primitive with point data with or without radius
it would take only 8 or 12 bytes.

```
<circle cx="105" cy="105" r="54.726524"/>
```

- (1 byte) circle-element-symbol
  - (1 byte) cx-length-attribute-symbol
  - (5 bytes) length encoding
    - (4 bytes) ieee-754 float
    - (1 byte)  length-unit-user
  - (1 byte) cy-length-attribute-symbol
  - (5 bytes) length encoding
    - (4 bytes) ieee-754 float
    - (1 byte)  length-unit-user
  - (1 byte) r-length-attribute-symbol
  - (5 bytes) length encoding
    - (4 bytes) ieee-754 float
    - (1 byte)  length-unit-user
  - (1 byte) end-attribute-list-symbol
- (1 byte) close-element-symbol

## musvgtool

musvgtool is a utility that converts between SVG XML and SVG Binary.

#### musvgtool test/input/path.svg statistics

example shows conversion of a small file from XML to binary using the
`--stats` option to show memory usage for the succinct internal format.

```
; ./build/musvgtool --stats -i xml -o svgb -if test/input/path.svg -of test/output/path.svgb

name             size      count   capacity    used(B)   alloc(B)
--------------- ----- ---------- ---------- ---------- ----------
nodes              16          3         16         48        256
attr_offsets        8         10         16         80        128
attr_storage        1         80        128         80        128
path_ops            1         11         16         11         16
path_points         8         11         16         88        128
points              4         20         32         80        128
strings             1         21         32         21         32
--------------- ----- ---------- ---------- ---------- ----------
totals                                             387        784
```

#### musvgtool test/input/tiger.svg statistics

example shows conversion of a larger file from XML to binary using the
`--stats` option to show memory usage for the succinct internal format.

```
; ./build/musvgtool --stats -i xml -o svgb -if test/input/tiger.svg -of test/output/tiger.svgb

name             size      count   capacity    used(B)   alloc(B)
--------------- ----- ---------- ---------- ---------- ----------
nodes              16        482        512       7712       8192
attr_offsets        8        623       1024       4984       8192
attr_storage        1       5008       8192       5008       8192
path_ops            1       2510       4096       2510       4096
path_points         8       2510       4096      20080      32768
points              4      12174      16384      48696      65536
strings             1         57         64         57         64
--------------- ----- ---------- ---------- ---------- ----------
totals                                           88990     126976
```

## tests

- json.svg bug

## topological hashing

once the binary representation is in place, there is a plan to succinctly
encode changes to a graph in a log. a concept called (_"topological content
addressing"_) will be used to identify nodes using topological content hashes.

> Git's internal format uses pure content addresses for blobs and
> topological content addresses for commits.

the difference between topological content addressing and pure content
addressing is that topological content addresses includes in hash sums
the relations to connected nodes via absorbtion of relation hash sums.

topological hashing allows unique addressing for nodes that would otherwise
have equivalent content hashes when only looking at their distinct properties.
topological content addressing solves the problem of identifying nodes that
are otherwise equal without requiring programatic assignment of unique IDs
which as we know is troublesome.

IDs are calculated not assigned meaning there is no redundant identity state
beyond the object properties and relations, removing _random_ entropy used
by alternative ID assignment methods to solve this identity and order problem.
this adds to succinctness. if a nonce is used in the hash sum, it can be
common to the collection.

> duplicate but unique entries can be addressed deterministically using
> sum of properties with topological order encoded via inclusion of the
> hash sums of relations, obviating any need to maintain ancillary state.

example list with three entries named 'a':

```
a
a
a
```

example list of three objects (`struct node { string name; }`) named 'a':


```
[name:a]
[name:a]
[name:a]
```

example showing the SHA-224 pure content addresses for three nodes named 'a':

```
001b7fa72f29c3e4c7c7b6ff231ff22d28dfb97ef95a7107c887946a
001b7fa72f29c3e4c7c7b6ff231ff22d28dfb97ef95a7107c887946a
001b7fa72f29c3e4c7c7b6ff231ff22d28dfb97ef95a7107c887946a
```

example of the three nodees with their adjacent nodes (_"next:"_) addressed
using the SHA-224 sum of their text representation which includes the SHA-224
sum of their relations:

```
[name:a next:b1276e6f59ae6e07e1c8cf5b2e1b1e07f947ee6b6457bc5829396edc]
[name:a next:71bc6a1ace1a609b82e2ddfb1e968db0f8677cde47dff234dc14efe5]
[name:a next:nil]
```

example of SHA-224 topological content addresses for the three nodes:

```
47f2c77bb9ad98a6cbae4c62a6120a4eff864021ce7e4de90becbe69
b1276e6f59ae6e07e1c8cf5b2e1b1e07f947ee6b6457bc5829396edc
71bc6a1ace1a609b82e2ddfb1e968db0f8677cde47dff234dc14efe5
```

later, in a synchronization or compression protocol, a context model using
a priori state can form a patch delta to a specific instance of some duplicate
entry using the minimum colliding prefix of the node's topological hash.

example succinct minimum collision-free hash prefixes for the three nodes:

```
4
b
7
```

example list reprojected into a lisp-like list notation using ("|") to
link nodes and ("∅") for the empty set:

```
(a|(a|(a|∅)))
(a|(a|∅))
(a|∅)
```

note: topological hashing imposes a dependency-free traversal because a node
hash cannot be calculated until the sum of its dependent node is known.

## building

### Ninja

The project can be built using the ninja build tool on Windows, Linux
and macOS, by specifying the `Ninja` generator to cmake:

```
cmake -G Ninja -B build
cmake --build build -- --verbose
```

choose specifically a Clang Release build with debug symbols enabled:

```
CXX=clang++ CC=clang cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -- --verbose
```

### Address Sanitizer

building with address sanitizer on Windows, Linux or macOS using Clang or GCC:

```
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DMUSVG_ENABLE_MSAN=ON
```

### Memory Sanitizer

building with memory sanitizer on Linux using Clang:

```
CXX=clang++ CC=clang cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DMUSVG_ENABLE_MSAN=ON
```
