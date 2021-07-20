#!/bin/sh

test -d test/output || mkdir test/output

./build/test_vfbuf

for name in tiger path polyline gradient-href gradient-linear gradient-radial; do
	./build/musvgtool   --input-format xml          --output-format text        \
		--input-file test/input/${name}.svg         --output-file test/output/${name}-1.text

	./build/musvgtool   --input-format xml          --output-format xml         \
		--input-file test/input/${name}.svg         --output-file test/output/${name}-1.svg
	./build/musvgtool   --input-format xml          --output-format xml         \
		--input-file test/output/${name}-1.svg      --output-file test/output/${name}-1-svg.svg

	./build/musvgtool   --input-format xml          --output-format binary-vf   \
		--input-file test/input/${name}.svg         --output-file test/output/${name}-1.vfbin
	./build/musvgtool   --input-format binary-vf    --output-format text        \
		--input-file test/output/${name}-1.vfbin    --output-file test/output/${name}-1-vfbin.text
	./build/musvgtool   --input-format binary-vf    --output-format xml         \
		--input-file test/output/${name}-1.vfbin    --output-file test/output/${name}-1-vfbin.svg

	./build/musvgtool   --input-format xml          --output-format binary-ieee \
		--input-file test/input/${name}.svg         --output-file test/output/${name}-1.ieeebin
	./build/musvgtool   --input-format binary-ieee  --output-format text        \
		--input-file test/output/${name}-1.ieeebin  --output-file test/output/${name}-1-ieeebin.text
	./build/musvgtool   --input-format binary-ieee  --output-format xml         \
		--input-file test/output/${name}-1.ieeebin  --output-file test/output/${name}-1-ieeebin.svg
done
