
# Create a new library
add_lib(base)

# Solution folder
if(MSVC)
	set_property(TARGET base PROPERTY FOLDER "shared")
endif(MSVC)
