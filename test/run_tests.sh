#!/bin/sh

test -d test/output || mkdir test/output

./build/test_vfbuf

for name in tiger path polyline gradient-href gradient-linear gradient-radial; do
	./build/musvgreader --input-format xml          --output-format text        \
		--input-file test/input/${name}.svg         --output-file test/output/${name}-1.text

	./build/musvgreader --input-format xml          --output-format xml         \
		--input-file test/input/${name}.svg         --output-file test/output/${name}-1.svg
	./build/musvgreader --input-format xml          --output-format xml         \
		--input-file test/output/${name}-1.svg      --output-file test/output/${name}-1-svg.svg

	./build/musvgreader --input-format xml          --output-format binary-vf   \
		--input-file test/input/${name}.svg         --output-file test/output/${name}-1.vfbin
	./build/musvgreader --input-format binary-vf    --output-format text        \
		--input-file test/output/${name}-1.vfbin    --output-file test/output/${name}-1-vfbin.text
	./build/musvgreader --input-format binary-vf    --output-format xml         \
		--input-file test/output/${name}-1.vfbin    --output-file test/output/${name}-1-vfbin.svg

	./build/musvgreader --input-format xml          --output-format binary-ieee \
		--input-file test/input/${name}.svg         --output-file test/output/${name}-1.ieeebin
	./build/musvgreader --input-format binary-ieee  --output-format text        \
		--input-file test/output/${name}-1.ieeebin  --output-file test/output/${name}-1-ieeebin.text
	./build/musvgreader --input-format binary-ieee  --output-format xml         \
		--input-file test/output/${name}-1.ieeebin  --output-file test/output/${name}-1-ieeebin.svg
done
