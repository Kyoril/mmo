mmo_add_test(launcher_tests base log)
target_sources(launcher_tests PRIVATE
	${PROJECT_SOURCE_DIR}/src/launcher/launcher_content.cpp
	${PROJECT_SOURCE_DIR}/src/launcher/launcher_model.cpp)
target_include_directories(launcher_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/launcher)
if (WIN32)
	target_sources(launcher_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/launcher/update_worker.cpp)
	target_link_libraries(launcher_tests updater ${OPENSSL_LIBRARIES})
endif()
