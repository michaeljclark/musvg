#!/bin/sh

test -d test/output || mkdir test/output

./build/test_vfbuf

./build/musvgreader --input-format xml    --output-format binary --input-file test/input/tiger.svg  > test/output/tiger-1.bin
./build/musvgreader --input-format binary --output-format text --input-file test/output/tiger-1.bin > test/output/tiger-1-bin.text
./build/musvgreader --input-format binary --output-format xml  --input-file test/output/tiger-1.bin > test/output/tiger-1-bin.svg

./build/musvgreader --input-format xml    --output-format xml    --input-file test/input/tiger.svg  > test/output/tiger-1.svg
./build/musvgreader --input-format xml    --output-format xml  --input-file test/output/tiger-1.svg > test/output/tiger-1-svg.svg

./build/musvgreader --input-format xml    --output-format binary --input-file test/input/path.svg  > test/output/path-1.bin
./build/musvgreader --input-format binary --output-format text --input-file test/output/path-1.bin > test/output/path-1-bin.text
./build/musvgreader --input-format binary --output-format xml  --input-file test/output/path-1.bin > test/output/path-1-bin.svg

./build/musvgreader --input-format xml    --output-format xml    --input-file test/input/path.svg  > test/output/path-1.svg
./build/musvgreader --input-format xml    --output-format xml  --input-file test/output/path-1.svg > test/output/path-1-svg.svg
