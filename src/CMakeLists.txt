# Document, sketch and solver code: no Truffle, SDL or OpenCASCADE dependency.
add_library(studio_model INTERFACE)
target_include_directories(studio_model INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(studio_model INTERFACE cxx_std_20)

# The geometry kernel: the only code that sees OpenCASCADE.
add_library(studio_kernel STATIC kernel/occt_kernel.cpp)
target_include_directories(studio_kernel PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(studio_kernel PRIVATE studio_occt)
target_compile_features(studio_kernel PUBLIC cxx_std_20)
studio_target_warnings(studio_kernel)

# Feature regeneration: document features to kernel solids.
add_library(studio_features STATIC features/regenerate.cpp)
target_link_libraries(studio_features PUBLIC studio_model studio_kernel)
studio_target_warnings(studio_features)

if(TFLST_BUILD_APP)
if(WIN32)
    set(studio_font_source host/system_font_win32.cpp)
    set(studio_native_libraries Truffle::BackendVulkan gdi32 user32)
else()
    set(studio_font_source host/system_font.mm)
    set(studio_native_libraries Truffle::BackendMetal "-framework CoreText" "-framework Foundation")
endif()
add_executable(truffle_studio
    main.cpp
    host/sdl_window.cpp
    ${studio_font_source}
    studio/app.cpp
    studio/scene_view.cpp
    studio/shell.cpp
    studio/cad/cad_workspace.cpp
    studio/cad/model_view.cpp
    studio/cad/sketch_editor.cpp)
target_include_directories(truffle_studio PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(truffle_studio PRIVATE
    studio_model studio_features Truffle::GUIDock Truffle::GUI Truffle::Renderer
    Truffle::RendererPresentation SDL3::SDL3-static
    ${studio_native_libraries})
target_compile_options(truffle_studio PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
studio_target_warnings(truffle_studio)
set_target_properties(truffle_studio PROPERTIES OUTPUT_NAME truffle-studio
    BUILD_RPATH "@loader_path/../lib")

if(BUILD_TESTING)
    add_test(NAME truffle_studio_smoke COMMAND truffle_studio --smoke)
    set_tests_properties(truffle_studio_smoke PROPERTIES
        LABELS "native-smoke;gui;host-window"
        TIMEOUT 120
        FAIL_REGULAR_EXPRESSION "Studio graphics diagnostic:;VUID-")
    if(APPLE)
        set_tests_properties(truffle_studio_smoke PROPERTIES
            ENVIRONMENT "MTL_DEBUG_LAYER=1;MTL_SHADER_VALIDATION=1;METAL_DEVICE_WRAPPER_TYPE=1")
    endif()
endif()
endif()
