#!/bin/sh

./test/run_tests.sh

echo
CPUPROFILE=test/output/prof-xml.out    LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libprofiler.so ./build/bench_svg 0 -1 10000 0
google-pprof --text build/bench_svg test/output/prof-xml.out

echo
CPUPROFILE=test/output/prof-binary-vf.out LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libprofiler.so ./build/bench_svg 1 -1 10000 0
google-pprof --text build/bench_svg test/output/prof-binary-vf.out

echo
CPUPROFILE=test/output/prof-binary-ieee.out LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libprofiler.so ./build/bench_svg 2 -1 10000 0
google-pprof --text build/bench_svg test/output/prof-binary-ieee.out
