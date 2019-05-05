# Copyright (C) 2019, Robin Klimonow. All rights reserved.

add_lib(log)
target_link_libraries(log base)

# Solution folder
set_property(TARGET log PROPERTY FOLDER "shared")
