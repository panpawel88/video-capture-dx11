#pragma once

#include <string>
#include <memory>
#include <d3d11.h>
#include <dxgiformat.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// Forward declarations
class VideoDemuxer;
class VideoDecoder;
struct DecodedFrame;

// OpenCV-compatible property IDs
enum VideoCaptureProperties {
    CAP_PROP_POS_MSEC = 0,
    CAP_PROP_POS_FRAMES = 1,
    CAP_PROP_POS_AVI_RATIO = 2,
    CAP_PROP_FRAME_WIDTH = 3,
    CAP_PROP_FRAME_HEIGHT = 4,
    CAP_PROP_FPS = 5,
    CAP_PROP_FOURCC = 6,
    CAP_PROP_FRAME_COUNT = 7
};

class VideoCapture {
public:
    VideoCapture();
    ~VideoCapture();

    // Initialize with D3D11 device (required for hardware decoding)
    static bool Initialize(ID3D11Device* device);

    // Open video file (returns false if hardware decode not available)
    bool open(const std::string& filename);

    // Read next frame (returns DX11 texture, always YUV format from hardware)
    // Returns false if no more frames or error occurred
    bool read(ID3D11Texture2D** outTexture, bool& isYUV, DXGI_FORMAT& format);

    // Video properties (OpenCV-compatible)
    double get(int propId) const;

    // Seeking (OpenCV-compatible)
    bool set(int propId, double value);

    // Status
    bool isOpened() const;
    void release();

private:
    static ID3D11Device* s_d3dDevice;
    static bool s_initialized;

    std::unique_ptr<VideoDemuxer> m_demuxer;
    std::unique_ptr<VideoDecoder> m_decoder;
    std::unique_ptr<DecodedFrame> m_currentFrame;

    bool m_opened;
    bool m_eof;
    int64_t m_frameCount;

    bool InitializeDecoder();
    bool DecodeNextFrame();
};