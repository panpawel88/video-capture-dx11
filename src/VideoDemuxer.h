#pragma once

#include <string>
#include <memory>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

class IDataSource;

class VideoDemuxer {
public:
    VideoDemuxer();
    ~VideoDemuxer();

    bool Open(const std::string& filePath);
    bool Open(IDataSource* dataSource, const std::string& format = "");
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
    AVIOContext* m_ioContext;
    IDataSource* m_dataSource;
    uint8_t* m_ioBuffer;
    int m_videoStreamIndex;
    AVStream* m_videoStream;

    bool FindVideoStream();
    bool SetupCustomIO(IDataSource* dataSource, const std::string& format);
    void Reset();

    // Static callbacks for AVIOContext
    static int ReadPacket(void* opaque, uint8_t* buf, int buf_size);
    static int64_t Seek(void* opaque, int64_t offset, int whence);
};