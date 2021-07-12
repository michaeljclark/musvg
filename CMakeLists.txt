cmake_minimum_required (VERSION 2.8)

project(musvg)

include(CheckLibraryExists)
check_library_exists(m sqrtf "" HAVE_LIB_M)
list (APPEND EXTRA_LIBS $<$<EQUAL:${HAVE_LIB_M},1>:m>)

add_library(musvg STATIC src/musvg.c src/vf128.cc)

include_directories(src)

add_executable(musvgreader app/musvgreader.c)
target_link_libraries(musvgreader musvg ${EXTRA_LIBS})

add_executable(test_vfbuf test/test_vfbuf.c)
target_link_libraries(test_vfbuf musvg ${EXTRA_LIBS})

add_executable(bench_io test/bench_io.cc)
target_link_libraries(bench_io musvg ${EXTRA_LIBS})
