# HLSL to Metal Shading Language cross compiler (glslang + SPIRV-Cross). Only used on macOS, where
# it turns the engine's HLSL into Metal shaders at build time and when baking materials.
find_package(glslang CONFIG REQUIRED)

find_path(SPIRV_CROSS_INCLUDE_DIR spirv_cross/spirv_msl.hpp)
find_library(SPIRV_CROSS_CORE_LIB spirv-cross-core)
find_library(SPIRV_CROSS_GLSL_LIB spirv-cross-glsl)
find_library(SPIRV_CROSS_MSL_LIB spirv-cross-msl)
if (NOT SPIRV_CROSS_INCLUDE_DIR OR NOT SPIRV_CROSS_CORE_LIB OR NOT SPIRV_CROSS_GLSL_LIB OR NOT SPIRV_CROSS_MSL_LIB)
	message(FATAL_ERROR "SPIRV-Cross was not found. Install it with: brew install spirv-cross glslang")
endif()

add_lib(msl_cross)
# The HLSL front end of glslang is only declared with this set.
target_compile_definitions(msl_cross PRIVATE ENABLE_HLSL)
target_include_directories(msl_cross PRIVATE ${SPIRV_CROSS_INCLUDE_DIR})
target_link_libraries(msl_cross glslang::glslang glslang::SPIRV glslang::glslang-default-resource-limits
	${SPIRV_CROSS_MSL_LIB} ${SPIRV_CROSS_GLSL_LIB} ${SPIRV_CROSS_CORE_LIB})

set_property(TARGET msl_cross PROPERTY FOLDER "shared")
