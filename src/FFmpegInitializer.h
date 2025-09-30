#pragma once

class FFmpegInitializer {
public:
    FFmpegInitializer() = default;
    ~FFmpegInitializer();

    bool Initialize();

    FFmpegInitializer(const FFmpegInitializer&) = delete;
    FFmpegInitializer& operator=(const FFmpegInitializer&) = delete;
    FFmpegInitializer(FFmpegInitializer&&) = delete;
    FFmpegInitializer& operator=(FFmpegInitializer&&) = delete;

private:
    bool initialized = false;
};