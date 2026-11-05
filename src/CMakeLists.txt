# Document, sketch and solver code: no Truffle, SDL or OpenCASCADE dependency.
add_library(studio_model INTERFACE)
target_include_directories(studio_model INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(studio_model INTERFACE cxx_std_20)

add_executable(truffle_studio
    main.cpp
    host/sdl_window.cpp
    host/system_font.mm
    studio/app.cpp
    studio/workspace.cpp)
target_include_directories(truffle_studio PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(truffle_studio PRIVATE
    studio_model Truffle::GUIDock Truffle::GUI Truffle::Renderer Truffle::BackendMetal SDL3::SDL3-static
    "-framework CoreText" "-framework Foundation")
target_compile_options(truffle_studio PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
studio_target_warnings(truffle_studio)
set_target_properties(truffle_studio PROPERTIES OUTPUT_NAME truffle-studio)

if(BUILD_TESTING)
    add_test(NAME truffle_studio_smoke COMMAND truffle_studio --smoke)
    set_tests_properties(truffle_studio_smoke PROPERTIES
        LABELS "native-smoke;gui;host-window"
        TIMEOUT 60
        ENVIRONMENT "MTL_DEBUG_LAYER=1;MTL_SHADER_VALIDATION=1;METAL_DEVICE_WRAPPER_TYPE=1")
endif()
