#include "../include/VideoCapture.h"
#include "VideoDemuxer.h"
#include "VideoDecoder.h"
#include "HardwareDecoder.h"
#include "Logger.h"
#include "FFmpegInitializer.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

// Static member initialization
ID3D11Device* VideoCapture::s_d3dDevice = nullptr;
bool VideoCapture::s_initialized = false;

VideoCapture::VideoCapture()
    : m_opened(false)
    , m_eof(false)
    , m_frameCount(0)
{
}

VideoCapture::~VideoCapture() {
    release();
}

bool VideoCapture::Initialize(ID3D11Device* device) {
    if (s_initialized) {
        return true;
    }

    if (!device) {
        LOG_ERROR("D3D11 device is required for VideoCapture initialization");
        return false;
    }

    s_d3dDevice = device;
    s_d3dDevice->AddRef(); // Keep reference to device

    // Initialize FFmpeg and hardware decoder detection
    static FFmpegInitializer ffmpegInit;
    if (!ffmpegInit.Initialize()) {
        LOG_ERROR("Failed to initialize FFmpeg");
        return false;
    }

    s_initialized = true;
    LOG_INFO("VideoCapture initialized successfully");
    return true;
}

bool VideoCapture::open(const std::string& filename) {
    if (!s_initialized) {
        LOG_ERROR("VideoCapture::Initialize() must be called before opening files");
        return false;
    }

    // Close any previously opened file
    release();

    // Create demuxer
    m_demuxer = std::make_unique<VideoDemuxer>();
    if (!m_demuxer->Open(filename)) {
        LOG_ERROR("Failed to open video file: ", filename);
        return false;
    }

    // Initialize decoder
    if (!InitializeDecoder()) {
        LOG_ERROR("Failed to initialize hardware decoder");
        release();
        return false;
    }

    // Calculate approximate frame count
    double duration = m_demuxer->GetDuration();
    double frameRate = m_demuxer->GetFrameRate();
    if (duration > 0.0 && frameRate > 0.0) {
        m_frameCount = static_cast<int64_t>(duration * frameRate);
    } else {
        m_frameCount = 0;
    }

    m_opened = true;
    m_eof = false;
    LOG_INFO("Video file opened successfully");
    return true;
}

bool VideoCapture::read(ID3D11Texture2D** outTexture, bool& isYUV, DXGI_FORMAT& format) {
    if (!m_opened || m_eof) {
        return false;
    }

    if (!DecodeNextFrame()) {
        m_eof = true;
        return false;
    }

    if (!m_currentFrame || !m_currentFrame->valid) {
        return false;
    }

    // Return texture reference
    *outTexture = m_currentFrame->texture.Get();
    if (*outTexture) {
        (*outTexture)->AddRef(); // Caller must release
    }

    isYUV = m_currentFrame->isYUV;
    format = m_currentFrame->format;

    return true;
}

double VideoCapture::get(int propId) const {
    if (!m_opened) {
        return 0.0;
    }

    switch (propId) {
        case CAP_PROP_FRAME_WIDTH:
            return static_cast<double>(m_demuxer->GetWidth());

        case CAP_PROP_FRAME_HEIGHT:
            return static_cast<double>(m_demuxer->GetHeight());

        case CAP_PROP_FPS:
            return m_demuxer->GetFrameRate();

        case CAP_PROP_FRAME_COUNT:
            return static_cast<double>(m_frameCount);

        case CAP_PROP_POS_MSEC:
            if (m_currentFrame && m_currentFrame->valid) {
                return m_currentFrame->presentationTime * 1000.0;
            }
            return 0.0;

        case CAP_PROP_POS_FRAMES:
            if (m_currentFrame && m_currentFrame->valid) {
                double frameRate = m_demuxer->GetFrameRate();
                if (frameRate > 0.0) {
                    return m_currentFrame->presentationTime * frameRate;
                }
            }
            return 0.0;

        case CAP_PROP_POS_AVI_RATIO:
            if (m_currentFrame && m_currentFrame->valid) {
                double duration = m_demuxer->GetDuration();
                if (duration > 0.0) {
                    return m_currentFrame->presentationTime / duration;
                }
            }
            return 0.0;

        default:
            LOG_WARNING("Unsupported property ID: ", propId);
            return 0.0;
    }
}

bool VideoCapture::set(int propId, double value) {
    if (!m_opened) {
        return false;
    }

    switch (propId) {
        case CAP_PROP_POS_MSEC: {
            double timeInSeconds = value / 1000.0;
            if (m_demuxer->SeekToTime(timeInSeconds)) {
                m_decoder->Flush();
                m_eof = false;
                return true;
            }
            return false;
        }

        case CAP_PROP_POS_FRAMES: {
            int64_t frameNumber = static_cast<int64_t>(value);
            if (m_demuxer->SeekToFrame(frameNumber)) {
                m_decoder->Flush();
                m_eof = false;
                return true;
            }
            return false;
        }

        case CAP_PROP_POS_AVI_RATIO: {
            double duration = m_demuxer->GetDuration();
            if (duration > 0.0) {
                double timeInSeconds = value * duration;
                if (m_demuxer->SeekToTime(timeInSeconds)) {
                    m_decoder->Flush();
                    m_eof = false;
                    return true;
                }
            }
            return false;
        }

        default:
            LOG_WARNING("Unsupported property ID for set: ", propId);
            return false;
    }
}

bool VideoCapture::isOpened() const {
    return m_opened;
}

void VideoCapture::release() {
    m_currentFrame.reset();
    m_decoder.reset();
    m_demuxer.reset();
    m_opened = false;
    m_eof = false;
    m_frameCount = 0;
}

bool VideoCapture::InitializeDecoder() {
    // Get decoder info
    DecoderInfo decoderInfo = HardwareDecoder::GetBestDecoder(m_demuxer->GetCodecID());
    if (decoderInfo.type != DecoderType::NVDEC || !decoderInfo.available) {
        LOG_ERROR("Hardware decoder not available - only hardware decoding is supported");
        return false;
    }

    // Create decoder
    m_decoder = std::make_unique<VideoDecoder>();
    if (!m_decoder->Initialize(m_demuxer->GetCodecParameters(), decoderInfo, s_d3dDevice, m_demuxer->GetTimeBase())) {
        LOG_ERROR("Failed to initialize video decoder");
        return false;
    }

    // Create frame holder
    m_currentFrame = std::make_unique<DecodedFrame>();

    return true;
}

bool VideoCapture::DecodeNextFrame() {
    if (!m_decoder || !m_demuxer) {
        return false;
    }

    // Try to decode a frame (may need multiple packets)
    const int MAX_ATTEMPTS = 100; // Prevent infinite loops
    int attempts = 0;

    while (attempts++ < MAX_ATTEMPTS) {
        // Try to receive a frame first (decoder may have buffered frames)
        if (m_decoder->ReceiveFrame(*m_currentFrame)) {
            if (m_currentFrame->valid) {
                return true; // Successfully decoded a frame
            }
        }

        // Need more data, read a packet
        AVPacket packet;
        if (!m_demuxer->ReadFrame(&packet)) {
            // End of file or error
            // Flush decoder to get remaining frames
            m_decoder->SendPacket(nullptr);
            if (m_decoder->ReceiveFrame(*m_currentFrame) && m_currentFrame->valid) {
                return true;
            }
            return false;
        }

        // Send packet to decoder
        if (!m_decoder->SendPacket(&packet)) {
            av_packet_unref(&packet);
            return false;
        }

        av_packet_unref(&packet);
    }

    LOG_ERROR("Failed to decode frame after ", MAX_ATTEMPTS, " attempts");
    return false;
}