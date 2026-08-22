add_lib(http)
# http_incoming_request.cpp calls WLOG, so liblog has to be a declared dependency: with a
# static-library toolchain the linker only resolves left to right, and callers that merely
# list log before http (as every test suite does) leave those symbols undefined.
target_link_libraries(http base64 log)
set_property(TARGET http PROPERTY FOLDER "shared")
