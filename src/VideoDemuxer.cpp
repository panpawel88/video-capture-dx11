#include "VideoDemuxer.h"
#include "IDataSource.h"
#include "Logger.h"
#include <iostream>

extern "C" {
#include <libavutil/error.h>
}

VideoDemuxer::VideoDemuxer()
    : m_formatContext(nullptr)
    , m_ioContext(nullptr)
    , m_dataSource(nullptr)
    , m_ioBuffer(nullptr)
    , m_videoStreamIndex(-1)
    , m_videoStream(nullptr) {
}

VideoDemuxer::~VideoDemuxer() {
    Close();
}

bool VideoDemuxer::Open(const std::string& filePath) {
    Close();

    // Open input file
    int ret = avformat_open_input(&m_formatContext, filePath.c_str(), nullptr, nullptr);
    if (ret < 0) {
        char errorBuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errorBuf, sizeof(errorBuf));
        LOG_ERROR("Cannot open file ", filePath, ": ", errorBuf);
        return false;
    }

    // Retrieve stream information
    ret = avformat_find_stream_info(m_formatContext, nullptr);
    if (ret < 0) {
        char errorBuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errorBuf, sizeof(errorBuf));
        LOG_ERROR("Cannot find stream info for ", filePath, ": ", errorBuf);
        Close();
        return false;
    }

    // Find video stream
    if (!FindVideoStream()) {
        LOG_ERROR("No video stream found in ", filePath);
        Close();
        return false;
    }

    LOG_INFO("Successfully opened video file: ", filePath);
    LOG_INFO("  Resolution: ", GetWidth(), "x", GetHeight());
    LOG_INFO("  Frame rate: ", GetFrameRate(), " FPS");
    LOG_INFO("  Duration: ", GetDuration(), " seconds");
    AVRational timebase = GetTimeBase();
    LOG_INFO("  Timebase: ", timebase.num, "/", timebase.den,
              " (", av_q2d(timebase), " seconds per unit)");

    return true;
}

bool VideoDemuxer::Open(IDataSource* dataSource, const std::string& format) {
    Close();

    if (!dataSource) {
        LOG_ERROR("Invalid data source");
        return false;
    }

    m_dataSource = dataSource;

    // Setup custom IO
    if (!SetupCustomIO(dataSource, format)) {
        LOG_ERROR("Failed to setup custom IO");
        Close();
        return false;
    }

    // Retrieve stream information
    int ret = avformat_find_stream_info(m_formatContext, nullptr);
    if (ret < 0) {
        char errorBuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errorBuf, sizeof(errorBuf));
        LOG_ERROR("Cannot find stream info: ", errorBuf);
        Close();
        return false;
    }

    // Find video stream
    if (!FindVideoStream()) {
        LOG_ERROR("No video stream found");
        Close();
        return false;
    }

    LOG_INFO("Successfully opened video from custom data source");
    LOG_INFO("  Resolution: ", GetWidth(), "x", GetHeight());
    LOG_INFO("  Frame rate: ", GetFrameRate(), " FPS");
    LOG_INFO("  Duration: ", GetDuration(), " seconds");
    AVRational timebase = GetTimeBase();
    LOG_INFO("  Timebase: ", timebase.num, "/", timebase.den,
              " (", av_q2d(timebase), " seconds per unit)");

    return true;
}

void VideoDemuxer::Close() {
    Reset();
}

bool VideoDemuxer::ReadFrame(AVPacket* packet) {
    if (!m_formatContext || m_videoStreamIndex < 0) {
        LOG_DEBUG("ReadFrame failed - no format context or invalid video stream index");
        return false;
    }

    while (true) {
        int ret = av_read_frame(m_formatContext, packet);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                LOG_DEBUG("End of file reached");
            } else {
                char errorBuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errorBuf, sizeof(errorBuf));
                LOG_DEBUG("av_read_frame failed: ", errorBuf, " (ret=", ret, ")");
            }
            return false;
        }

        // Only return packets from the video stream
        if (packet->stream_index == m_videoStreamIndex) {
            LOG_DEBUG("Read video packet - Size: ", packet->size,
                     ", PTS: ", packet->pts,
                     ", DTS: ", packet->dts,
                     ", Stream: ", packet->stream_index,
                     ", Flags: ", packet->flags);
            return true;
        }

        // Free packet if it's not from the video stream
        LOG_DEBUG("Skipping non-video packet from stream ", packet->stream_index);
        av_packet_unref(packet);
    }
}

bool VideoDemuxer::SeekToTime(double timeInSeconds) {
    if (!m_formatContext || !m_videoStream) {
        return false;
    }

    int64_t timestamp = SecondsToPacketTime(timeInSeconds);

    LOG_DEBUG("Seeking to time ", timeInSeconds, " seconds (timestamp: ", timestamp, ")");

    int ret = av_seek_frame(m_formatContext, m_videoStreamIndex, timestamp, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        char errorBuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errorBuf, sizeof(errorBuf));
        LOG_ERROR("Seek failed: ", errorBuf);
        return false;
    }

    LOG_DEBUG("Seek completed successfully");
    return true;
}

bool VideoDemuxer::SeekToFrame(int64_t frameNumber) {
    if (!m_formatContext || !m_videoStream) {
        return false;
    }

    double timeInSeconds = frameNumber / GetFrameRate();
    return SeekToTime(timeInSeconds);
}

double VideoDemuxer::GetDuration() const {
    if (!m_formatContext) {
        return 0.0;
    }

    if (m_formatContext->duration != AV_NOPTS_VALUE) {
        return static_cast<double>(m_formatContext->duration) / AV_TIME_BASE;
    }

    return 0.0;
}

double VideoDemuxer::GetFrameRate() const {
    if (!m_videoStream) {
        return 0.0;
    }

    if (m_videoStream->avg_frame_rate.num != 0 && m_videoStream->avg_frame_rate.den != 0) {
        return av_q2d(m_videoStream->avg_frame_rate);
    } else if (m_videoStream->r_frame_rate.num != 0 && m_videoStream->r_frame_rate.den != 0) {
        return av_q2d(m_videoStream->r_frame_rate);
    }

    return 25.0; // Default fallback
}

int VideoDemuxer::GetWidth() const {
    if (!m_videoStream) {
        return 0;
    }
    return m_videoStream->codecpar->width;
}

int VideoDemuxer::GetHeight() const {
    if (!m_videoStream) {
        return 0;
    }
    return m_videoStream->codecpar->height;
}

AVCodecID VideoDemuxer::GetCodecID() const {
    if (!m_videoStream) {
        return AV_CODEC_ID_NONE;
    }
    return m_videoStream->codecpar->codec_id;
}

AVCodecParameters* VideoDemuxer::GetCodecParameters() const {
    if (!m_videoStream) {
        return nullptr;
    }
    return m_videoStream->codecpar;
}

int VideoDemuxer::GetVideoStreamIndex() const {
    return m_videoStreamIndex;
}

AVRational VideoDemuxer::GetTimeBase() const {
    if (!m_videoStream) {
        return {0, 1};
    }
    return m_videoStream->time_base;
}

double VideoDemuxer::PacketTimeToSeconds(int64_t pts) const {
    if (!m_videoStream || pts == AV_NOPTS_VALUE) {
        return 0.0;
    }
    return static_cast<double>(pts) * av_q2d(m_videoStream->time_base);
}

int64_t VideoDemuxer::SecondsToPacketTime(double seconds) const {
    if (!m_videoStream) {
        return 0;
    }
    return static_cast<int64_t>(seconds / av_q2d(m_videoStream->time_base));
}

bool VideoDemuxer::IsValidPacket(const AVPacket* packet) const {
    return packet && packet->stream_index == m_videoStreamIndex;
}

bool VideoDemuxer::FindVideoStream() {
    if (!m_formatContext) {
        return false;
    }

    for (unsigned int i = 0; i < m_formatContext->nb_streams; i++) {
        if (m_formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_videoStreamIndex = i;
            m_videoStream = m_formatContext->streams[i];

            // Validate codec support (H264/H265/AV1 only)
            AVCodecID codecId = m_videoStream->codecpar->codec_id;
            if (codecId != AV_CODEC_ID_H264 && codecId != AV_CODEC_ID_HEVC && codecId != AV_CODEC_ID_AV1) {
                LOG_ERROR("Unsupported video codec found. Only H264, H265, and AV1 are supported.");
                return false;
            }

            return true;
        }
    }

    return false;
}

bool VideoDemuxer::SetupCustomIO(IDataSource* dataSource, const std::string& format) {
    const size_t IO_BUFFER_SIZE = 32768; // 32KB buffer

    // Allocate IO buffer
    m_ioBuffer = static_cast<uint8_t*>(av_malloc(IO_BUFFER_SIZE));
    if (!m_ioBuffer) {
        LOG_ERROR("Failed to allocate IO buffer");
        return false;
    }

    // Create AVIOContext with custom callbacks
    m_ioContext = avio_alloc_context(
        m_ioBuffer,
        IO_BUFFER_SIZE,
        0,                          // write_flag (0 = read-only)
        dataSource,                 // opaque user data
        &VideoDemuxer::ReadPacket,  // read_packet callback
        nullptr,                    // write_packet callback
        &VideoDemuxer::Seek         // seek callback
    );

    if (!m_ioContext) {
        LOG_ERROR("Failed to allocate AVIOContext");
        av_free(m_ioBuffer);
        m_ioBuffer = nullptr;
        return false;
    }

    // Allocate format context
    m_formatContext = avformat_alloc_context();
    if (!m_formatContext) {
        LOG_ERROR("Failed to allocate AVFormatContext");
        return false;
    }

    // Assign custom IO context
    m_formatContext->pb = m_ioContext;
    m_formatContext->flags |= AVFMT_FLAG_CUSTOM_IO;

    // Determine input format
    const AVInputFormat* inputFormat = nullptr;
    if (!format.empty()) {
        inputFormat = av_find_input_format(format.c_str());
        if (!inputFormat) {
            LOG_WARNING("Could not find input format: ", format);
        }
    }

    // Open input (with empty filename for custom IO)
    int ret = avformat_open_input(&m_formatContext, "", inputFormat, nullptr);
    if (ret < 0) {
        char errorBuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errorBuf, sizeof(errorBuf));
        LOG_ERROR("Cannot open custom input: ", errorBuf);
        return false;
    }

    LOG_DEBUG("Custom IO setup complete");
    return true;
}

void VideoDemuxer::Reset() {
    if (m_formatContext) {
        avformat_close_input(&m_formatContext);
        m_formatContext = nullptr;
    }

    if (m_ioContext) {
        // Note: ioContext->buffer is freed by avformat_close_input if the context was used
        // If not, we need to free it manually
        if (m_ioBuffer) {
            av_freep(&m_ioContext->buffer);
        }
        avio_context_free(&m_ioContext);
        m_ioContext = nullptr;
    }

    if (m_ioBuffer) {
        // Already freed by avio_context_free or avformat_close_input
        m_ioBuffer = nullptr;
    }

    m_dataSource = nullptr;
    m_videoStreamIndex = -1;
    m_videoStream = nullptr;
}

int VideoDemuxer::ReadPacket(void* opaque, uint8_t* buf, int buf_size) {
    IDataSource* dataSource = static_cast<IDataSource*>(opaque);
    if (!dataSource) {
        return AVERROR(EIO);
    }

    int bytesRead = dataSource->Read(buf, buf_size);
    if (bytesRead < 0) {
        // Return FFmpeg error code
        return bytesRead;
    }
    if (bytesRead == 0) {
        // End of file
        return AVERROR_EOF;
    }

    return bytesRead;
}

int64_t VideoDemuxer::Seek(void* opaque, int64_t offset, int whence) {
    IDataSource* dataSource = static_cast<IDataSource*>(opaque);
    if (!dataSource) {
        return AVERROR(EIO);
    }

    if (!dataSource->IsSeekable()) {
        return AVERROR(ENOSYS);
    }

    return dataSource->Seek(offset, whence);
}