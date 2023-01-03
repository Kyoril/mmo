
add_lib(web_services)
target_link_libraries(web_services http)
set_property(TARGET web_services PROPERTY FOLDER "shared")
