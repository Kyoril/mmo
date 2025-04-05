file(GLOB codeFiles
    "*.h"
    "*.cpp"
)

add_executable(mmo_error WIN32 mmo_error.rc ${codeFiles})

target_link_libraries(mmo_error base log)
target_link_libraries(mmo_error ${OPENSSL_LIBRARIES})
if(WIN32)
    target_link_libraries(mmo_error comctl32 version)
endif()
set_target_properties(mmo_error PROPERTIES OUTPUT_NAME mmo_error)

set_property(TARGET mmo_error PROPERTY FOLDER "client")

if (WIN32)
    # Client is dpi aware
    set_property(TARGET mmo_error PROPERTY VS_DPI_AWARE "PerMonitor")
endif()
