# Copyright (C) 2019, Kyoril. All rights reserved.

add_lib(log)
target_link_libraries(log base)

# Solution folder
set_property(TARGET log PROPERTY FOLDER "shared")
