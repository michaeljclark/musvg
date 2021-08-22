cmake_minimum_required (VERSION 3.12)

project(musvg)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(MUSVG_ENABLE_ASAN "Enable ASAN" OFF)
option(MUSVG_ENABLE_MSAN "Enable MSAN" OFF)
option(MUSVG_ENABLE_UBSAN "Enable UBSAN" OFF)

macro(add_compiler_flag)
   set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ARGN}")
   set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ARGN}")
endmacro(add_compiler_flag)

macro(add_linker_flag)
  set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} ${ARGN}")
endmacro(add_linker_flag)

include(CheckCXXCompilerFlag)

# We need haswell for TZCNT/LZCNT
check_cxx_compiler_flag("-march=x86-64-v3" has_march_x86_64_v3 "int main() { return 0; }")
check_cxx_compiler_flag("-march=haswell" has_march_haswell "int main() { return 0; }")
if (has_march_x86_64_v3)
	add_compiler_flag(-march=x86-64-v3)
elseif (has_march_haswell)
	add_compiler_flag(-march=haswell)
endif()

if (MUSVG_ENABLE_ASAN)
  add_compiler_flag(-fsanitize=address)
  add_linker_flag(-fsanitize=address)
endif()

if(MUSVG_ENABLE_MSAN)
  add_compiler_flag(-fsanitize=memory)
  add_linker_flag(-fsanitize=memory)
endif()

if(MUSVG_ENABLE_UBSAN)
  add_compiler_flag(-fsanitize=undefined)
  add_linker_flag(-fsanitize=undefined)
endif()

include(CheckLibraryExists)
check_library_exists(m sqrtf "" HAVE_LIB_M)
list (APPEND EXTRA_LIBS $<$<EQUAL:${HAVE_LIB_M},1>:m>)

# check for the C thread support library
find_package(Threads REQUIRED)
check_include_file("threads.h" have_threads_h)
set(CMAKE_REQUIRED_LIBRARIES ${CMAKE_THREAD_LIBS_INIT})
check_c_source_compiles("#include <threads.h>
int main() { thrd_t t; thrd_create(&t, &main, NULL); }" has_thread_support)
check_c_source_compiles("#include <time.h>
int main() { struct timespec ts; timespec_get(&ts, 0); }" has_timespec_get)

if(NOT has_thread_support)
include_directories(src/compat)
endif()
if(CMAKE_USE_PTHREADS_INIT)
add_definitions(-DHAVE_PTHREAD)
if(has_timespec_get)
add_definitions(-DHAVE_TIMESPEC_GET)
endif()
endif()

add_subdirectory(third_party/mimalloc)
include_directories(third_party/mimalloc/include)
list (APPEND EXTRA_LIBS mimalloc)

include_directories(src src/blake3 src/sha2)

add_library(musvg STATIC
  src/musvg.c
  src/mubuf.cc
  src/sha2/sha256.c
  src/sha2/sha512.c
  src/blake3/blake3.c
  src/blake3/blake3_dispatch.c
  src/blake3/blake3_portable.c
  src/blake3/blake3_sse2.c
  src/blake3/blake3_sse41.c
  src/blake3/blake3_avx2.c
  src/blake3/blake3_avx512.c)

set_source_files_properties(src/blake3/blake3_sse2.c PROPERTIES COMPILE_FLAGS -msse2)
set_source_files_properties(src/blake3/blake3_sse41.c PROPERTIES COMPILE_FLAGS -msse4.1)
set_source_files_properties(src/blake3/blake3_avx2.c PROPERTIES COMPILE_FLAGS -mavx2)
set_source_files_properties(src/blake3/blake3_avx512.c PROPERTIES COMPILE_FLAGS "-mavx512f -mavx512vl")

add_executable(bench_blake3 test/bench_blake3.c)
target_link_libraries(bench_blake3 musvg ${EXTRA_LIBS})

add_executable(musvgtool app/musvgtool.c)
target_link_libraries(musvgtool musvg ${EXTRA_LIBS})

add_executable(test_muvec test/test_muvec.c)
target_link_libraries(test_muvec musvg ${EXTRA_LIBS})

add_executable(test_mumule test/test_mumule.c)
target_link_libraries(test_mumule musvg ${EXTRA_LIBS})

add_executable(test_mubuf test/test_mubuf.c)
target_link_libraries(test_mubuf musvg ${EXTRA_LIBS})

add_executable(bench_svg test/bench_svg.cc)
target_link_libraries(bench_svg musvg ${EXTRA_LIBS})
