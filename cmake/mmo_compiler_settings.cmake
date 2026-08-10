# Output goes into the source tree rather than the build tree, so two build directories
# configured from the same source would otherwise overwrite each other's binaries AND static
# libraries. That is not hypothetical: mixing sanitized and unsanitized objects produces
# LNK2038 "mismatch detected for annotate_string" and similar, which reads like a code error
# rather than the build collision it is. The sanitizer tree therefore gets its own suffix.
if (MMO_ENABLE_ASAN)
	set(MMO_OUTPUT_SUFFIX "-asan")
else()
	set(MMO_OUTPUT_SUFFIX "")
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib${MMO_OUTPUT_SUFFIX})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib${MMO_OUTPUT_SUFFIX})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin${MMO_OUTPUT_SUFFIX})

if (MSVC)
	include("mmo_compilers/msvc")
elseif(${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
	include("mmo_compilers/clang")
elseif(${CMAKE_CXX_COMPILER_ID} STREQUAL "AppleClang")
    include("mmo_compilers/apple-clang")
elseif(${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
	include("mmo_compilers/gcc")
else()
	message(FATAL_ERROR "Unsupported compiler! Currently, this project supports Microsoft Visual Studio 2017 or newer, GCC 8 or newer and Clang.")
endif()

