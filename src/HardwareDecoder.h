#pragma once

#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
}

enum class DecoderType {
    NONE,
    NVDEC
};

struct DecoderInfo {
    DecoderType type;
    std::string name;
    AVHWDeviceType hwDeviceType;
    bool available;

    DecoderInfo() : type(DecoderType::NONE), hwDeviceType(AV_HWDEVICE_TYPE_NONE), available(false) {}
};

class HardwareDecoder {
public:
    static bool Initialize();
    static void Cleanup();
    static std::vector<DecoderInfo> GetAvailableDecoders();
    static DecoderInfo GetBestDecoder(AVCodecID codecId);
    static bool SupportsCodec(const DecoderInfo& decoder, AVCodecID codecId);

private:
    static bool s_initialized;
    static std::vector<DecoderInfo> s_availableDecoders;

    static void DetectHardwareDecoders();
    static bool TestNVDECAvailability();
};