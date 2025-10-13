#ifdef WEBRTC_SUPPORT_ENABLED

#include "WebRTCDataSource.h"
#include "Logger.h"
#include <rtc/h264rtpdepacketizer.hpp>

WebRTCDataSource::WebRTCDataSource()
    : m_buffer(std::make_unique<BufferDataSource>())
    , m_codec("H264")
    , m_payloadType(96)
    , m_connected(false)
    , m_initialized(false)
{
    // Configure buffer for streaming (non-seekable)
    m_buffer->SetSeekable(false);
}

WebRTCDataSource::~WebRTCDataSource() {
    Close();
}

int WebRTCDataSource::Read(uint8_t* buffer, int size) {
    return m_buffer->Read(buffer, size);
}

int64_t WebRTCDataSource::Seek(int64_t offset, int whence) {
    return m_buffer->Seek(offset, whence);
}

int64_t WebRTCDataSource::GetSize() const {
    return m_buffer->GetSize();
}

bool WebRTCDataSource::IsSeekable() const {
    return false; // WebRTC streams are not seekable
}

void WebRTCDataSource::SetSignalingCallback(SignalingCallback callback) {
    m_signalingCallback = std::move(callback);
}

void WebRTCDataSource::SetStateChangeCallback(StateChangeCallback callback) {
    m_stateCallback = std::move(callback);
}

bool WebRTCDataSource::Initialize(const std::string& codec, int payloadType) {
    if (m_initialized) {
        LOG_WARNING("WebRTCDataSource already initialized");
        return true;
    }

    m_codec = codec;
    m_payloadType = payloadType;

    try {
        // Initialize libdatachannel logger
        rtc::InitLogger(rtc::LogLevel::Warning);

        // Create peer connection
        rtc::Configuration config;
        // Add STUN server for NAT traversal
        config.iceServers.emplace_back("stun:stun.l.google.com:19302");

        m_peerConnection = std::make_shared<rtc::PeerConnection>(config);

        // Set up state change callbacks
        m_peerConnection->onStateChange([this](rtc::PeerConnection::State state) {
            OnStateChange(state);
        });

        m_peerConnection->onGatheringStateChange([this](rtc::PeerConnection::GatheringState state) {
            OnGatheringStateChange(state);
        });

        // Create receive-only video track
        rtc::Description::Video media("video", rtc::Description::Direction::RecvOnly);

        if (codec == "H264") {
            media.addH264Codec(payloadType);
        } else if (codec == "H265" || codec == "HEVC") {
            media.addH265Codec(payloadType);
        } else if (codec == "AV1") {
            // AV1 support - requires libdatachannel with AV1 codec support
            media.addVideoCodec(payloadType, "AV1");
        } else {
            LOG_ERROR("Unsupported codec: ", codec);
            return false;
        }

        media.setBitrate(5000); // Request 5 Mbps

        m_track = m_peerConnection->addTrack(media);

        // Set up RTP depacketizer
        std::shared_ptr<rtc::MediaHandler> depacketizer;
        if (codec == "H264") {
            // Use StartSequence separator (00 00 01 or 00 00 00 01)
            depacketizer = std::make_shared<rtc::H264RtpDepacketizer>();
        } else if (codec == "H265" || codec == "HEVC") {
            depacketizer = std::make_shared<rtc::H265RtpDepacketizer>();
        } else if (codec == "AV1") {
            // TODO: AV1 RTP depacketizer support depends on libdatachannel version
            // Check if rtc::AV1RtpDepacketizer is available in your libdatachannel version
            LOG_WARNING("AV1 RTP depacketizer may not be available in libdatachannel yet");
            // depacketizer = std::make_shared<rtc::AV1RtpDepacketizer>();  // Uncomment if available
        }

        m_track->setMediaHandler(depacketizer);

        // Set up message callback to receive depacketized NAL units
        m_track->onMessage(
            [this](rtc::binary message) {
                OnTrackMessage(std::move(message));
            },
            nullptr // onBufferedAmountLow callback
        );

        // Create local offer
        m_peerConnection->setLocalDescription();

        m_initialized = true;
        LOG_INFO("WebRTCDataSource initialized with codec: ", codec);
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize WebRTCDataSource: ", e.what());
        return false;
    }
}

bool WebRTCDataSource::SetRemoteDescription(const std::string& sdp, const std::string& type) {
    if (!m_initialized) {
        LOG_ERROR("WebRTCDataSource not initialized");
        return false;
    }

    try {
        rtc::Description answer(sdp, type);
        m_peerConnection->setRemoteDescription(answer);
        LOG_INFO("Remote description set successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to set remote description: ", e.what());
        return false;
    }
}

std::string WebRTCDataSource::GetLocalDescription() const {
    if (m_peerConnection && m_peerConnection->localDescription()) {
        return std::string(m_peerConnection->localDescription().value());
    }
    return "";
}

std::string WebRTCDataSource::GetLocalDescriptionType() const {
    if (m_peerConnection && m_peerConnection->localDescription()) {
        return m_peerConnection->localDescription()->typeString();
    }
    return "";
}

bool WebRTCDataSource::IsConnected() const {
    return m_connected;
}

bool WebRTCDataSource::IsDataAvailable() const {
    return m_buffer->GetBytesAvailable() > 0;
}

void WebRTCDataSource::Close() {
    if (m_track) {
        m_track->close();
        m_track.reset();
    }

    if (m_peerConnection) {
        m_peerConnection->close();
        m_peerConnection.reset();
    }

    m_buffer->Clear();
    m_connected = false;
    m_initialized = false;

    LOG_INFO("WebRTCDataSource closed");
}

std::string WebRTCDataSource::GetFormatHint() const {
    if (m_codec == "H264") {
        return "h264";
    } else if (m_codec == "H265" || m_codec == "HEVC") {
        return "hevc";
    }
    return "";
}

void WebRTCDataSource::OnTrackMessage(rtc::binary message) {
    // Received depacketized NAL units (with start codes already added by depacketizer)
    if (!message.empty()) {
        LOG_DEBUG("Received NAL unit: ", message.size(), " bytes");

        // Append to buffer for AVIOContext to read
        m_buffer->AppendData(reinterpret_cast<const uint8_t*>(message.data()), message.size());
    }
}

void WebRTCDataSource::OnStateChange(rtc::PeerConnection::State state) {
    LOG_INFO("WebRTC connection state: ", static_cast<int>(state));

    switch (state) {
        case rtc::PeerConnection::State::Connected:
            m_connected = true;
            LOG_INFO("WebRTC connection established");
            break;

        case rtc::PeerConnection::State::Disconnected:
        case rtc::PeerConnection::State::Failed:
        case rtc::PeerConnection::State::Closed:
            m_connected = false;
            if (state == rtc::PeerConnection::State::Failed) {
                LOG_ERROR("WebRTC connection failed");
            } else {
                LOG_INFO("WebRTC connection closed");
            }
            // Signal EOF when connection closes
            m_buffer->SetEOF(true);
            break;

        default:
            break;
    }

    if (m_stateCallback) {
        m_stateCallback(state);
    }
}

void WebRTCDataSource::OnGatheringStateChange(rtc::PeerConnection::GatheringState state) {
    LOG_DEBUG("ICE gathering state: ", static_cast<int>(state));

    if (state == rtc::PeerConnection::GatheringState::Complete) {
        // ICE gathering complete, local description is ready
        if (m_signalingCallback) {
            auto description = m_peerConnection->localDescription();
            if (description) {
                m_signalingCallback(description->typeString(), std::string(description.value()));
            }
        }
    }
}

#endif // WEBRTC_SUPPORT_ENABLED
