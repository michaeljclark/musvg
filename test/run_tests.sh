#!/bin/sh

./build/test_vfbuf

musvgtool=./build/musvgtool
in=test/input
out=test/output
test -d ${out} || mkdir ${out}

for name in tiger path ellipse circle line rect polygon polyline \
	    gradient-href gradient-linear gradient-radial;
do
  ${musvgtool} -i xml                    -o text                       \
               -if ${in}/${name}.svg     -of ${out}/${name}.text

  ${musvgtool} -i xml                    -o xml                        \
               -if ${in}/${name}.svg     -of ${out}/${name}.svg
  ${musvgtool} -i xml                    -o xml                        \
               -if ${out}/${name}.svg    -of ${out}/${name}.svg.svg

  diff ${out}/${name}.svg ${out}/${name}.svg.svg > /dev/null
  r1=$?

  ${musvgtool} -i xml                    -o svgv                       \
               -if ${in}/${name}.svg     -of ${out}/${name}.svgv
  ${musvgtool} -i svgv                   -o text                       \
               -if ${out}/${name}.svgv   -of ${out}/${name}.svgv.text
  ${musvgtool} -i svgv                   -o xml                        \
               -if ${out}/${name}.svgv   -of ${out}/${name}.svgv.svg

  diff ${out}/${name}.svg ${out}/${name}.svgv.svg > /dev/null
  r2=$?

  ${musvgtool} -i xml                    -o svgb                       \
               -if ${in}/${name}.svg     -of ${out}/${name}.svgb
  ${musvgtool} -i svgb                   -o text                       \
               -if ${out}/${name}.svgb   -of ${out}/${name}.svgb.text
  ${musvgtool} -i svgb                   -o xml                        \
               -if ${out}/${name}.svgb   -of ${out}/${name}.svgb.svg

  diff ${out}/${name}.svg ${out}/${name}.svgb.svg > /dev/null
  r3=$?

  if [ "$r1" -eq "0" -a "$r2" -eq "0" -a "$r3" -eq "0" ]; then
    echo "round-trip ${name}.svg: PASS"
  else
    echo "round-trip ${name}.svg: FAIL"
  fi
done
