#pragma once

#include <string>
#include <memory>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

class VideoDemuxer {
public:
    VideoDemuxer();
    ~VideoDemuxer();

    bool Open(const std::string& filePath);
    void Close();

    bool ReadFrame(AVPacket* packet);
    bool SeekToTime(double timeInSeconds);
    bool SeekToFrame(int64_t frameNumber);

    // Getters
    double GetDuration() const;
    double GetFrameRate() const;
    int GetWidth() const;
    int GetHeight() const;
    AVCodecID GetCodecID() const;
    AVCodecParameters* GetCodecParameters() const;
    int GetVideoStreamIndex() const;
    AVRational GetTimeBase() const;

    // Utility functions
    double PacketTimeToSeconds(int64_t pts) const;
    int64_t SecondsToPacketTime(double seconds) const;
    bool IsValidPacket(const AVPacket* packet) const;

private:
    AVFormatContext* m_formatContext;
    int m_videoStreamIndex;
    AVStream* m_videoStream;

    bool FindVideoStream();
    void Reset();
};