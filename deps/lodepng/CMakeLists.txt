# CMake build script (C) 2019 - 2022, Robin Klimonow. All rights reserved.
# LodePNG version 20210627 Copyright (c) 2005-2021 Lode Vandevenne

file(GLOB src "*.cpp" "*.h")
add_library(lodepng ${src})
set_property(TARGET lodepng PROPERTY FOLDER "deps")
