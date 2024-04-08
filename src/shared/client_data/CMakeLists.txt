
if (MSVC)
	# ignore annoying protobuf-map-related warning
	add_definitions("/wd4800")
endif()

function(PROTOBUF_GENERATE_CPP SRCS HDRS)
  if(NOT ARGN)
    message(SEND_ERROR "Error: PROTOBUF_GENERATE_CPP() called without any proto files")
    return()
  endif()

  set(${SRCS})
  set(${HDRS})

  foreach(FIL ${ARGN})
    get_filename_component(ABS_FIL ${FIL} ABSOLUTE)
    get_filename_component(FIL_WE ${FIL} NAME_WE)

    list(APPEND ${SRCS} "${CMAKE_CURRENT_BINARY_DIR}/${FIL_WE}.pb.cc")
    list(APPEND ${HDRS} "${CMAKE_CURRENT_BINARY_DIR}/${FIL_WE}.pb.h")

    add_custom_command(
      OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${FIL_WE}.pb.cc"
             "${CMAKE_CURRENT_BINARY_DIR}/${FIL_WE}.pb.h"
      COMMAND $<TARGET_FILE:protoc> ARGS --cpp_out ${CMAKE_CURRENT_BINARY_DIR} -I ${CMAKE_CURRENT_SOURCE_DIR} ${ABS_FIL}
      DEPENDS ${ABS_FIL}
      COMMENT "Running C++ protocol buffer compiler on ${FIL}"
      VERBATIM
    )
  endforeach()

  set(${SRCS} ${${SRCS}} PARENT_SCOPE)
  set(${HDRS} ${${HDRS}} PARENT_SCOPE)
endfunction()

file(GLOB srcFiles "*.cpp" "*.h")
file(GLOB protoFiles "*.proto")
PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS ${protoFiles})

source_group(src FILES ${srcFiles})
source_group(src\\auto_code FILES ${PROTO_SRCS} ${PROTO_HDRS})
source_group(defs FILES ${protoFiles})

add_library(client_data ${PROTO_SRCS} ${PROTO_HDRS} ${srcFiles} ${protoFiles})
target_include_directories(client_data PRIVATE ${protobuf_INCLUDE_DIRS})
target_link_libraries(client_data assets)
target_link_libraries(client_data libprotobuf ${protobuf_LIBRARIES})

# Solution folder
set_property(TARGET client_data PROPERTY FOLDER "shared")
