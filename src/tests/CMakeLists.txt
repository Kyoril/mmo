# One test executable per library. Each subdirectory holds that library's tests and a
# CMakeLists.txt whose only content is an mmo_add_test() call naming the libraries under
# test. Suites that need the renderer are gated on a client or editor build; everything
# else has to keep working on the headless Linux server build.
