# Building FFmpeg with AV1 Hardware Decoding Support

This guide explains how to build FFmpeg from source with native AV1 D3D11VA hardware decoding support for Windows.

## Why Custom Build is Needed

The pre-built FFmpeg binaries (BtbN builds) use **libdav1d** for AV1 decoding, which is a CPU-based software decoder. For hardware-accelerated AV1 decoding with D3D11 texture output, you need FFmpeg's **native AV1 decoder** with D3D11VA support.

**Key difference:**
- `libdav1d`: Software decoding only (CPU)
- Native AV1 decoder + D3D11VA: Hardware decoding (GPU)

---

## Prerequisites

### Required Software

1. **Visual Studio 2022**
   - MSVC compiler (cl.exe)
   - Windows SDK 10.0.19041.0 or newer (for DXVA_PicParams_AV1 headers)
   - Install via Visual Studio Installer

2. **MSYS2**
   - Provides bash environment needed for FFmpeg's configure script
   - Download from: https://www.msys2.org/
   - Install to default location: `C:\msys64`

---

## Step 1: Install MSYS2 Build Tools

1. Download and install MSYS2 from https://www.msys2.org/

2. Open **MSYS2 MINGW64** terminal

3. Update package database:
```bash
pacman -Syu
```

4. Install required build tools:
```bash
pacman -S make yasm nasm pkg-config diffutils
```

---

## Step 2: Setup MSVC Environment

FFmpeg needs to be built with MSVC to match your project's compiler.

1. Open **"x64 Native Tools Command Prompt for VS 2022"** from Start Menu

2. Launch MSYS2 from within this prompt (this inherits MSVC environment):
```cmd
C:\msys64\msys2_shell.cmd -mingw64 -use-full-path
```

3. Verify MSVC is available:
```bash
which cl.exe
# Should output: /c/Program Files/Microsoft Visual Studio/...
```

---

## Step 3: Download FFmpeg Source

```bash
cd /c/build
curl -L https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n7.1.2.tar.gz -o ffmpeg.tar.gz
tar -xzf ffmpeg.tar.gz
cd FFmpeg-n7.1.2
```

---

## Step 4: Configure FFmpeg

This is the critical step. The configuration **disables libdav1d** and **enables native AV1 decoder** with D3D11VA support.

```bash
./configure \
  --prefix=/c/ffmpeg-av1-d3d11va \
  --toolchain=msvc \
  --arch=x86_64 \
  --target-os=win64 \
  \
  --enable-shared \
  --disable-static \
  --disable-programs \
  --disable-doc \
  --disable-debug \
  \
  --disable-libdav1d \
  --enable-decoder=av1 \
  --enable-decoder=h264 \
  --enable-decoder=hevc \
  \
  --enable-d3d11va \
  --enable-dxva2 \
  \
  --enable-zlib \
  --enable-iconv \
  \
  --extra-cflags="-MD" \
  --extra-cxxflags="-MD"
```

### Important Configure Flags

| Flag | Purpose |
|------|---------|
| `--toolchain=msvc` | Use MSVC compiler instead of MinGW |
| `--disable-libdav1d` | **Critical**: Prevents libdav1d from being used |
| `--enable-decoder=av1` | Enables native AV1 decoder |
| `--enable-d3d11va` | Enables D3D11VA hardware acceleration |
| `--enable-dxva2` | Enables DXVA2 (fallback for older systems) |
| `--extra-cflags="-MD"` | Use dynamic runtime (matches your project) |

### Configuration Output

After running configure, verify AV1 D3D11VA support is enabled:

```bash
grep "CONFIG_AV1_D3D11VA_HWACCEL" config.h
```

**Expected output:**
```
#define CONFIG_AV1_D3D11VA_HWACCEL 1
```

If you see `#define CONFIG_AV1_D3D11VA_HWACCEL 0`, check that:
- Windows SDK 10.0.19041.0+ is installed
- `DXVA_PicParams_AV1` header is available

---

## Step 5: Build FFmpeg

```bash
make -j8
```

This will take 10-15 minutes depending on your CPU.

---

## Step 6: Install FFmpeg

```bash
make install
```

FFmpeg will be installed to `C:\ffmpeg-av1-d3d11va\`:
- **Headers**: `C:\ffmpeg-av1-d3d11va\include`
- **Import libs**: `C:\ffmpeg-av1-d3d11va\lib`
- **DLLs**: `C:\ffmpeg-av1-d3d11va\bin`

---

## Step 7: Verify Build

Test that AV1 decoder with D3D11VA is available:

```bash
cd /c/ffmpeg-av1-d3d11va/bin
./ffmpeg -decoders | grep av1
./ffmpeg -hwaccels
```

**Expected output:**
```
V..... av1    Alliance for Open Media AV1 (codec av1)
...
d3d11va
dxva2
```

### Test AV1 Hardware Decoding

```bash
./ffmpeg -hwaccel d3d11va -hwaccel_output_format d3d11 \
  -i /path/to/test_av1.mp4 \
  -f null -
```

If working correctly, you should see low CPU usage and the decode happening on GPU.

---

## Step 8: Use Custom FFmpeg in Your Project

Update your CMake configuration:

```bash
cd D:\video-capture-dx11
cmake -B build -G "Visual Studio 17 2022" \
  -DUSE_CUSTOM_FFMPEG=ON \
  -DFFMPEG_DIR=C:/ffmpeg-av1-d3d11va

cmake --build build --config Release
```

---

## Troubleshooting

### Configure Fails with "DXVA_PicParams_AV1 not found"

**Solution:**
- Install Windows SDK 10.0.19041.0 or newer via Visual Studio Installer
- Check path: `C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\um\dxva.h`

### "cl.exe not found" during configure

**Solution:**
- Ensure you launched MSYS2 from "x64 Native Tools Command Prompt"
- Verify with: `which cl.exe`

### Native AV1 decoder not available after build

**Solution:**
- Check `config.log` for why av1_decoder was disabled
- Ensure `--disable-libdav1d` is present in configure command
- Verify: `grep "av1_decoder" config.h` should show `1`

### Build succeeds but still software decoding

**Solution:**
- Verify av1_d3d11va_hwaccel was compiled:
```bash
grep "av1_d3d11va_hwaccel" config.h
```
- Should see: `#define CONFIG_AV1_D3D11VA_HWACCEL 1`

---

## Advanced: Minimal Build

If you want an even smaller FFmpeg build for just this use case:

```bash
./configure \
  --prefix=/c/ffmpeg-av1-d3d11va \
  --toolchain=msvc \
  --arch=x86_64 \
  --target-os=win64 \
  --enable-shared \
  --disable-static \
  --disable-all \
  --enable-avcodec \
  --enable-avformat \
  --enable-avutil \
  --enable-decoder=av1 \
  --enable-decoder=h264 \
  --enable-decoder=hevc \
  --enable-hwaccel=av1_d3d11va \
  --enable-hwaccel=h264_d3d11va \
  --enable-hwaccel=hevc_d3d11va \
  --enable-d3d11va \
  --enable-dxva2 \
  --enable-protocol=file \
  --enable-demuxer=mov \
  --enable-demuxer=matroska \
  --extra-cflags="-MD" \
  --extra-cxxflags="-MD"
```

This creates a minimal FFmpeg with only required components.

---

## References

- FFmpeg configure options: https://www.ffmpeg.org/ffmpeg-all.html
- D3D11VA API: https://ffmpeg.org/doxygen/trunk/d3d11va_8h.html
- DXVA AV1 Spec: https://www.microsoft.com/en-us/download/details.aspx?id=101577
