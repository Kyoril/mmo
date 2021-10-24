
# Add shared graphics library
add_lib(game)
target_link_libraries(game assets)

# Put the library into the shared solution folder
set_property(TARGET game PROPERTY FOLDER "shared")
