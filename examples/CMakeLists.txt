cmake_minimum_required(VERSION 3.20)

# Simple player example
add_executable(simple_player WIN32
    simple_player.cpp
)

target_link_libraries(simple_player
    PRIVATE
        VideoCaptureDX11
        d3d11.lib
        dxgi.lib
        d3dcompiler.lib
)

set_target_properties(simple_player PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

message(STATUS "Example application configured: simple_player")