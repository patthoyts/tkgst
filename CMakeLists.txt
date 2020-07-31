cmake_minimum_required(VERSION 3.12)

project(tkgst VERSION 0.1.0)

set (PKG_VERSION ${PROJECT_VERSION_MAJOR}${PROJECT_VERSION_MINOR}${PROJECT_VERSION_PATCH})
set (PKG_DOT_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")

find_package(TclStub REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GSTVIDEO REQUIRED gstreamer-video-1.0)

set (TARGETNAME ${PROJECT_NAME}${PKG_VERSION})
add_library(${TARGETNAME} SHARED tkgst.c)

include_directories(${TCL_INCLUDE_PATH} ${TK_INCLUDE_PATH} ${GSTREAMER_INCLUDE_DIRS} ${GSTVIDEO_INCLUDE_DIRS})
target_link_libraries(${TARGETNAME} ${TCL_STUB_LIBRARY} ${TK_STUB_LIBRARY} ${GSTREAMER_LIBRARIES} ${GSTVIDEO_LIBRARIES})
add_definitions(-DUSE_TCL_STUBS -DUSE_TK_STUBS -DPACKAGE_NAME="${PROJECT_NAME}")
add_definitions(-DPACKAGE_VERSION="${PKG_DOT_VERSION}")

# Generate the pkgIndex.tcl
file(GENERATE
    OUTPUT "pkgIndex.tcl"
    CONTENT "package ifneeded ${PROJECT_NAME} ${PKG_DOT_VERSION} [list load [file join $dir $<TARGET_FILE_NAME:${TARGETNAME}>]]\n")
