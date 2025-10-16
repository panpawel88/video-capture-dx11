#include "VideoDecoder.h"
#include "Logger.h"
#include <iostream>
#include <iomanip>

extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/hwcontext_d3d11va.h>
}

VideoDecoder::VideoDecoder()
    : m_initialized(false)
    , m_useHardwareDecoding(false)
    , m_codec(nullptr)
    , m_codecContext(nullptr)
    , m_hwDeviceContext(nullptr)
    , m_frame(nullptr)
{
}

VideoDecoder::~VideoDecoder() {
    Cleanup();
}

bool VideoDecoder::Initialize(AVCodecParameters* codecParams, const DecoderInfo& decoderInfo, ID3D11Device* d3dDevice, AVRational streamTimebase) {
    if (m_initialized) {
        Cleanup();
    }

    if (!codecParams) {
        LOG_ERROR("Invalid codec parameters for VideoDecoder initialization");
        return false;
    }

    if (!d3dDevice) {
        LOG_ERROR("D3D11 device is required for hardware decoding");
        return false;
    }

    if (decoderInfo.type != DecoderType::D3D11VA || !decoderInfo.available) {
        LOG_ERROR("Hardware decoder not available - only hardware decoding is supported");
        return false;
    }

    m_d3dDevice = d3dDevice;
    m_d3dDevice->GetImmediateContext(&m_d3dContext);
    m_decoderInfo = decoderInfo;
    m_streamTimebase = streamTimebase;

    LOG_INFO("Initializing hardware video decoder with ", decoderInfo.name);

    // Allocate frame
    m_frame = av_frame_alloc();
    if (!m_frame) {
        LOG_ERROR("Failed to allocate AVFrame structure");
        Cleanup();
        return false;
    }

    // Initialize hardware decoder
    if (!InitializeHardwareDecoder(codecParams)) {
        LOG_ERROR("Failed to initialize hardware decoder");
        Cleanup();
        return false;
    }

    m_useHardwareDecoding = true;
    m_initialized = true;
    LOG_INFO("Hardware video decoder initialized successfully");
    return true;
}

void VideoDecoder::Cleanup() {
    Reset();
}

bool VideoDecoder::SendPacket(AVPacket* packet) {
    if (!m_initialized || !m_codecContext) {
        LOG_DEBUG("SendPacket failed - decoder not initialized or no codec context");
        return false;
    }

    LOG_DEBUG("Sending packet to decoder - Size: ", (packet ? packet->size : 0),
              ", PTS: ", (packet && packet->pts != AV_NOPTS_VALUE ? packet->pts : -1),
              ", DTS: ", (packet && packet->dts != AV_NOPTS_VALUE ? packet->dts : -1));

    int ret = avcodec_send_packet(m_codecContext, packet);
    if (ret < 0) {
        if (ret == AVERROR_EOF) {
            LOG_DEBUG("Decoder reached end of stream");
            return true; // End of stream
        }
        char errorBuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errorBuf, sizeof(errorBuf));
        LOG_DEBUG("Error sending packet to decoder: ", errorBuf, " (ret=", ret, ")");
        return false;
    }

    LOG_DEBUG("Packet sent to decoder successfully");
    return true;
}

bool VideoDecoder::ReceiveFrame(DecodedFrame& frame) {
    if (!m_initialized || !m_codecContext) {
        LOG_DEBUG("ReceiveFrame failed - decoder not initialized or no codec context");
        return false;
    }

    frame.valid = false;

    int ret = avcodec_receive_frame(m_codecContext, m_frame);
    if (ret < 0) {
        if (ret == AVERROR(EAGAIN)) {
            LOG_DEBUG("No frame available yet (EAGAIN)");
            return true; // No frame available yet
        } else if (ret == AVERROR_EOF) {
            LOG_DEBUG("End of stream reached (EOF)");
            return true; // End of stream
        }
        char errorBuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errorBuf, sizeof(errorBuf));
        LOG_DEBUG("Error receiving frame from decoder: ", errorBuf, " (ret=", ret, ")");
        return false;
    }

    LOG_DEBUG("Received frame from decoder - Size: ", m_frame->width, "x", m_frame->height,
              ", Format: ", m_frame->format, ", PTS: ", m_frame->pts,
              ", Codec Timebase: ", m_codecContext->time_base.num, "/", m_codecContext->time_base.den,
              ", Stream Timebase: ", m_streamTimebase.num, "/", m_streamTimebase.den);

    // Process hardware frame
    LOG_DEBUG("Processing hardware frame");
    bool success = ProcessHardwareFrame(frame);

    if (success) {
        // Set presentation time using stream timebase
        if (m_frame->pts != AV_NOPTS_VALUE) {
            if (m_streamTimebase.den != 0) {
                frame.presentationTime = static_cast<double>(m_frame->pts) * av_q2d(m_streamTimebase);
                LOG_DEBUG("Frame presentation time (using stream timebase): ", frame.presentationTime, " seconds");
            } else {
                // Fallback to codec timebase if stream timebase is invalid
                frame.presentationTime = static_cast<double>(m_frame->pts) * av_q2d(m_codecContext->time_base);
                LOG_DEBUG("Frame presentation time (using codec timebase): ", frame.presentationTime, " seconds");
            }
        } else {
            LOG_DEBUG("Frame has no PTS (AV_NOPTS_VALUE)");
        }

        // Set keyframe flag based on FFmpeg's frame information
        frame.keyframe = (m_frame->flags & AV_FRAME_FLAG_KEY) || (m_frame->pict_type == AV_PICTURE_TYPE_I);
        if (frame.keyframe) {
            LOG_DEBUG("Frame is a keyframe (I-frame) at time: ", frame.presentationTime);
        }

        frame.valid = true;
        LOG_DEBUG("Frame processed successfully");
    } else {
        LOG_DEBUG("Failed to process frame");
    }

    return success;
}

void VideoDecoder::Flush() {
    if (m_codecContext) {
        avcodec_flush_buffers(m_codecContext);
    }
}

bool VideoDecoder::InitializeHardwareDecoder(AVCodecParameters* codecParams) {
    // Find appropriate hardware decoder
    m_codec = avcodec_find_decoder(codecParams->codec_id);
    if (!m_codec) {
        LOG_ERROR("Decoder not found for codec");
        return false;
    }

    m_codecContext = avcodec_alloc_context3(m_codec);
    if (!m_codecContext) {
        LOG_ERROR("Failed to allocate codec context");
        return false;
    }

    // Copy codec parameters
    int ret = avcodec_parameters_to_context(m_codecContext, codecParams);
    if (ret < 0) {
        char errorBuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errorBuf, sizeof(errorBuf));
        LOG_ERROR("Failed to copy codec parameters: ", errorBuf);
        return false;
    }

    // Create hardware device context
    if (!CreateHardwareDeviceContext()) {
        return false;
    }

    // Setup hardware decoding
    if (!SetupHardwareDecoding()) {
        return false;
    }

    // Set get_format callback to force hardware pixel format (critical!)
    m_codecContext->get_format = GetHardwareFormat;

    // Open codec
    ret = avcodec_open2(m_codecContext, m_codec, nullptr);
    if (ret < 0) {
        char errorBuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errorBuf, sizeof(errorBuf));
        LOG_ERROR("Failed to open hardware codec: ", errorBuf);
        return false;
    }

    return true;
}

bool VideoDecoder::CreateHardwareDeviceContext() {
    // Create D3D11VA device context using the existing D3D11 device
    AVHWDeviceContext* deviceContext;
    AVD3D11VADeviceContext* d3d11vaContext;

    m_hwDeviceContext = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
    if (!m_hwDeviceContext) {
        LOG_ERROR("Failed to allocate D3D11VA device context");
        return false;
    }

    deviceContext = reinterpret_cast<AVHWDeviceContext*>(m_hwDeviceContext->data);
    d3d11vaContext = reinterpret_cast<AVD3D11VADeviceContext*>(deviceContext->hwctx);

    // Use our existing D3D11 device
    d3d11vaContext->device = m_d3dDevice.Get();
    d3d11vaContext->device->AddRef(); // AddRef since FFmpeg will release it
    d3d11vaContext->device_context = m_d3dContext.Get();
    d3d11vaContext->device_context->AddRef();

    int ret = av_hwdevice_ctx_init(m_hwDeviceContext);
    if (ret < 0) {
        char errorBuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errorBuf, sizeof(errorBuf));
        LOG_ERROR("Failed to initialize D3D11VA device context: ", errorBuf);
        return false;
    }

    return true;
}

bool VideoDecoder::SetupHardwareDecoding() {
    if (!m_codecContext || !m_hwDeviceContext) {
        return false;
    }

    m_codecContext->hw_device_ctx = av_buffer_ref(m_hwDeviceContext);
    return true;
}

bool VideoDecoder::ProcessHardwareFrame(DecodedFrame& outFrame) {
    if (!IsHardwareFrame(m_frame)) {
        LOG_ERROR("Expected hardware frame but got software frame");
        return false;
    }

    // Extract D3D11 texture from hardware frame
    if (!ExtractD3D11Texture(m_frame, outFrame.texture)) {
        return false;
    }

    // Get actual format from the extracted texture
    if (outFrame.texture) {
        D3D11_TEXTURE2D_DESC desc;
        outFrame.texture->GetDesc(&desc);
        outFrame.format = desc.Format;
        LOG_DEBUG("Hardware frame format: ", desc.Format);

        // Set YUV flag based on actual format
        if (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8X8_UNORM) {
            // RGB format
            outFrame.isYUV = false;
            LOG_DEBUG("Hardware texture is RGB format: ", desc.Format);
        } else {
            // YUV format (NV12, P010, 420_OPAQUE, etc.)
            outFrame.isYUV = true;
            LOG_DEBUG("Hardware texture is YUV format: ", desc.Format, ", enabling YUV processing");
        }
    } else {
        // Fallback
        outFrame.isYUV = false;
        outFrame.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    }

    // Set the actual video dimensions from FFmpeg frame (not texture dimensions which may include padding)
    outFrame.width = m_frame->width;
    outFrame.height = m_frame->height;
    LOG_DEBUG("D3D11 hardware frame processed - Video dimensions: ", outFrame.width, "x", outFrame.height);

    return true;
}

bool VideoDecoder::IsHardwareFrame(AVFrame* frame) const {
    if (!frame) {
        return false;
    }

    // Check if the frame format is a hardware pixel format
    return frame->format == AV_PIX_FMT_D3D11 ||
           frame->format == AV_PIX_FMT_DXVA2_VLD ||
           frame->hw_frames_ctx != nullptr;
}

bool VideoDecoder::ExtractD3D11Texture(AVFrame* frame, ComPtr<ID3D11Texture2D>& texture) {
    if (!frame || frame->format != AV_PIX_FMT_D3D11) {
        LOG_DEBUG("Frame is not D3D11 format or is null");
        return false;
    }

    // Extract D3D11 texture directly from the hardware frame
    // For D3D11 frames, data[0] contains the ID3D11Texture2D pointer
    // and data[1] contains the texture array index
    ID3D11Texture2D* hwTexture = reinterpret_cast<ID3D11Texture2D*>(frame->data[0]);
    if (!hwTexture) {
        LOG_DEBUG("No D3D11 texture found in hardware frame");
        return false;
    }

    // Get texture description to understand format and properties
    D3D11_TEXTURE2D_DESC desc;
    hwTexture->GetDesc(&desc);

    LOG_DEBUG("Hardware texture extracted - Size: ", desc.Width, "x", desc.Height,
              ", Format: ", desc.Format, ", ArraySize: ", desc.ArraySize);

    // Update outFrame format information based on actual hardware texture format
    switch (desc.Format) {
        case DXGI_FORMAT_NV12:
            LOG_DEBUG("Hardware texture is NV12 format (87)");
            break;
        case DXGI_FORMAT_P010:
            LOG_DEBUG("Hardware texture is P010 format (104)");
            break;
        case DXGI_FORMAT_420_OPAQUE:
            LOG_DEBUG("Hardware texture is 420_OPAQUE format (189)");
            break;
        case DXGI_FORMAT_B8G8R8A8_UNORM:
            LOG_DEBUG("Hardware texture is B8G8R8A8_UNORM format (87)");
            break;
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            LOG_DEBUG("Hardware texture is R8G8B8A8_UNORM format (28)");
            break;
        default:
            LOG_DEBUG("Hardware texture format: ", desc.Format, " (unknown)");
            break;
    }

    // If this is a texture array (common with hardware decode), we need to create a view of the specific slice
    int arrayIndex = reinterpret_cast<intptr_t>(frame->data[1]);
    if (desc.ArraySize > 1) {
        // Create a new texture as a copy of the specific array slice
        D3D11_TEXTURE2D_DESC newDesc = desc;
        newDesc.ArraySize = 1;
        newDesc.Usage = D3D11_USAGE_DEFAULT;
        newDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        newDesc.CPUAccessFlags = 0;
        newDesc.MiscFlags = 0;

        HRESULT hr = m_d3dDevice->CreateTexture2D(&newDesc, nullptr, &texture);
        if (FAILED(hr)) {
            LOG_DEBUG("Failed to create texture copy. HRESULT: 0x", std::hex, hr);
            return false;
        }

        // Copy the specific array slice to our new texture
        m_d3dContext->CopySubresourceRegion(
            texture.Get(), 0, 0, 0, 0,
            hwTexture, arrayIndex, nullptr
        );
    } else {
        // Single texture, use directly
        texture = hwTexture;
    }

    LOG_DEBUG("D3D11 texture extracted successfully from hardware frame");
    return true;
}

enum AVPixelFormat VideoDecoder::GetHardwareFormat(AVCodecContext* ctx, const enum AVPixelFormat* pix_fmts) {
    const enum AVPixelFormat* p;

    // Look for D3D11 format in available formats
    for (p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        if (*p == AV_PIX_FMT_D3D11) {
            LOG_DEBUG("Selecting D3D11 hardware pixel format");
            return *p;
        }
    }

    LOG_ERROR("Failed to find D3D11 pixel format in available formats");
    return AV_PIX_FMT_NONE;
}

void VideoDecoder::Reset() {
    m_initialized = false;
    m_useHardwareDecoding = false;

    if (m_codecContext) {
        avcodec_free_context(&m_codecContext);
    }

    if (m_hwDeviceContext) {
        av_buffer_unref(&m_hwDeviceContext);
    }

    if (m_frame) {
        av_frame_free(&m_frame);
    }

    m_codec = nullptr;
    m_d3dDevice.Reset();
    m_d3dContext.Reset();
}