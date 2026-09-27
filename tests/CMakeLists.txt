# Tests for InkscapeVectorizerFull (v1.2)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBXML libxml-2.0 REQUIRED)

add_executable(test_core test_core.cpp)
target_include_directories(test_core SYSTEM PRIVATE ${LIBXML_INCLUDE_DIRS})
target_link_libraries(test_core PRIVATE ivfcore ${LIBXML_LIBRARIES})
add_test(NAME core COMMAND test_core)
