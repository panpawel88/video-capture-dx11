#include "FFmpegInitializer.h"
#include "HardwareDecoder.h"

FFmpegInitializer::~FFmpegInitializer() {
    if (initialized) {
        HardwareDecoder::Cleanup();
    }
}

bool FFmpegInitializer::Initialize(ID3D11Device* d3dDevice) {
    if (!HardwareDecoder::Initialize(d3dDevice)) {
        return false;
    }

    initialized = true;
    return true;
}