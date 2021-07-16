cmake_minimum_required (VERSION 2.8)

project(musvg)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(CheckCXXCompilerFlag)

# We need haswell for TZCNT/LZCNT
check_cxx_compiler_flag("-march=x86-64-v3" has_march_x86_64_v3 "int main() { return 0; }")
check_cxx_compiler_flag("-march=haswell" has_march_haswell "int main() { return 0; }")
if (has_march_x86_64_v3)
	list(APPEND CMAKE_C_FLAGS -march=x86-64-v3)
	list(APPEND CMAKE_CXX_FLAGS -march=x86-64-v3)
elseif (has_march_haswell)
	list(APPEND CMAKE_C_FLAGS -march=haswell)
	list(APPEND CMAKE_CXX_FLAGS -march=haswell)
endif()

include(CheckLibraryExists)
check_library_exists(m sqrtf "" HAVE_LIB_M)
list (APPEND EXTRA_LIBS $<$<EQUAL:${HAVE_LIB_M},1>:m>)

add_library(musvg STATIC src/musvg.c src/vf128.cc)

include_directories(src)

add_executable(musvgreader app/musvgreader.c)
target_link_libraries(musvgreader musvg ${EXTRA_LIBS})

add_executable(test_vfbuf test/test_vfbuf.c)
target_link_libraries(test_vfbuf musvg ${EXTRA_LIBS})

add_executable(bench_svg test/bench_svg.cc)
target_link_libraries(bench_svg musvg ${EXTRA_LIBS})
