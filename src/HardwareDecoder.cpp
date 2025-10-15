#include "HardwareDecoder.h"
#include "Logger.h"
#include <iostream>
#include <d3d11.h>

extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavcodec/avcodec.h>
}

bool HardwareDecoder::s_initialized = false;
std::vector<DecoderInfo> HardwareDecoder::s_availableDecoders;

bool HardwareDecoder::Initialize(ID3D11Device* d3dDevice) {
    if (s_initialized) {
        return true;
    }

    LOG_INFO("Initializing hardware decoder detection...");

    DetectHardwareDecoders(d3dDevice);

    LOG_INFO("Available decoders:");
    for (const auto& decoder : s_availableDecoders) {
        LOG_INFO("  - ", decoder.name, " (", (decoder.available ? "Available" : "Unavailable"), ")");
    }

    s_initialized = true;
    return true;
}

void HardwareDecoder::Cleanup() {
    if (s_initialized) {
        s_availableDecoders.clear();
        s_initialized = false;
    }
}

std::vector<DecoderInfo> HardwareDecoder::GetAvailableDecoders() {
    return s_availableDecoders;
}

DecoderInfo HardwareDecoder::GetBestDecoder(AVCodecID codecId) {
    if (!s_initialized) {
        DecoderInfo noneDecoder;
        noneDecoder.type = DecoderType::NONE;
        noneDecoder.name = "None";
        noneDecoder.available = false;
        return noneDecoder;
    }

    // Prefer D3D11VA (native Windows API, works with all vendors)
    for (const auto& decoder : s_availableDecoders) {
        if (decoder.available && decoder.type == DecoderType::D3D11VA &&
            SupportsCodec(decoder, codecId)) {
            return decoder;
        }
    }

    // Fallback to NVDEC if D3D11VA not available
    for (const auto& decoder : s_availableDecoders) {
        if (decoder.available && decoder.type == DecoderType::NVDEC &&
            SupportsCodec(decoder, codecId)) {
            return decoder;
        }
    }

    // No hardware decoder available
    DecoderInfo noneDecoder;
    noneDecoder.type = DecoderType::NONE;
    noneDecoder.name = "None";
    noneDecoder.available = false;
    return noneDecoder;
}

bool HardwareDecoder::SupportsCodec(const DecoderInfo& decoder, AVCodecID codecId) {
    switch (decoder.type) {
        case DecoderType::D3D11VA:
            // D3D11VA supports H.264 and H.265 (AV1 added in next commit)
            return (codecId == AV_CODEC_ID_H264 || codecId == AV_CODEC_ID_HEVC);
        case DecoderType::NVDEC:
            return (codecId == AV_CODEC_ID_H264 || codecId == AV_CODEC_ID_HEVC);
        default:
            return false;
    }
}

void HardwareDecoder::DetectHardwareDecoders(ID3D11Device* d3dDevice) {
    s_availableDecoders.clear();

    // Test D3D11VA availability (preferred for Windows)
    if (d3dDevice) {
        DecoderInfo d3d11vaDecoder;
        d3d11vaDecoder.type = DecoderType::D3D11VA;
        d3d11vaDecoder.name = "D3D11VA Hardware Decoder";
        d3d11vaDecoder.hwDeviceType = AV_HWDEVICE_TYPE_D3D11VA;
        d3d11vaDecoder.available = TestD3D11VAAvailability(d3dDevice);
        s_availableDecoders.push_back(d3d11vaDecoder);
    }

    // Test NVDEC availability (fallback for NVIDIA GPUs)
    DecoderInfo nvdecDecoder;
    nvdecDecoder.type = DecoderType::NVDEC;
    nvdecDecoder.name = "NVIDIA NVDEC";
    nvdecDecoder.hwDeviceType = AV_HWDEVICE_TYPE_CUDA;
    nvdecDecoder.available = TestNVDECAvailability();
    s_availableDecoders.push_back(nvdecDecoder);
}

bool HardwareDecoder::TestNVDECAvailability() {
    AVBufferRef* hwDeviceCtx = nullptr;

    // Try to create CUDA hardware device context
    int ret = av_hwdevice_ctx_create(&hwDeviceCtx, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0);
    if (ret < 0) {
        char errorBuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errorBuf, sizeof(errorBuf));
        LOG_INFO("NVDEC not available: Failed to create CUDA device context: ", errorBuf);
        return false;
    }

    // Test if we can find NVDEC decoders
    bool h264Available = false;
    bool h265Available = false;

    // Check for H264 NVDEC decoder
    const AVCodec* h264Decoder = avcodec_find_decoder_by_name("h264_cuvid");
    if (h264Decoder) {
        h264Available = true;
        LOG_INFO("H264 NVDEC decoder found");
    }

    // Check for H265 NVDEC decoder
    const AVCodec* h265Decoder = avcodec_find_decoder_by_name("hevc_cuvid");
    if (h265Decoder) {
        h265Available = true;
        LOG_INFO("H265 NVDEC decoder found");
    }

    av_buffer_unref(&hwDeviceCtx);

    if (h264Available || h265Available) {
        LOG_INFO("NVDEC hardware decoding available");
        return true;
    } else {
        LOG_INFO("NVDEC hardware decoders not found");
        return false;
    }
}

bool HardwareDecoder::TestD3D11VAAvailability(ID3D11Device* d3dDevice) {
    if (!d3dDevice) {
        LOG_INFO("D3D11VA not available: No D3D11 device provided");
        return false;
    }

    AVBufferRef* hwDeviceCtx = nullptr;
    AVHWDeviceContext* deviceContext = nullptr;
    AVD3D11VADeviceContext* d3d11vaContext = nullptr;

    // Allocate D3D11VA device context
    hwDeviceCtx = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
    if (!hwDeviceCtx) {
        LOG_INFO("D3D11VA not available: Failed to allocate device context");
        return false;
    }

    deviceContext = reinterpret_cast<AVHWDeviceContext*>(hwDeviceCtx->data);
    d3d11vaContext = reinterpret_cast<AVD3D11VADeviceContext*>(deviceContext->hwctx);

    // Use the provided D3D11 device
    d3d11vaContext->device = d3dDevice;
    d3d11vaContext->device->AddRef();

    ID3D11DeviceContext* deviceCtx = nullptr;
    d3dDevice->GetImmediateContext(&deviceCtx);
    d3d11vaContext->device_context = deviceCtx;

    // Initialize the hardware device context
    int ret = av_hwdevice_ctx_init(hwDeviceCtx);
    if (ret < 0) {
        char errorBuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errorBuf, sizeof(errorBuf));
        LOG_INFO("D3D11VA not available: Failed to initialize device context: ", errorBuf);
        av_buffer_unref(&hwDeviceCtx);
        if (deviceCtx) deviceCtx->Release();
        return false;
    }

    // Test if standard decoders support D3D11VA
    const AVCodec* h264Decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
    const AVCodec* h265Decoder = avcodec_find_decoder(AV_CODEC_ID_HEVC);

    bool h264Available = false;
    bool h265Available = false;

    // Check if H.264 decoder supports D3D11VA
    if (h264Decoder) {
        for (int i = 0;; i++) {
            const AVCodecHWConfig* config = avcodec_get_hw_config(h264Decoder, i);
            if (!config) break;
            if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                config->device_type == AV_HWDEVICE_TYPE_D3D11VA) {
                h264Available = true;
                LOG_INFO("H264 D3D11VA decoder available");
                break;
            }
        }
    }

    // Check if H.265 decoder supports D3D11VA
    if (h265Decoder) {
        for (int i = 0;; i++) {
            const AVCodecHWConfig* config = avcodec_get_hw_config(h265Decoder, i);
            if (!config) break;
            if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                config->device_type == AV_HWDEVICE_TYPE_D3D11VA) {
                h265Available = true;
                LOG_INFO("H265 D3D11VA decoder available");
                break;
            }
        }
    }

    av_buffer_unref(&hwDeviceCtx);
    if (deviceCtx) deviceCtx->Release();

    if (h264Available || h265Available) {
        LOG_INFO("D3D11VA hardware decoding available");
        return true;
    } else {
        LOG_INFO("D3D11VA hardware decoders not found");
        return false;
    }
}