#include "HardwareDecoder.h"
#include "Logger.h"
#include <iostream>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxva.h>
#include <wrl/client.h>
#include <iomanip>
#include <sstream>

extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavcodec/avcodec.h>
}

using Microsoft::WRL::ComPtr;

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

    // Use D3D11VA (native Windows API, works with all vendors)
    for (const auto& decoder : s_availableDecoders) {
        if (decoder.available && decoder.type == DecoderType::D3D11VA &&
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
            // D3D11VA supports H.264, H.265, and AV1 (hardware dependent)
            return (codecId == AV_CODEC_ID_H264 || codecId == AV_CODEC_ID_HEVC || codecId == AV_CODEC_ID_AV1);
        default:
            return false;
    }
}

void HardwareDecoder::DetectHardwareDecoders(ID3D11Device* d3dDevice) {
    s_availableDecoders.clear();

    // Test D3D11VA availability (native Windows API, works with all vendors)
    if (d3dDevice) {
        DecoderInfo d3d11vaDecoder;
        d3d11vaDecoder.type = DecoderType::D3D11VA;
        d3d11vaDecoder.name = "D3D11VA Hardware Decoder";
        d3d11vaDecoder.hwDeviceType = AV_HWDEVICE_TYPE_D3D11VA;
        d3d11vaDecoder.available = TestD3D11VAAvailability(d3dDevice);
        s_availableDecoders.push_back(d3d11vaDecoder);
    }
}

bool HardwareDecoder::QueryD3D11VideoDecoderGUIDs(ID3D11Device* d3dDevice) {
    if (!d3dDevice) {
        return false;
    }

    // Query ID3D11VideoDevice to enumerate decoder profiles
    ComPtr<ID3D11VideoDevice> videoDevice;
    HRESULT hr = d3dDevice->QueryInterface(__uuidof(ID3D11VideoDevice), &videoDevice);
    if (FAILED(hr)) {
        return false;
    }

    UINT profileCount = videoDevice->GetVideoDecoderProfileCount();

    // AV1 Decoder GUID from DXVA spec
    static const GUID D3D11_DECODER_PROFILE_AV1_VLD_PROFILE0 =
        {0xb8be4ccb, 0xcf53, 0x46ba, {0x8d, 0x59, 0xd6, 0xb8, 0xa6, 0xda, 0x5d, 0x2a}};

    // Known decoder GUIDs
    static const GUID D3D11_DECODER_PROFILE_H264_VLD_NOFGT =
        {0x1b81be68, 0xa0c7, 0x11d3, {0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5}};
    static const GUID D3D11_DECODER_PROFILE_HEVC_VLD_MAIN =
        {0x5b11d51b, 0x2f4c, 0x4452, {0xbc, 0xc3, 0x09, 0xf2, 0xa1, 0x16, 0x0c, 0xc0}};

    bool av1Found = false;
    bool h264Found = false;
    bool hevcFound = false;

    for (UINT i = 0; i < profileCount; i++) {
        GUID profileGuid;
        hr = videoDevice->GetVideoDecoderProfile(i, &profileGuid);
        if (SUCCEEDED(hr)) {
            if (profileGuid == D3D11_DECODER_PROFILE_AV1_VLD_PROFILE0) {
                av1Found = true;
            } else if (profileGuid == D3D11_DECODER_PROFILE_H264_VLD_NOFGT) {
                h264Found = true;
            } else if (profileGuid == D3D11_DECODER_PROFILE_HEVC_VLD_MAIN) {
                hevcFound = true;
            }
        }
    }

    LOG_INFO("D3D11 Video Decoder Hardware Support:");
    LOG_INFO("  H264: ", (h264Found ? "Yes" : "No"));
    LOG_INFO("  HEVC: ", (hevcFound ? "Yes" : "No"));
    LOG_INFO("  AV1:  ", (av1Found ? "Yes" : "No"));

    return av1Found;
}

bool HardwareDecoder::TestD3D11VAAvailability(ID3D11Device* d3dDevice) {
    if (!d3dDevice) {
        LOG_INFO("D3D11VA not available: No D3D11 device provided");
        return false;
    }

    // Query the actual D3D11 Video Device for decoder GUIDs
    QueryD3D11VideoDecoderGUIDs(d3dDevice);

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
    const AVCodec* av1Decoder = avcodec_find_decoder(AV_CODEC_ID_AV1);

    bool h264Available = false;
    bool h265Available = false;
    bool av1Available = false;

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

    // Check if AV1 decoder supports D3D11VA
    if (av1Decoder) {
        for (int i = 0;; i++) {
            const AVCodecHWConfig* config = avcodec_get_hw_config(av1Decoder, i);
            if (!config) break;
            if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                config->device_type == AV_HWDEVICE_TYPE_D3D11VA) {
                av1Available = true;
                LOG_INFO("AV1 D3D11VA decoder available");
                break;
            }
        }
    }

    av_buffer_unref(&hwDeviceCtx);
    if (deviceCtx) deviceCtx->Release();

    if (h264Available || h265Available || av1Available) {
        LOG_INFO("D3D11VA hardware decoding available");
        return true;
    } else {
        LOG_INFO("D3D11VA hardware decoders not found");
        return false;
    }
}