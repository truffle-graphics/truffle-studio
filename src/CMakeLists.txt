add_executable(truffle_studio main.cpp)
target_link_libraries(truffle_studio PRIVATE
    Truffle::GUIDock Truffle::GUI Truffle::Renderer Truffle::BackendMetal SDL3::SDL3-static)
studio_target_warnings(truffle_studio)
set_target_properties(truffle_studio PROPERTIES OUTPUT_NAME truffle-studio)
