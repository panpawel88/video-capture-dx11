#include <VideoCapture.h>
#include <Logger.h>
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <iostream>
#include <string>
#include <algorithm>

using Microsoft::WRL::ComPtr;

// Global variables
ComPtr<ID3D11Device> g_device;
ComPtr<ID3D11DeviceContext> g_context;
ComPtr<IDXGISwapChain> g_swapChain;
ComPtr<ID3D11RenderTargetView> g_renderTargetView;
ComPtr<ID3D11VertexShader> g_vertexShader;
ComPtr<ID3D11PixelShader> g_pixelShaderYUV;
ComPtr<ID3D11InputLayout> g_inputLayout;
ComPtr<ID3D11Buffer> g_vertexBuffer;
ComPtr<ID3D11Buffer> g_indexBuffer;
ComPtr<ID3D11SamplerState> g_samplerState;

VideoCapture g_videoCapture;
bool g_running = true;

// Simple YUV to RGB pixel shader (NV12 format)
const char* g_pixelShaderYUVCode = R"(
Texture2D txY : register(t0);
Texture2D txUV : register(t1);
SamplerState samLinear : register(s0);

struct PS_INPUT {
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_Target {
    float y = txY.Sample(samLinear, input.Tex).r;
    float2 uv = txUV.Sample(samLinear, input.Tex).rg;

    // Convert YUV to RGB (BT.709)
    float u = uv.r - 0.5;
    float v = uv.g - 0.5;

    float r = y + 1.5748 * v;
    float g = y - 0.1873 * u - 0.4681 * v;
    float b = y + 1.8556 * u;

    return float4(r, g, b, 1.0);
}
)";

// Simple vertex shader
const char* g_vertexShaderCode = R"(
struct VS_INPUT {
    float3 Pos : POSITION;
    float2 Tex : TEXCOORD0;
};

struct PS_INPUT {
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD0;
};

PS_INPUT main(VS_INPUT input) {
    PS_INPUT output;
    output.Pos = float4(input.Pos, 1.0f);
    output.Tex = input.Tex;
    return output;
}
)";

struct Vertex {
    float pos[3];
    float tex[2];
};

// Helper function to parse log level from string
LogLevel ParseLogLevel(const std::wstring& levelStr) {
    std::wstring lower = levelStr;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    if (lower == L"error") return LogLevel::Error;
    if (lower == L"warning") return LogLevel::Warning;
    if (lower == L"info") return LogLevel::Info;
    if (lower == L"debug") return LogLevel::Debug;

    return LogLevel::Info; // default
}

// Window procedure
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY:
            g_running = false;
            PostQuitMessage(0);
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                g_running = false;
                PostQuitMessage(0);
            }
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool InitD3D11(HWND hwnd, int width, int height) {
    // Create device and swap chain
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = width;
    sd.BufferDesc.Height = height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;

    UINT createDeviceFlags = 0;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        nullptr, 0, D3D11_SDK_VERSION, &sd, &g_swapChain,
        &g_device, &featureLevel, &g_context);

    if (FAILED(hr)) {
        std::cerr << "Failed to create D3D11 device and swap chain" << std::endl;
        return false;
    }

    // Create render target view
    ComPtr<ID3D11Texture2D> backBuffer;
    g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), &backBuffer);
    g_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &g_renderTargetView);

    // Set viewport
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    g_context->RSSetViewports(1, &viewport);

    // Compile shaders
    ComPtr<ID3DBlob> vsBlob, psBlob, errorBlob;

    hr = D3DCompile(g_vertexShaderCode, strlen(g_vertexShaderCode), nullptr, nullptr, nullptr,
                    "main", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            std::cerr << "Vertex shader compilation error: " << (char*)errorBlob->GetBufferPointer() << std::endl;
        }
        return false;
    }

    hr = D3DCompile(g_pixelShaderYUVCode, strlen(g_pixelShaderYUVCode), nullptr, nullptr, nullptr,
                    "main", "ps_5_0", 0, 0, &psBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            std::cerr << "Pixel shader compilation error: " << (char*)errorBlob->GetBufferPointer() << std::endl;
        }
        return false;
    }

    g_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_vertexShader);
    g_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_pixelShaderYUV);

    // Create input layout
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    g_device->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_inputLayout);

    // Create vertex buffer (fullscreen quad)
    Vertex vertices[] = {
        { { -1.0f,  1.0f, 0.0f }, { 0.0f, 0.0f } },
        { {  1.0f,  1.0f, 0.0f }, { 1.0f, 0.0f } },
        { {  1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f } },
        { { -1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f } }
    };

    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(vertices);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = vertices;
    g_device->CreateBuffer(&bd, &initData, &g_vertexBuffer);

    // Create index buffer
    unsigned short indices[] = { 0, 1, 2, 0, 2, 3 };
    bd.ByteWidth = sizeof(indices);
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    initData.pSysMem = indices;
    g_device->CreateBuffer(&bd, &initData, &g_indexBuffer);

    // Create sampler state
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    g_device->CreateSamplerState(&samplerDesc, &g_samplerState);

    return true;
}

void Render(ID3D11Texture2D* videoTexture, DXGI_FORMAT format) {
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    g_context->ClearRenderTargetView(g_renderTargetView.Get(), clearColor);

    if (videoTexture) {
        // Create shader resource views for Y and UV planes
        ComPtr<ID3D11ShaderResourceView> srvY, srvUV;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8_UNORM; // Y plane
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        g_device->CreateShaderResourceView(videoTexture, &srvDesc, &srvY);

        srvDesc.Format = DXGI_FORMAT_R8G8_UNORM; // UV plane
        g_device->CreateShaderResourceView(videoTexture, &srvDesc, &srvUV);

        // Set up pipeline
        g_context->IASetInputLayout(g_inputLayout.Get());
        UINT stride = sizeof(Vertex);
        UINT offset = 0;
        g_context->IASetVertexBuffers(0, 1, g_vertexBuffer.GetAddressOf(), &stride, &offset);
        g_context->IASetIndexBuffer(g_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        g_context->VSSetShader(g_vertexShader.Get(), nullptr, 0);
        g_context->PSSetShader(g_pixelShaderYUV.Get(), nullptr, 0);
        g_context->PSSetShaderResources(0, 1, srvY.GetAddressOf());
        g_context->PSSetShaderResources(1, 1, srvUV.GetAddressOf());
        g_context->PSSetSamplers(0, 1, g_samplerState.GetAddressOf());
        g_context->OMSetRenderTargets(1, g_renderTargetView.GetAddressOf(), nullptr);

        // Draw
        g_context->DrawIndexed(6, 0, 0);
    }

    g_swapChain->Present(1, 0);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Parse command line
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    if (argc < 2) {
        MessageBoxA(nullptr,
            "Usage: simple_player.exe <video_file.mp4> [options]\n\n"
            "Options:\n"
            "  --log-level <level>  Set log level (error, warning, info, debug)\n"
            "  -l <level>           Short form of --log-level",
            "Simple Player", MB_OK | MB_ICONINFORMATION);
        return 1;
    }

    // Parse arguments
    std::wstring videoPath(argv[1]);
    LogLevel logLevel = LogLevel::Info; // default

    for (int i = 2; i < argc; i++) {
        std::wstring arg(argv[i]);
        if ((arg == L"--log-level" || arg == L"-l") && i + 1 < argc) {
            logLevel = ParseLogLevel(argv[i + 1]);
            i++; // skip next argument
        }
    }

    // Set log level
    Logger::GetInstance().SetLogLevel(logLevel);

    // Convert wide string to narrow string for VideoCapture
    std::string videoPathNarrow(videoPath.begin(), videoPath.end());

    // Create window
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
    wc.lpszClassName = L"SimplePlayerWindowClass";
    RegisterClassEx(&wc);

    int width = 1280;
    int height = 720;

    HWND hwnd = CreateWindowEx(
        0, L"SimplePlayerWindowClass", L"Simple Video Player - Hardware Decoding",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        MessageBoxA(nullptr, "Failed to create window", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Initialize D3D11
    if (!InitD3D11(hwnd, width, height)) {
        MessageBoxA(nullptr, "Failed to initialize D3D11", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Initialize VideoCapture
    if (!VideoCapture::Initialize(g_device.Get())) {
        MessageBoxA(nullptr, "Failed to initialize VideoCapture", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Open video file
    if (!g_videoCapture.open(videoPathNarrow)) {
        std::string error = "Failed to open video file: " + videoPathNarrow;
        MessageBoxA(nullptr, error.c_str(), "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    std::cout << "Video opened successfully" << std::endl;
    std::cout << "Resolution: " << g_videoCapture.get(CAP_PROP_FRAME_WIDTH) << "x" << g_videoCapture.get(CAP_PROP_FRAME_HEIGHT) << std::endl;
    std::cout << "FPS: " << g_videoCapture.get(CAP_PROP_FPS) << std::endl;
    std::cout << "Frame count: " << g_videoCapture.get(CAP_PROP_FRAME_COUNT) << std::endl;
    std::cout << "Press ESC to exit" << std::endl;

    // Main loop
    MSG msg = {};
    while (g_running) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // Read and display frame
        ID3D11Texture2D* texture = nullptr;
        bool isYUV = false;
        DXGI_FORMAT format;

        if (g_videoCapture.read(&texture, isYUV, format)) {
            Render(texture, format);
            if (texture) {
                texture->Release(); // Release the reference we got from read()
            }
        } else {
            // End of video or error - loop back to start
            g_videoCapture.set(CAP_PROP_POS_FRAMES, 0);
        }

        Sleep(16); // ~60 FPS
    }

    g_videoCapture.release();
    LocalFree(argv);
    return 0;
}