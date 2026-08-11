mmo_add_test(frame_ui_tests
	base)

# text_wrap.cpp is compiled in directly rather than linking frame_ui, which drags in the
# graphics device and freetype. The wrapping logic itself needs neither, so this keeps it
# covered on the headless server build.
target_sources(frame_ui_tests PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../../shared/frame_ui/text_wrap.cpp
)
