get_filename_component(_studio_build_name "${CMAKE_BINARY_DIR}" NAME)
set(STUDIO_OUTPUT_DIR "${PROJECT_SOURCE_DIR}/out/${_studio_build_name}"
    CACHE PATH "Root directory for built executables and libraries")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${STUDIO_OUTPUT_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${STUDIO_OUTPUT_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${STUDIO_OUTPUT_DIR}/lib")
set(STUDIO_DEPENDENCY_PREFIX "${PROJECT_SOURCE_DIR}/install/${_studio_build_name}/dependencies"
    CACHE PATH "Staging prefix for built dependencies")
