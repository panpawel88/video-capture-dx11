cmake_minimum_required(VERSION 3.20)
project(VideoCaptureDX11 VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Windows-only build
if(NOT WIN32)
    message(FATAL_ERROR "This library only supports Windows")
endif()

# Fix runtime library conflicts on Windows
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    # Use dynamic runtime library
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

    # Explicitly set C++20 flag for MSVC
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/std:c++20>)

    # Build performance optimizations for MSVC
    add_compile_options(
        $<$<COMPILE_LANGUAGE:CXX>:/MP>         # Enable parallel compilation
        $<$<COMPILE_LANGUAGE:CXX>:/bigobj>     # Increase object file capacity
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/Od>       # No optimization in Debug
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/Zi>       # Debug info
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/O2>     # Maximum speed optimization
    )

    # Linker optimizations for Release
    add_link_options(
        $<$<CONFIG:Release>:/OPT:REF>  # Remove unreferenced code
        $<$<CONFIG:Release>:/OPT:ICF>  # Remove duplicate code
    )
endif()

# Enable Unicode support for Windows
add_definitions(-DUNICODE -D_UNICODE)

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# FFmpeg download configuration using FetchContent
include(FetchContent)

set(FFMPEG_VERSION "7.1.1")
set(FFMPEG_URL "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-n7.1-latest-win64-lgpl-shared-7.1.zip")

FetchContent_Declare(
    FFmpeg
    URL ${FFMPEG_URL}
    SOURCE_DIR "${CMAKE_BINARY_DIR}/ffmpeg"
)

FetchContent_MakeAvailable(FFmpeg)

# Set FFmpeg paths
set(FFMPEG_DIR "${CMAKE_BINARY_DIR}/ffmpeg")
set(FFMPEG_INCLUDE_DIR "${FFMPEG_DIR}/include")
set(FFMPEG_LIB_DIR "${FFMPEG_DIR}/lib")
set(FFMPEG_BIN_DIR "${FFMPEG_DIR}/bin")

# Find FFmpeg libraries (minimal set for hardware decoding)
find_library(AVCODEC_LIB avcodec PATHS ${FFMPEG_LIB_DIR} NO_DEFAULT_PATH)
find_library(AVFORMAT_LIB avformat PATHS ${FFMPEG_LIB_DIR} NO_DEFAULT_PATH)
find_library(AVUTIL_LIB avutil PATHS ${FFMPEG_LIB_DIR} NO_DEFAULT_PATH)

# Verify FFmpeg libraries were found
if(NOT AVCODEC_LIB OR NOT AVFORMAT_LIB OR NOT AVUTIL_LIB)
    message(FATAL_ERROR "Could not find required FFmpeg libraries")
endif()

# Create FFmpeg target
add_library(FFmpeg INTERFACE)
target_include_directories(FFmpeg INTERFACE ${FFMPEG_INCLUDE_DIR})
target_link_libraries(FFmpeg INTERFACE ${AVCODEC_LIB} ${AVFORMAT_LIB} ${AVUTIL_LIB})

message(STATUS "FFmpeg libraries found:")
message(STATUS "  avcodec: ${AVCODEC_LIB}")
message(STATUS "  avformat: ${AVFORMAT_LIB}")
message(STATUS "  avutil: ${AVUTIL_LIB}")

# Library source files
set(LIBRARY_SOURCES
    src/VideoCapture.cpp
    src/VideoDemuxer.cpp
    src/VideoDecoder.cpp
    src/HardwareDecoder.cpp
    src/Logger.cpp
    src/FFmpegInitializer.cpp
    src/FileDataSource.cpp
    src/BufferDataSource.cpp
)

set(LIBRARY_HEADERS
    include/VideoCapture.h
    src/VideoDemuxer.h
    src/VideoDecoder.h
    src/HardwareDecoder.h
    src/Logger.h
    src/FFmpegInitializer.h
    src/IDataSource.h
    src/FileDataSource.h
    src/BufferDataSource.h
)

# Create static library
add_library(VideoCaptureDX11 STATIC ${LIBRARY_SOURCES} ${LIBRARY_HEADERS})

target_include_directories(VideoCaptureDX11
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(VideoCaptureDX11
    PUBLIC
        FFmpeg
        d3d11.lib
        dxgi.lib
)

# Set output name
set_target_properties(VideoCaptureDX11 PROPERTIES
    OUTPUT_NAME "VideoCaptureDX11"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# Function to copy VideoCaptureDX11 runtime dependencies (FFmpeg DLLs) to executable output directory
function(copy_videocapture_dependencies target_name)
    if(WIN32)
        file(GLOB FFMPEG_DLLS "${FFMPEG_BIN_DIR}/*.dll")
        foreach(DLL ${FFMPEG_DLLS})
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DLL}" $<TARGET_FILE_DIR:${target_name}>
            )
        endforeach()
    endif()
endfunction()

# Optional WebRTC support via libdatachannel
option(BUILD_WEBRTC_SUPPORT "Build with WebRTC streaming support (requires libdatachannel)" OFF)

if(BUILD_WEBRTC_SUPPORT)
    # Try to find libdatachannel via vcpkg or system first
    find_package(LibDataChannel QUIET)

    if(NOT LibDataChannel_FOUND)
        message(STATUS "libdatachannel not found via find_package, attempting FetchContent...")

        # Download and build libdatachannel via FetchContent
        FetchContent_Declare(
            libdatachannel
            GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
            GIT_TAG v0.23.2  # Use stable release
            GIT_SHALLOW TRUE
        )

        # Configure libdatachannel options
        set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
        set(NO_TESTS ON CACHE BOOL "" FORCE)
        set(NO_MEDIA OFF CACHE BOOL "" FORCE)  # We need media support for H264RtpDepacketizer
        set(NO_WEBSOCKET ON CACHE BOOL "" FORCE)

        FetchContent_MakeAvailable(libdatachannel)

        # Create alias for consistent target naming
        if(TARGET datachannel-static)
            add_library(LibDataChannel::LibDataChannel ALIAS datachannel-static)
            set(LibDataChannel_FOUND TRUE)
        elseif(TARGET datachannel)
            add_library(LibDataChannel::LibDataChannel ALIAS datachannel)
            set(LibDataChannel_FOUND TRUE)
        endif()
    endif()

    if(LibDataChannel_FOUND)
        message(STATUS "libdatachannel found - WebRTC support enabled")
        list(APPEND LIBRARY_SOURCES src/WebRTCDataSource.cpp)
        list(APPEND LIBRARY_HEADERS src/WebRTCDataSource.h)
        target_link_libraries(VideoCaptureDX11 PUBLIC LibDataChannel::LibDataChannel)
        target_compile_definitions(VideoCaptureDX11 PUBLIC WEBRTC_SUPPORT_ENABLED)
    else()
        message(WARNING "Failed to fetch libdatachannel - WebRTC support disabled")
        set(BUILD_WEBRTC_SUPPORT OFF CACHE BOOL "Build with WebRTC streaming support (requires libdatachannel)" FORCE)
    endif()
endif()

# Example application (optional)
option(BUILD_EXAMPLES "Build example application" ON)

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Installation rules
install(TARGETS VideoCaptureDX11
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES include/VideoCapture.h DESTINATION include)

message(STATUS "VideoCaptureDX11 library configuration complete")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Output directory: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")