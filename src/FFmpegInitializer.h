#pragma once

// Forward declaration
struct ID3D11Device;

class FFmpegInitializer {
public:
    FFmpegInitializer() = default;
    ~FFmpegInitializer();

    bool Initialize(ID3D11Device* d3dDevice = nullptr);

    FFmpegInitializer(const FFmpegInitializer&) = delete;
    FFmpegInitializer& operator=(const FFmpegInitializer&) = delete;
    FFmpegInitializer(FFmpegInitializer&&) = delete;
    FFmpegInitializer& operator=(FFmpegInitializer&&) = delete;

private:
    bool initialized = false;
};