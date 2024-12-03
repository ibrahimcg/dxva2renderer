#include <windows.h>
#include <d3d9.h>
#include <fstream>
#include <vector>
#include <iostream>
#include <string>
#include <dxva2api.h>
#include <d3d9types.h>
#include <d3dcompiler.h>


#pragma comment(lib, "dxva2.lib")
#define FOURCC_NV12 MAKEFOURCC('N', 'V', '1', '2')

template<typename T>
T clamp(T value, T min, T max) {
    return value < min ? min : (value > max ? max : value);
}
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dcompiler.lib")

class DXVA2Player {
private:
    IDirect3D9* m_pD3D = nullptr;
    IDirect3DDevice9* m_pDevice = nullptr;
    IDirect3DTexture9* m_pYTexture = nullptr;
    IDirect3DTexture9* m_pUVTexture = nullptr;
    IDirect3DVertexBuffer9* m_pVertexBuffer = nullptr;
    IDirect3DPixelShader9* m_pPixelShader = nullptr;
    const int WIDTH = 640;
    const int HEIGHT = 360;
    std::ifstream m_file;
    std::vector<BYTE> m_frameBuffer;

    struct CUSTOMVERTEX {
        float x, y, z;
        float u, v;
    };
#define D3DFVF_CUSTOMVERTEX (D3DFVF_XYZ | D3DFVF_TEX1)

    // Fixed shader code with proper HLSL syntax
    const char* pixelShaderCode = R"(
        texture2D YTexture;
        texture2D UVTexture;

        sampler2D YSampler = sampler_state {
            Texture = <YTexture>;
            MipFilter = NONE;
            MinFilter = POINT;
            MagFilter = POINT;
            AddressU = CLAMP;
            AddressV = CLAMP;
        };

        sampler2D UVSampler = sampler_state {
            Texture = <UVTexture>;
            MipFilter = NONE;
            MinFilter = POINT;
            MagFilter = POINT;
            AddressU = CLAMP;
            AddressV = CLAMP;
        };

        struct PS_INPUT {
            float2 tex : TEXCOORD0;
        };

        float4 main(PS_INPUT input) : COLOR0
        {
            // Sample YUV values
            float Y = tex2D(YSampler, input.tex).r;
            float2 UV = tex2D(UVSampler, input.tex).rg;
    
            // Simple range adjustment for limited range input
            Y = (Y - 16.0/255.0);
            UV -= 128.0/255.0;
    
            // BT.709 conversion matrix for HD content
            float3 rgb;
            rgb.r = Y + 1.5748 * UV.y;
            rgb.g = Y - 0.1873 * UV.x - 0.4681 * UV.y;
            rgb.b = Y + 1.8556 * UV.x;
    
            // Ensure we stay in valid range
            rgb = saturate(rgb);
    
            return float4(rgb, 1.0);
        }
    )";


public:
    HRESULT Initialize(HWND hWnd, const std::string& filePath) {
        // Create D3D device (similar to original code)
        m_pD3D = Direct3DCreate9(D3D_SDK_VERSION);
        if (!m_pD3D) return E_FAIL;

        D3DPRESENT_PARAMETERS d3dpp = {};
        d3dpp.BackBufferWidth = WIDTH;
        d3dpp.BackBufferHeight = HEIGHT;
        d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
        d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        d3dpp.Windowed = TRUE;
        d3dpp.hDeviceWindow = hWnd;

        HRESULT hr = m_pD3D->CreateDevice(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL,
            hWnd,
            D3DCREATE_HARDWARE_VERTEXPROCESSING,
            &d3dpp,
            &m_pDevice
        );
        if (FAILED(hr)) return hr;

        // Create textures for Y and UV planes
        hr = m_pDevice->CreateTexture(WIDTH, HEIGHT, 1, 0, D3DFMT_L8, D3DPOOL_MANAGED, &m_pYTexture, NULL);
        if (FAILED(hr)) return hr;

        hr = m_pDevice->CreateTexture(WIDTH / 2, HEIGHT / 2, 1, 0, D3DFMT_A8L8, D3DPOOL_MANAGED, &m_pUVTexture, NULL);
        if (FAILED(hr)) return hr;

        // Create vertex buffer
        CUSTOMVERTEX vertices[] = {
            {-1.0f, 1.0f, 0.0f, 0.0f, 0.0f},
            {1.0f, 1.0f, 0.0f, 1.0f, 0.0f},
            {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
            {1.0f, -1.0f, 0.0f, 1.0f, 1.0f}
        };

        hr = m_pDevice->CreateVertexBuffer(
            4 * sizeof(CUSTOMVERTEX),
            0,
            D3DFVF_CUSTOMVERTEX,
            D3DPOOL_DEFAULT,
            &m_pVertexBuffer,
            NULL
        );
        if (FAILED(hr)) return hr;

        void* pVertices;
        hr = m_pVertexBuffer->Lock(0, sizeof(vertices), &pVertices, 0);
        memcpy(pVertices, vertices, sizeof(vertices));
        m_pVertexBuffer->Unlock();

        // Compile and create pixel shader using D3DCompiler
        ID3DBlob* pShaderBlob = nullptr;
        ID3DBlob* pErrorBlob = nullptr;

        hr = D3DCompile(
            pixelShaderCode,
            strlen(pixelShaderCode),
            NULL,
            NULL,
            NULL,
            "main",
            "ps_2_0",
            0,
            0,
            &pShaderBlob,
            &pErrorBlob
        );

        if (FAILED(hr)) {
            if (pErrorBlob) {
                MessageBoxA(NULL, (char*)pErrorBlob->GetBufferPointer(), "Shader Compilation Error", MB_OK);
                pErrorBlob->Release();
            }
            return hr;
        }

        hr = m_pDevice->CreatePixelShader(
            (DWORD*)pShaderBlob->GetBufferPointer(),
            &m_pPixelShader
        );

        pShaderBlob->Release();
        if (pErrorBlob) pErrorBlob->Release();

        m_frameBuffer.resize(WIDTH * HEIGHT * 3 / 2);
        m_file.open(filePath, std::ios::binary);

        return hr;
    }

    bool RenderNextFrame() {
        if (!m_file.read(reinterpret_cast<char*>(m_frameBuffer.data()), m_frameBuffer.size())) {
            return false;
        }

        // Update Y texture
        D3DLOCKED_RECT yRect;
        m_pYTexture->LockRect(0, &yRect, NULL, 0);
        for (int y = 0; y < HEIGHT; y++) {
            memcpy(
                (BYTE*)yRect.pBits + y * yRect.Pitch,
                m_frameBuffer.data() + y * WIDTH,
                WIDTH
            );
        }
        m_pYTexture->UnlockRect(0);

        // Update UV texture
        D3DLOCKED_RECT uvRect;
        m_pUVTexture->LockRect(0, &uvRect, NULL, 0);
        const BYTE* uvPlane = m_frameBuffer.data() + (WIDTH * HEIGHT);
        for (int y = 0; y < HEIGHT / 2; y++) {
            memcpy(
                (BYTE*)uvRect.pBits + y * uvRect.Pitch,
                uvPlane + y * WIDTH,
                WIDTH
            );
        }
        m_pUVTexture->UnlockRect(0);

        // Render
        m_pDevice->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
        m_pDevice->BeginScene();

        m_pDevice->SetPixelShader(m_pPixelShader);
        m_pDevice->SetTexture(0, m_pYTexture);
        m_pDevice->SetTexture(1, m_pUVTexture);
        m_pDevice->SetStreamSource(0, m_pVertexBuffer, 0, sizeof(CUSTOMVERTEX));
        m_pDevice->SetFVF(D3DFVF_CUSTOMVERTEX);
        m_pDevice->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);

        m_pDevice->EndScene();
        m_pDevice->Present(NULL, NULL, NULL, NULL);

        return true;
    }

    ~DXVA2Player() {
        if (m_file.is_open()) m_file.close();
        if (m_pPixelShader) m_pPixelShader->Release();
        if (m_pVertexBuffer) m_pVertexBuffer->Release();
        if (m_pYTexture) m_pYTexture->Release();
        if (m_pUVTexture) m_pUVTexture->Release();
        if (m_pDevice) m_pDevice->Release();
        if (m_pD3D) m_pD3D->Release();
    }
};



int main() {
    std::string filePath;
#ifdef _DEBUG
    filePath = "C:\\Users\\ibrah\\Downloads\\output.raw";
#else
    std::cout << "Enter path to NV12 raw file: ";
    std::getline(std::cin, filePath);
#endif

    HINSTANCE hInstance = GetModuleHandle(NULL);

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"NV12Player";
    RegisterClassEx(&wc);

    RECT windowRect = { 0, 0, 640, 360 };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hWnd = CreateWindow(
        L"NV12Player",
        L"NV12 Video Player",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        NULL, NULL, hInstance, NULL
    );


    if (!hWnd) {
        MessageBoxW(NULL, L"Window Creation Failed!", L"Error", MB_ICONERROR);
        return 1;
    }

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    DXVA2Player player;
    if (FAILED(player.Initialize(hWnd, filePath))) {
        MessageBoxW(NULL, L"Player Initialization Failed!", L"Error", MB_ICONERROR);
        return 1;
    }

    MSG msg = {};
    DWORD frameTime = GetTickCount64();
    const DWORD frameDelay = 33; // ~30fps

    while (true) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        DWORD currentTime = GetTickCount64();
        if (currentTime - frameTime >= frameDelay) {
            if (!player.RenderNextFrame()) break;
            frameTime = currentTime;
        }
        else {
            Sleep(1);
        }
    }

    return static_cast<int>(msg.wParam);
}