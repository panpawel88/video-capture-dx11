# WebRTC Streaming Example

This example demonstrates real-time video streaming from a web browser to a native Windows application using WebRTC.

## Requirements

- **libdatachannel**: Automatically downloaded via CMake FetchContent (no manual installation needed!)
- **Build with WebRTC support**:
  ```bash
  cmake -B build -DBUILD_WEBRTC_SUPPORT=ON
  cmake --build build --config Release
  ```

> **Note**: The build system will automatically download and compile libdatachannel v0.23.2 from GitHub. No vcpkg or manual dependency installation required!

## Architecture

```
Browser (Sender)          →  Native App (Receiver)
─────────────────────────────────────────────────────
getUserMedia/getDisplayMedia → WebRTC PeerConnection
   ↓                              ↓
RTC VideoTrack              → RTP Packets
   ↓                              ↓
H264 Encoder                → H264RtpDepacketizer
   ↓                              ↓
RTP Packets                 → NAL Units (Annex B)
   ↓                              ↓
Send over DataChannel       → BufferDataSource
                                   ↓
                            AVIOContext → FFmpeg Demuxer
                                   ↓
                            NVDEC Hardware Decoder
                                   ↓
                            D3D11 Texture Display
```

## Usage

### Option 1: Browser Camera to Native App

1. **Build the application**:
   ```bash
   cmake -B build -DBUILD_WEBRTC_SUPPORT=ON
   cmake --build build --config Release
   ```

2. **Run the native receiver**:
   ```bash
   build\bin\Release\webrtc_player.exe
   ```
   The app will display its SDP offer in the console.

3. **Open the web sender**:
   - Open `examples/webrtc_sender.html` in a modern browser (Chrome/Edge recommended)
   - Click "Start Camera" (or "Share Screen")
   - Grant camera/screen permissions

4. **Exchange SDP**:
   - Copy the SDP offer from `webrtc_player.exe` console
   - Paste it into the "Remote SDP" textarea in the browser
   - Click "Connect"
   - Copy the SDP answer from the "Local SDP" textarea
   - Paste it into the `webrtc_player.exe` console

5. **Watch the stream**:
   - The native app will receive and decode the video stream
   - Hardware-accelerated playback via NVDEC (NVIDIA GPU required)

### Option 2: Screen Sharing to Native App

Same steps as above, but click "Share Screen" instead of "Start Camera" in step 3.

## Implementation Details

### WebRTCDataSource

The `WebRTCDataSource` class implements the `IDataSource` interface and provides:

- **WebRTC Connection Management**: Creates `PeerConnection` via libdatachannel
- **RTP Depacketization**: Uses `H264RtpDepacketizer` to convert RTP packets → NAL units
- **Buffer Management**: Stores NAL units in `BufferDataSource` for FFmpeg to read
- **Non-Seekable Stream**: Properly handles live streaming constraints

### Key Features

- ✅ **Hardware Decoding**: Uses NVDEC for efficient GPU-accelerated H.264 decoding
- ✅ **Real-time Streaming**: Low-latency video transmission
- ✅ **Browser Compatible**: Works with standard WebRTC APIs
- ✅ **NAT Traversal**: Uses STUN servers for connectivity
- ✅ **Extensible**: Supports H.264 and H.265 (with browser support)

## Supported Codecs

| Codec | Browser Support | Native Support | Notes |
|-------|----------------|----------------|-------|
| H.264 | ✅ All browsers | ✅ NVDEC | Recommended |
| H.265 | ⚠️ Limited | ✅ NVDEC | Chrome/Edge only with flag |
| VP8   | ✅ All browsers | ❌ Not yet | Future support |
| VP9   | ✅ All browsers | ❌ Not yet | Future support |

## Troubleshooting

### "WebRTC support not enabled"
- Ensure you built with `-DBUILD_WEBRTC_SUPPORT=ON`
- Check that libdatachannel was found during CMake configure

### "libdatachannel not found"
This should not happen with FetchContent, but if you encounter issues:
```bash
# Option 1: Let CMake auto-download (default)
cmake -B build -DBUILD_WEBRTC_SUPPORT=ON

# Option 2: Use vcpkg (alternative)
vcpkg install libdatachannel
cmake -B build -DBUILD_WEBRTC_SUPPORT=ON -DCMAKE_TOOLCHAIN_FILE=[vcpkg root]/scripts/buildsystems/vcpkg.cmake
```

> The build system tries vcpkg first, then automatically falls back to FetchContent if not found.

### "Connection failed"
- Check firewall settings (UDP ports may be blocked)
- Ensure STUN server is reachable: `stun:stun.l.google.com:19302`
- Try disabling VPN/proxy temporarily

### "No video frames received"
- Verify browser is sending H.264 (check `chrome://webrtc-internals`)
- Ensure NVIDIA GPU is available for hardware decoding
- Check console logs in both browser and native app

### Browser Shows "NotAllowedError"
- Grant camera/screen permissions when prompted
- On HTTPS-only browsers, use `localhost` or serve via HTTPS

## Advanced Configuration

### Custom STUN/TURN Servers

Edit `WebRTCDataSource.cpp`:
```cpp
rtc::Configuration config;
config.iceServers.emplace_back("stun:your-stun-server.com:3478");
config.iceServers.emplace_back("turn:your-turn-server.com:3478", "username", "password");
```

### H.265 Support

Change in `webrtc_player.cpp`:
```cpp
webrtcSource.Initialize("H265", 96);
```

And in browser `webrtc_sender.html`, force H.265:
```javascript
// Note: Limited browser support, may require chrome://flags/#enable-experimental-web-platform-features
const offer = await peerConnection.createOffer({
    offerToReceiveVideo: true,
    offerToReceiveAudio: false
});
```

## Performance Tips

- **Resolution**: Lower resolutions (720p) have lower latency than 1080p
- **Bitrate**: Adjust in `WebRTCDataSource.cpp`: `media.setBitrate(3000)` (Kbps)
- **Frame Rate**: Browser typically caps at 30 FPS for getUserMedia
- **Network**: Use wired Ethernet for best quality, Wi-Fi may introduce jitter

## Example Use Cases

1. **Remote Desktop Viewer**: Stream screen from browser to native app
2. **IP Camera Viewer**: Receive WebRTC streams from network cameras
3. **Video Conferencing**: Multi-peer video reception
4. **Live Streaming**: Receive streams from WebRTC broadcasting services
5. **Robotics/IoT**: Control interface with video feedback

## Future Enhancements

- [ ] VP8/VP9 codec support
- [ ] Audio streaming
- [ ] Multiple simultaneous streams
- [ ] Recording to file
- [ ] Automatic reconnection
- [ ] TURN server support
- [ ] Simulcast support
- [ ] Adaptive bitrate

## License

This example is part of the VideoCaptureDX11 library.
