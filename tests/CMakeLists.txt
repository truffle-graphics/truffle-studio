add_executable(studio_tests main.cpp document_tests.cpp kernel_tests.cpp sketch_tests.cpp solver_tests.cpp view_tests.cpp feature_tests.cpp)
target_link_libraries(studio_tests PRIVATE studio_model studio_features)
set_target_properties(studio_tests PROPERTIES BUILD_RPATH "@loader_path/../lib")
studio_target_warnings(studio_tests)
add_test(NAME studio_unit COMMAND studio_tests)
set_tests_properties(studio_unit PROPERTIES LABELS "unit")

if(WIN32 AND TFLST_BUILD_APP)
    add_executable(studio_host_tests main.cpp host_tests.cpp
        ../src/host/sdl_window.cpp ../src/host/system_font_win32.cpp)
    target_link_libraries(studio_host_tests PRIVATE studio_model Truffle::GUI
        SDL3::SDL3-static gdi32 user32)
    studio_target_warnings(studio_host_tests)
    add_test(NAME studio_win32_host COMMAND studio_host_tests)
    set_tests_properties(studio_win32_host PROPERTIES LABELS "native-smoke;host-window" TIMEOUT 30)
endif()
if(WIN32)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME studio_windows_scripts
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/windows_script_tests.py")
        set_tests_properties(studio_windows_scripts PROPERTIES LABELS "tooling" TIMEOUT 60)
    endif()
endif()
