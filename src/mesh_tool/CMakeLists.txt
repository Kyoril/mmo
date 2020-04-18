# Add tool file
add_exe(mesh_tool)
target_link_libraries(mesh_tool mesh_v1_0 log base)

# Link FBX sdk files
include_directories(${FBXSDK_INCLUDE_DIR})
target_link_libraries(mesh_tool ${FBXSDK_LIBRARIES})

# Set solution folder
set_property(TARGET mesh_tool PROPERTY FOLDER "tools")
