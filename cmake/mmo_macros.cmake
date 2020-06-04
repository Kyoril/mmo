function(protobuf_generate)
  set(_options APPEND_PATH DESCRIPTORS)
  set(_singleargs LANGUAGE OUT_VAR EXPORT_MACRO PROTOC_OUT_DIR)
  if(COMMAND target_sources)
    list(APPEND _singleargs TARGET)
  endif()
  set(_multiargs PROTOS IMPORT_DIRS GENERATE_EXTENSIONS)

  cmake_parse_arguments(protobuf_generate "${_options}" "${_singleargs}" "${_multiargs}" "${ARGN}")

  if(NOT protobuf_generate_PROTOS AND NOT protobuf_generate_TARGET)
    message(SEND_ERROR "Error: protobuf_generate called without any targets or source files")
    return()
  endif()

  if(NOT protobuf_generate_OUT_VAR AND NOT protobuf_generate_TARGET)
    message(SEND_ERROR "Error: protobuf_generate called without a target or output variable")
    return()
  endif()

  if(NOT protobuf_generate_LANGUAGE)
    set(protobuf_generate_LANGUAGE cpp)
  endif()
  string(TOLOWER ${protobuf_generate_LANGUAGE} protobuf_generate_LANGUAGE)

  if(NOT protobuf_generate_PROTOC_OUT_DIR)
    set(protobuf_generate_PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
  endif()

  if(protobuf_generate_EXPORT_MACRO AND protobuf_generate_LANGUAGE STREQUAL cpp)
    set(_dll_export_decl "dllexport_decl=${protobuf_generate_EXPORT_MACRO}:")
  endif()

  if(NOT protobuf_generate_GENERATE_EXTENSIONS)
    if(protobuf_generate_LANGUAGE STREQUAL cpp)
      set(protobuf_generate_GENERATE_EXTENSIONS .pb.h .pb.cc)
    elseif(protobuf_generate_LANGUAGE STREQUAL python)
      set(protobuf_generate_GENERATE_EXTENSIONS _pb2.py)
    else()
      message(SEND_ERROR "Error: protobuf_generate given unknown Language ${LANGUAGE}, please provide a value for GENERATE_EXTENSIONS")
      return()
    endif()
  endif()

  if(protobuf_generate_TARGET)
    get_target_property(_source_list ${protobuf_generate_TARGET} SOURCES)
    foreach(_file ${_source_list})
      if(_file MATCHES "proto$")
        list(APPEND protobuf_generate_PROTOS ${_file})
      endif()
    endforeach()
  endif()

  if(NOT protobuf_generate_PROTOS)
    message(SEND_ERROR "Error: protobuf_generate could not find any .proto files")
    return()
  endif()

  if(protobuf_generate_APPEND_PATH)
    # Create an include path for each file specified
    foreach(_file ${protobuf_generate_PROTOS})
      get_filename_component(_abs_file ${_file} ABSOLUTE)
      get_filename_component(_abs_path ${_abs_file} PATH)
      list(FIND _protobuf_include_path ${_abs_path} _contains_already)
      if(${_contains_already} EQUAL -1)
          list(APPEND _protobuf_include_path -I ${_abs_path})
      endif()
    endforeach()
  else()
    set(_protobuf_include_path -I ${CMAKE_CURRENT_SOURCE_DIR})
  endif()

  foreach(DIR ${protobuf_generate_IMPORT_DIRS})
    get_filename_component(ABS_PATH ${DIR} ABSOLUTE)
    list(FIND _protobuf_include_path ${ABS_PATH} _contains_already)
    if(${_contains_already} EQUAL -1)
        list(APPEND _protobuf_include_path -I ${ABS_PATH})
    endif()
  endforeach()

  set(_generated_srcs_all)
  foreach(_proto ${protobuf_generate_PROTOS})
    get_filename_component(_abs_file ${_proto} ABSOLUTE)
    get_filename_component(_abs_dir ${_abs_file} DIRECTORY)
    get_filename_component(_basename ${_proto} NAME_WE)
    file(RELATIVE_PATH _rel_dir ${CMAKE_CURRENT_SOURCE_DIR} ${_abs_dir})

    set(_possible_rel_dir)
    if (NOT protobuf_generate_APPEND_PATH)
        set(_possible_rel_dir ${_rel_dir}/)
    endif()

    set(_generated_srcs)
    foreach(_ext ${protobuf_generate_GENERATE_EXTENSIONS})
      list(APPEND _generated_srcs "${protobuf_generate_PROTOC_OUT_DIR}/${_possible_rel_dir}${_basename}${_ext}")
    endforeach()

    if(protobuf_generate_DESCRIPTORS AND protobuf_generate_LANGUAGE STREQUAL cpp)
      set(_descriptor_file "${CMAKE_CURRENT_BINARY_DIR}/${_basename}.desc")
      set(_dll_desc_out "--descriptor_set_out=${_descriptor_file}")
      list(APPEND _generated_srcs ${_descriptor_file})
    endif()
    list(APPEND _generated_srcs_all ${_generated_srcs})

    add_custom_command(
      OUTPUT ${_generated_srcs}
      COMMAND  protobuf::protoc
      ARGS --${protobuf_generate_LANGUAGE}_out ${_dll_export_decl}${protobuf_generate_PROTOC_OUT_DIR} ${_dll_desc_out} ${_protobuf_include_path} ${_abs_file}
      DEPENDS ${_abs_file} protobuf::protoc
      COMMENT "Running ${protobuf_generate_LANGUAGE} protocol buffer compiler on ${_proto}"
      VERBATIM )
  endforeach()

  set_source_files_properties(${_generated_srcs_all} PROPERTIES GENERATED TRUE)
  if(protobuf_generate_OUT_VAR)
    set(${protobuf_generate_OUT_VAR} ${_generated_srcs_all} PARENT_SCOPE)
  endif()
  if(protobuf_generate_TARGET)
    target_sources(${protobuf_generate_TARGET} PRIVATE ${_generated_srcs_all})
  endif()
endfunction()

function(PROTOBUF_GENERATE_CPP SRCS HDRS)
  cmake_parse_arguments(protobuf_generate_cpp "" "EXPORT_MACRO;DESCRIPTORS" "" ${ARGN})

  set(_proto_files "${protobuf_generate_cpp_UNPARSED_ARGUMENTS}")
  if(NOT _proto_files)
    message(SEND_ERROR "Error: PROTOBUF_GENERATE_CPP() called without any proto files")
    return()
  endif()

  if(PROTOBUF_GENERATE_CPP_APPEND_PATH)
    set(_append_arg APPEND_PATH)
  endif()

  if(protobuf_generate_cpp_DESCRIPTORS)
    set(_descriptors DESCRIPTORS)
  endif()

  if(DEFINED PROTOBUF_IMPORT_DIRS AND NOT DEFINED Protobuf_IMPORT_DIRS)
    set(Protobuf_IMPORT_DIRS "${PROTOBUF_IMPORT_DIRS}")
  endif()

  if(DEFINED Protobuf_IMPORT_DIRS)
    set(_import_arg IMPORT_DIRS ${Protobuf_IMPORT_DIRS})
  endif()

  set(_outvar)
  protobuf_generate(${_append_arg} ${_descriptors} LANGUAGE cpp EXPORT_MACRO ${protobuf_generate_cpp_EXPORT_MACRO} OUT_VAR _outvar ${_import_arg} PROTOS ${_proto_files})

  set(${SRCS})
  set(${HDRS})
  if(protobuf_generate_cpp_DESCRIPTORS)
    set(${protobuf_generate_cpp_DESCRIPTORS})
  endif()

  foreach(_file ${_outvar})
    if(_file MATCHES "cc$")
      list(APPEND ${SRCS} ${_file})
    elseif(_file MATCHES "desc$")
      list(APPEND ${protobuf_generate_cpp_DESCRIPTORS} ${_file})
    else()
      list(APPEND ${HDRS} ${_file})
    endif()
  endforeach()
  set(${SRCS} ${${SRCS}} PARENT_SCOPE)
  set(${HDRS} ${${HDRS}} PARENT_SCOPE)
  if(protobuf_generate_cpp_DESCRIPTORS)
    set(${protobuf_generate_cpp_DESCRIPTORS} "${${protobuf_generate_cpp_DESCRIPTORS}}" PARENT_SCOPE)
  endif()
endfunction()



function(enable_unity_build UB_SUFFIX SOURCE_VARIABLE_NAME)
	#(copied from http://cheind.wordpress.com/2009/12/10/reducing-compilation-time-unity-builds/ )
	#modified to support pch in unity build file and support splitting N files in one big unity cpp file
	set(files ${${SOURCE_VARIABLE_NAME}})
	list(LENGTH files fileCount)
	if(fileCount GREATER 2)
		set(unity_index 0)
		# Exclude all translation units from compilation
		set_source_files_properties(${files} PROPERTIES HEADER_FILE_ONLY true)
		math(EXPR file_end "${fileCount}-1")
		# Generate N unity build files
		foreach(loop_var RANGE 0 ${file_end} ${MMO_UNITY_FILES_PER_UNIT})
			message(STATUS "  Generating unity build file ub_${UB_SUFFIX}.${unity_index}.cpp")
			# Generate a unique filename for the unity build translation unit
			set(unit_build_file "${CMAKE_CURRENT_BINARY_DIR}/ub_${UB_SUFFIX}.${unity_index}.cpp")
			# Open the ub file
			file(WRITE ${unit_build_file} "// Unity Build generated by CMake (do not edit this file)\n#include \"pch.h\"\n")
			set(file_index 0)
			math(EXPR min_file "${unity_index}*${MMO_UNITY_FILES_PER_UNIT}")
			math(EXPR max_file "${min_file}+${MMO_UNITY_FILES_PER_UNIT}+1")
			# Add include statement for each translation unit
			foreach(source_file ${files})
				#adding pch.cpp is unnecessary
				if(NOT(${source_file} MATCHES ".*pch\\.cpp"))
					math(EXPR file_index "${file_index}+1")
					if (file_index GREATER min_file AND file_index LESS max_file)
						file(APPEND ${unit_build_file} "#include <${source_file}>\n")
					endif()
				else()
					set_source_files_properties(${source_file} PROPERTIES HEADER_FILE_ONLY false)
				endif()
			endforeach()
			# Complement list of translation units with the name of ub
			set(${SOURCE_VARIABLE_NAME} "${${SOURCE_VARIABLE_NAME}}" "${unit_build_file}")
			set(${SOURCE_VARIABLE_NAME} "${${SOURCE_VARIABLE_NAME}}" "${unit_build_file}" PARENT_SCOPE)
			# Increment unity build file counter
			math(EXPR unity_index "${unity_index}+1")
		endforeach()
	endif()
endfunction()

macro(add_lib name)
	file(GLOB sources "*.cpp" "*.c")
	file(GLOB headers "*.h" "*.hpp")
	#remove_pch_cpp(sources "${CMAKE_CURRENT_SOURCE_DIR}/pch.cpp")
	if(MMO_UNITY_BUILD)
		message(STATUS "Unity build enabled for ${name}")
		include_directories(${CMAKE_CURRENT_SOURCE_DIR})
		enable_unity_build(${name} sources)
	endif()
	add_library(${name} ${headers} ${sources})
	#add_precompiled_header(${name} "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")
	source_group(src FILES ${headers} ${sources})
	
	if(${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
		target_link_libraries(${name} stdc++fs)
		target_link_libraries(${name} ${OPENSSL_LIBRARIES})
	endif()
endmacro()

macro(add_proto_lib name)
	file(GLOB sources "*.cpp" "*.c")
	file(GLOB headers "*.h" "*.hpp")
	file(GLOB protoFiles "proto/*.proto")
	PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS ${protoFiles})
	
	include_directories(${CMAKE_CURRENT_BINARY_DIR})
	
	#remove_pch_cpp(sources "${CMAKE_CURRENT_SOURCE_DIR}/pch.cpp")
	if(MMO_UNITY_BUILD)
		message(STATUS "Unity build enabled for ${name}")
		include_directories(${CMAKE_CURRENT_SOURCE_DIR})
		enable_unity_build(${name} sources)
	endif()
	add_library(${name} ${PROTO_SRCS} ${PROTO_HDRS} ${protoFiles} ${headers} ${sources})
	#add_precompiled_header(${name} "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")
	source_group(src FILES ${headers} ${sources})
	source_group(src\\auto_code FILES ${PROTO_SRCS} ${PROTO_HDRS})
	source_group(defs FILES ${protoFiles})
	
	if(${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
		target_link_libraries(${name} stdc++fs)
		target_link_libraries(${name} ${OPENSSL_LIBRARIES})
	endif()
endmacro()

macro(add_exe name)
	file(GLOB sources "*.cpp")
	file(GLOB headers "*.h" "*.hpp")
	#remove_pch_cpp(sources "${CMAKE_CURRENT_SOURCE_DIR}/pch.cpp")
	if(MMO_UNITY_BUILD)
		message(STATUS "Unity build enabled for ${name}")
		include_directories(${CMAKE_CURRENT_SOURCE_DIR})
		enable_unity_build(${name} sources)
	endif()
	add_executable(${name} ${headers} ${sources})
	#add_precompiled_header(${name} "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")
	source_group(src FILES ${headers} ${sources})
	if(${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
		target_link_libraries(${name} stdc++fs)
		target_link_libraries(${name} ${OPENSSL_LIBRARIES})
	endif()
endmacro()

macro(add_gui_exe name)
	set (add_sources ${ARGN})

	file(GLOB sources "*.cpp")
	file(GLOB headers "*.h" "*.hpp")
	file(GLOB resources "*.rc")
	
	# Append additional sources if any
	list(LENGTH add_sources num_add_sources)
    if (${num_add_sources} GREATER 0)
		list(APPEND sources ${add_sources})
	endif()
	
	#remove_pch_cpp(sources "${CMAKE_CURRENT_SOURCE_DIR}/pch.cpp")
	if(MMO_UNITY_BUILD)
		message(STATUS "Unity build enabled for ${name}")
		include_directories(${CMAKE_CURRENT_SOURCE_DIR})
		enable_unity_build(${name} sources)
	endif()
	add_executable(${name} WIN32 MACOSX_BUNDLE ${resources} ${headers} ${sources})
	#add_precompiled_header(${name} "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")
	source_group(src FILES ${headers} ${sources})
	if(${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
		target_link_libraries(${name} stdc++fs)
	endif()
endmacro()

MACRO(ADD_PRECOMPILED_HEADER _targetName _input)
	GET_FILENAME_COMPONENT(_name ${_input} NAME)
	get_filename_component(_name_no_ext ${_name} NAME_WE)
	if(MSVC)
		set_target_properties(${_targetName} PROPERTIES COMPILE_FLAGS "/Yu${_name}")
		set_source_files_properties("${_name_no_ext}.cpp" PROPERTIES COMPILE_FLAGS "/Yc${_name}")
	else()
		IF(APPLE)
			set_target_properties(
        		    ${_targetName} 
        		    PROPERTIES
        		    XCODE_ATTRIBUTE_GCC_PREFIX_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/${_name_no_ext}.h"
        		    XCODE_ATTRIBUTE_GCC_PRECOMPILE_PREFIX_HEADER "YES"
        		)
		ELSE()
			if(UNIX)
				GET_FILENAME_COMPONENT(_path ${_input} PATH)
				SET(_outdir "${CMAKE_CURRENT_BINARY_DIR}")
				SET(_output "${_input}.gch")

				set_directory_properties(PROPERTY ADDITIONAL_MAKE_CLEAN_FILES _output)

				STRING(TOUPPER "CMAKE_CXX_FLAGS_${CMAKE_BUILD_TYPE}" _flags_var_name)
				SET(_compile_FLAGS ${${_flags_var_name}})

				GET_DIRECTORY_PROPERTY(_directory_flags INCLUDE_DIRECTORIES)
				list(REMOVE_ITEM _directory_flags /usr/include) #/usr/include is not needed in this list and can cause problems with gcc
				foreach(d ${_directory_flags})
                                        #-isystem to ignore warnings in foreign headers
					list(APPEND _compile_FLAGS "-isystem${d}")
				endforeach()

				GET_DIRECTORY_PROPERTY(_directory_flags COMPILE_DEFINITIONS)
				LIST(APPEND defines ${_directory_flags})

				string(TOUPPER "COMPILE_DEFINITIONS_${CMAKE_BUILD_TYPE}" defines_for_build_name)
				get_directory_property(defines_build ${defines_for_build_name})
				list(APPEND defines ${defines_build})

				foreach(item ${defines})
					list(APPEND _compile_FLAGS "-D${item}")
				endforeach()

				LIST(APPEND _compile_FLAGS ${CMAKE_CXX_FLAGS})

				SEPARATE_ARGUMENTS(_compile_FLAGS)
				ADD_CUSTOM_COMMAND(
					OUTPUT ${_output}
					COMMAND ${CMAKE_CXX_COMPILER}
					${_compile_FLAGS}
					-x c++-header
					-fPIC
					-o ${_output}
					${_input}
					DEPENDS ${_input} #${CMAKE_CURRENT_BINARY_DIR}/${_name}
				)
				ADD_CUSTOM_TARGET(${_targetName}_gch
					DEPENDS	${_output}
				)
				ADD_DEPENDENCIES(${_targetName} ${_targetName}_gch)
			endif()
		ENDIF()
	endif()
ENDMACRO()

MACRO(remove_pch_cpp sources pch_cpp)
	if(UNIX)
		list(REMOVE_ITEM ${sources} ${pch_cpp})
	endif()
ENDMACRO()

