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

# Copy VideoCaptureDX11 runtime dependencies (FFmpeg DLLs)
copy_videocapture_dependencies(simple_player)

# Stream player example (demonstrates custom IO with BufferDataSource)
add_executable(stream_player WIN32
    stream_player.cpp
)

target_link_libraries(stream_player
    PRIVATE
        VideoCaptureDX11
        d3d11.lib
        dxgi.lib
        d3dcompiler.lib
        wininet.lib
)

set_target_properties(stream_player PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

copy_videocapture_dependencies(stream_player)

# WebRTC player example (requires BUILD_WEBRTC_SUPPORT=ON)
if(BUILD_WEBRTC_SUPPORT)
    add_executable(webrtc_player WIN32
        webrtc_player.cpp
    )

    target_link_libraries(webrtc_player
        PRIVATE
            VideoCaptureDX11
            d3d11.lib
            dxgi.lib
            d3dcompiler.lib
    )

    set_target_properties(webrtc_player PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    copy_videocapture_dependencies(webrtc_player)

    message(STATUS "Example applications configured: simple_player, stream_player, webrtc_player")
else()
    message(STATUS "Example applications configured: simple_player, stream_player")
    message(STATUS "  Note: webrtc_player requires BUILD_WEBRTC_SUPPORT=ON")
endif()