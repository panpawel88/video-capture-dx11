#include "HardwareDecoder.h"
#include "Logger.h"
#include <iostream>

extern "C" {
#include <libavutil/hwcontext.h>
#include <libavcodec/avcodec.h>
}

bool HardwareDecoder::s_initialized = false;
std::vector<DecoderInfo> HardwareDecoder::s_availableDecoders;

bool HardwareDecoder::Initialize() {
    if (s_initialized) {
        return true;
    }

    LOG_INFO("Initializing hardware decoder detection...");

    DetectHardwareDecoders();

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

    // Return NVDEC for H264/H265 if available
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
        case DecoderType::NVDEC:
            return (codecId == AV_CODEC_ID_H264 || codecId == AV_CODEC_ID_HEVC || codecId == AV_CODEC_ID_AV1);
        default:
            return false;
    }
}

void HardwareDecoder::DetectHardwareDecoders() {
    s_availableDecoders.clear();

    // Test NVDEC availability
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
    bool av1Available = false;

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

    // Check for AV1 NVDEC decoder (requires RTX 30 series or newer)
    const AVCodec* av1Decoder = avcodec_find_decoder_by_name("av1_cuvid");
    if (av1Decoder) {
        av1Available = true;
        LOG_INFO("AV1 NVDEC decoder found");
    }

    av_buffer_unref(&hwDeviceCtx);

    if (h264Available || h265Available || av1Available) {
        LOG_INFO("NVDEC hardware decoding available");
        return true;
    } else {
        LOG_INFO("NVDEC hardware decoders not found");
        return false;
    }
}