#pragma once

#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
}

// Forward declaration
struct ID3D11Device;

enum class DecoderType {
    NONE,
    NVDEC,
    D3D11VA
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
    static bool Initialize(ID3D11Device* d3dDevice = nullptr);
    static void Cleanup();
    static std::vector<DecoderInfo> GetAvailableDecoders();
    static DecoderInfo GetBestDecoder(AVCodecID codecId);
    static bool SupportsCodec(const DecoderInfo& decoder, AVCodecID codecId);

private:
    static bool s_initialized;
    static std::vector<DecoderInfo> s_availableDecoders;

    static void DetectHardwareDecoders(ID3D11Device* d3dDevice);
    static bool TestNVDECAvailability();
    static bool TestD3D11VAAvailability(ID3D11Device* d3dDevice);
};