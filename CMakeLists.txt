cmake_minimum_required (VERSION 2.8)

project(musvg)

include(CheckLibraryExists)
check_library_exists(m sqrtf "" HAVE_LIB_M)
list (APPEND EXTRA_LIBS $<$<EQUAL:${HAVE_LIB_M},1>:m>)

add_library(musvg STATIC src/musvg.c)

include_directories(src)

add_executable(musvgreader app/musvgreader.c)
target_link_libraries(musvgreader musvg ${EXTRA_LIBS})
