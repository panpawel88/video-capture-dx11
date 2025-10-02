#pragma once

#ifdef WEBRTC_SUPPORT_ENABLED

#include "IDataSource.h"
#include "BufferDataSource.h"
#include <rtc/rtc.hpp>
#include <memory>
#include <string>
#include <functional>
#include <atomic>

/**
 * WebRTC-based data source for receiving video streams.
 * Uses libdatachannel to receive H264/H265 RTP packets and depacketize them into NAL units.
 * The NAL units are written to an internal buffer that AVIOContext can read from.
 */
class WebRTCDataSource : public IDataSource {
public:
    using SignalingCallback = std::function<void(const std::string& type, const std::string& sdp)>;
    using StateChangeCallback = std::function<void(rtc::PeerConnection::State)>;

    WebRTCDataSource();
    ~WebRTCDataSource() override;

    // IDataSource interface
    int Read(uint8_t* buffer, int size) override;
    int64_t Seek(int64_t offset, int whence) override;
    int64_t GetSize() const override;
    bool IsSeekable() const override;

    // WebRTC setup
    void SetSignalingCallback(SignalingCallback callback);
    void SetStateChangeCallback(StateChangeCallback callback);

    // Initialize WebRTC peer connection
    bool Initialize(const std::string& codec = "H264", int payloadType = 96);

    // Set remote SDP (answer from browser/peer)
    bool SetRemoteDescription(const std::string& sdp, const std::string& type);

    // Get local SDP (offer to send to browser/peer)
    std::string GetLocalDescription() const;
    std::string GetLocalDescriptionType() const;

    // Connection state
    bool IsConnected() const;
    bool IsDataAvailable() const;

    // Control
    void Close();

    // Get the container format hint for demuxer (e.g., "h264", "hevc")
    std::string GetFormatHint() const;

private:
    std::shared_ptr<rtc::PeerConnection> m_peerConnection;
    std::shared_ptr<rtc::Track> m_track;
    std::unique_ptr<BufferDataSource> m_buffer;

    SignalingCallback m_signalingCallback;
    StateChangeCallback m_stateCallback;

    std::string m_codec;
    int m_payloadType;
    std::atomic<bool> m_connected;
    std::atomic<bool> m_initialized;

    mutable std::string m_localSdp;
    mutable std::string m_localType;

    void OnTrackMessage(rtc::binary message);
    void OnStateChange(rtc::PeerConnection::State state);
    void OnGatheringStateChange(rtc::PeerConnection::GatheringState state);
};

#endif // WEBRTC_SUPPORT_ENABLED
