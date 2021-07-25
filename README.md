# musvg

_musvg_ is an experiment to create a binary rendergraph protocol from the
SVG imaging model.

_musvg_ is based on _nanosvg_.

## differences

_nanosvg_ has a simple single-pass SVG XML parse and linearization step
using a SAX-like event stream. the _nanosvg_ approach couples the parse and
linearization steps meaning an alternate projection of SVG into binary
instead of XML would require copy and paste of the linearization code.

a refactor is in progress to split the code into a distinct parse to graph
step, adding linearization as a separate lowering step from an intermediate
graph. attributes are given value types with pure functions to convert to or
from external text or binary representation to an internal representation.

an SVG emitter is being added as well as parsers and emitters for a succinct
binary representation. a linearization pass can then be shared with front-ends
for other formats such as [IconVG](https://github.com/google/iconvg/).
presently there is only a parser split out from nanosvg with the addition
of a graph representation that preserves the structure of the SVG document.

## binary encoding example

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
