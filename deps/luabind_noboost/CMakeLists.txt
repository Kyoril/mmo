# Build for LuaBind
# Ryan Pavlik <rpavlik@iastate.edu>
# http://academic.cleardefinition.com/
# Iowa State University HCI Graduate Program/VRAC

# Modifications by Robin Klimonow (Kyoril) for the mmo project

cmake_minimum_required(VERSION 3.10)
project(luabind)

include_directories(
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${CMAKE_CURRENT_SOURCE_DIR}/../lua"
)

add_subdirectory(src)

set_property(TARGET luabind PROPERTY FOLDER "deps")