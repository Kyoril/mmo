
# This file, when included, does some config options for Clang.

# Enable C++20 standard
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++20 -stdlib=libc++")
link_libraries("uuid")
