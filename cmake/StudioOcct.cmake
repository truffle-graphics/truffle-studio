# OpenCASCADE (OCCT) is built from the external/occt submodule as an isolated
# ExternalProject: shared libraries (LGPL-2.1 with exception), only the
# modelling toolkits Studio's kernel uses, no visualisation, Draw or Tcl.
# The first build compiles OCCT and takes a while; later builds reuse it.

include(ExternalProject)

set(STUDIO_OCCT_TOOLKITS
    TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo TKPrim TKMesh TKBO)
set(STUDIO_OCCT_PREFIX "${STUDIO_DEPENDENCY_PREFIX}/occt")
set(STUDIO_OCCT_LIBRARIES)
foreach(toolkit IN LISTS STUDIO_OCCT_TOOLKITS)
    list(APPEND STUDIO_OCCT_LIBRARIES
        "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/${CMAKE_SHARED_LIBRARY_PREFIX}${toolkit}${CMAKE_SHARED_LIBRARY_SUFFIX}")
endforeach()

# OCCT is always optimised: its debug build is very slow for meshing.
ExternalProject_Add(studio_occt_build
    SOURCE_DIR "${PROJECT_SOURCE_DIR}/external/occt"
    BINARY_DIR "${CMAKE_BINARY_DIR}/occt-build"
    INSTALL_DIR "${STUDIO_OCCT_PREFIX}"
    LIST_SEPARATOR |
    CMAKE_ARGS
        -G Ninja
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        -DINSTALL_DIR_LIB=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
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
    BUILD_BYPRODUCTS ${STUDIO_OCCT_LIBRARIES}
    UPDATE_COMMAND ""
    USES_TERMINAL_BUILD ON)

# Include directory must exist at configure time for the imported target.
file(MAKE_DIRECTORY "${STUDIO_OCCT_PREFIX}/include/opencascade")
add_library(studio_occt INTERFACE)
add_dependencies(studio_occt studio_occt_build)
# OCCT headers are third-party: include them as system headers so Studio's
# strict warnings do not apply to them.
target_include_directories(studio_occt SYSTEM INTERFACE "${STUDIO_OCCT_PREFIX}/include/opencascade")
target_link_libraries(studio_occt INTERFACE ${STUDIO_OCCT_LIBRARIES})
set(STUDIO_OCCT_LIBRARY_DIR "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
