
# Create a new library
add_lib(base)
set_property(TARGET base PROPERTY FOLDER "shared")

# TaskSystem needs the platform thread library on non-MSVC toolchains
find_package(Threads REQUIRED)
target_link_libraries(base Threads::Threads)
