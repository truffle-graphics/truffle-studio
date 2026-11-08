# OpenCASCADE (OCCT) is built from the external/occt submodule as an isolated
# ExternalProject: shared libraries (LGPL-2.1 with exception), only the
# modelling toolkits Studio's kernel uses, no visualisation, Draw or Tcl.
# The first build compiles OCCT and takes a while; later builds reuse it.

include(ExternalProject)

set(TFLST_OCCT_TOOLKITS
    TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo TKPrim TKMesh TKBO)
set(TFLST_OCCT_PREFIX "${TFLST_DEPENDENCY_PREFIX}/occt")
set(TFLST_OCCT_LIBRARIES)
foreach(toolkit IN LISTS TFLST_OCCT_TOOLKITS)
    if(WIN32)
        list(APPEND TFLST_OCCT_LIBRARIES "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}/${toolkit}.lib")
        list(APPEND TFLST_OCCT_RUNTIME "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${toolkit}.dll")
    else()
        list(APPEND TFLST_OCCT_LIBRARIES
            "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/${CMAKE_SHARED_LIBRARY_PREFIX}${toolkit}${CMAKE_SHARED_LIBRARY_SUFFIX}")
    endif()
endforeach()

# OCCT is always optimised: its debug build is very slow for meshing.
ExternalProject_Add(studio_occt_build
    SOURCE_DIR "${PROJECT_SOURCE_DIR}/external/occt"
    BINARY_DIR "${CMAKE_BINARY_DIR}/occt-build"
    INSTALL_DIR "${TFLST_OCCT_PREFIX}"
    LIST_SEPARATOR |
    CMAKE_ARGS
        -G Ninja
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        -DINSTALL_DIR_LIB=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        -DINSTALL_DIR_BIN=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        -DINSTALL_DIR_INCLUDE=include/opencascade
        -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
        -DINSTALL_DIR_LAYOUT=Unix
        -DBUILD_LIBRARY_TYPE=Shared
        -DBUILD_MODULE_FoundationClasses=OFF
        -DBUILD_MODULE_ModelingData=OFF
        -DBUILD_MODULE_ModelingAlgorithms=OFF
        -DBUILD_MODULE_Visualization=OFF
        -DBUILD_MODULE_ApplicationFramework=OFF
        -DBUILD_MODULE_DataExchange=OFF
        -DBUILD_MODULE_Draw=OFF
        -DBUILD_ADDITIONAL_TOOLKITS=TKPrim|TKMesh|TKBO
        -DBUILD_DOC_Overview=OFF
        -DBUILD_GTEST=OFF
        -DUSE_TK=OFF
        -DUSE_TCL=OFF
        -DUSE_FREETYPE=OFF
        -DUSE_FREEIMAGE=OFF
        -DUSE_OPENGL=OFF
        -DUSE_GLES2=OFF
        -DUSE_XLIB=OFF
        -DUSE_TBB=OFF
        -DUSE_VTK=OFF
        -DUSE_RAPIDJSON=OFF
        -DUSE_DRACO=OFF
    BUILD_BYPRODUCTS ${TFLST_OCCT_LIBRARIES} ${TFLST_OCCT_RUNTIME}
    UPDATE_COMMAND ""
    USES_TERMINAL_BUILD ON)

# Include directory must exist at configure time for the imported target.
file(MAKE_DIRECTORY "${TFLST_OCCT_PREFIX}/include/opencascade")
add_library(studio_occt INTERFACE)
add_dependencies(studio_occt studio_occt_build)
# OCCT headers are third-party: include them as system headers so Studio's
# strict warnings do not apply to them.
target_include_directories(studio_occt SYSTEM INTERFACE "${TFLST_OCCT_PREFIX}/include/opencascade")
target_link_libraries(studio_occt INTERFACE ${TFLST_OCCT_LIBRARIES})
set(TFLST_OCCT_LIBRARY_DIR "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
