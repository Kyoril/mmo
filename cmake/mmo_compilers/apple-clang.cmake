
# This file, when included, does some config options for Clang.

# Enable C++20 standard
set(CMAKE_XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD "c++20")
set(CMAKE_XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++20 -stdlib=libc++")
