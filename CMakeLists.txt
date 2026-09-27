# InkscapeVectorizerFull — standalone Trace Bitmap application (v1.2)
#
# Derived from Inkscape (https://gitlab.com/inkscape/inkscape), tag
# INKSCAPE_1_4_4. Licensed under GNU GPL v2 or later; see LICENSE and NOTICE.
cmake_minimum_required(VERSION 3.10)
project(InkscapeVectorizerFull VERSION 1.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIBMM REQUIRED glibmm-2.4)
pkg_check_modules(GDKMM REQUIRED gdkmm-3.0)   # gdk-pixbuf (Pixbuf) — gtkmm NOT required
pkg_check_modules(LIB2GEOM REQUIRED 2geom)    # lib2geom (path geometry)

# potrace ships no pkg-config file on some distros; locate it manually.
find_path(POTRACE_INCLUDE_DIR potracelib.h)
find_library(POTRACE_LIBRARY NAMES potrace)
if(NOT POTRACE_INCLUDE_DIR OR NOT POTRACE_LIBRARY)
    message(FATAL_ERROR "potrace development files not found (install potrace-devel / libpotrace-dev)")
endif()
message(STATUS "potrace: ${POTRACE_LIBRARY}")

# ---------------------------------------------------------------------------
# Core tracing library (extracted from Inkscape src/trace + new glue)
# ---------------------------------------------------------------------------
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/core)

add_library(ivfcore STATIC
    ${CORE_DIR}/trace/trace.cpp
    ${CORE_DIR}/trace/imagemap.cpp
    ${CORE_DIR}/trace/imagemap-gdk.cpp
    ${CORE_DIR}/trace/filterset.cpp
    ${CORE_DIR}/trace/quantize.cpp
    ${CORE_DIR}/trace/potrace/inkscape-potrace.cpp
    ${CORE_DIR}/imageio.cpp
    ${CORE_DIR}/tracer.cpp
)

target_include_directories(ivfcore PUBLIC
    ${CORE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CORE_DIR}/trace
    ${CORE_DIR}/trace/potrace
    ${POTRACE_INCLUDE_DIR}
)

target_include_directories(ivfcore SYSTEM PUBLIC
    ${GLIBMM_INCLUDE_DIRS} ${GDKMM_INCLUDE_DIRS} ${LIB2GEOM_INCLUDE_DIRS}
)
target_link_libraries(ivfcore PUBLIC
    ${GLIBMM_LIBRARIES} ${GDKMM_LIBRARIES} ${LIB2GEOM_LIBRARIES} ${POTRACE_LIBRARY}
)
target_compile_options(ivfcore PUBLIC ${GLIBMM_CFLAGS_OTHER} ${GDKMM_CFLAGS_OTHER})

find_package(Threads REQUIRED)
target_link_libraries(ivfcore PUBLIC Threads::Threads)

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
add_executable(trace-bitmap src/cli/main.cpp)
target_link_libraries(trace-bitmap PRIVATE ivfcore)

# ---------------------------------------------------------------------------
# GUI (GTK3, optional)
# ---------------------------------------------------------------------------
option(IVF_BUILD_GUI "Build the GTK3 GUI (trace-bitmap-gui)" ON)
if(IVF_BUILD_GUI)
    pkg_check_modules(GTKMM REQUIRED gtkmm-3.0)  # widgets needed only here
    add_executable(trace-bitmap-gui src/gui/main.cpp)
    target_include_directories(trace-bitmap-gui SYSTEM PRIVATE ${GTKMM_INCLUDE_DIRS})
    target_link_libraries(trace-bitmap-gui PRIVATE ivfcore ${GTKMM_LIBRARIES})
    target_compile_options(trace-bitmap-gui PRIVATE ${GTKMM_CFLAGS_OTHER})
    install(TARGETS trace-bitmap-gui RUNTIME DESTINATION bin)
    message(STATUS "GUI enabled (gtkmm-3.0 ${GTKMM_VERSION})")
endif()
option(IVF_BUILD_TESTS "Build tests" ON)
if(IVF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(TARGETS trace-bitmap RUNTIME DESTINATION bin)
