
INCLUDE_DIRECTORIES ( /System/Library/Frameworks )
FIND_LIBRARY(COCOA_LIBRARY Cocoa)
FIND_LIBRARY(METAL_LIBRARY Metal)
MARK_AS_ADVANCED (COCOA_LIBRARY)
SET(EXTRA_LIBS ${COCOA_LIBRARY} ${METAL_LIBRARY})


# Add shared graphics library
add_lib(graphics_metal)
target_link_libraries(graphics_metal graphics ${EXTRA_LIBS})

# Put the library into the shared solution folder
set_property(TARGET graphics_metal PROPERTY FOLDER "shared")
