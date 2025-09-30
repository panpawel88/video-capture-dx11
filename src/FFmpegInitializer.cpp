#include "FFmpegInitializer.h"
#include "HardwareDecoder.h"

FFmpegInitializer::~FFmpegInitializer() {
    if (initialized) {
        HardwareDecoder::Cleanup();
    }
}

bool FFmpegInitializer::Initialize() {
    if (!HardwareDecoder::Initialize()) {
        return false;
    }

    initialized = true;
    return true;
}