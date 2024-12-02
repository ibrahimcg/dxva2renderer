#include <windows.h>
#include <d3d9.h>
#include <fstream>
#include <vector>
#include <iostream>
#include <string>
#include <dxva2api.h>
#include <d3d9types.h>

#pragma comment(lib, "dxva2.lib")
#define FOURCC_NV12 MAKEFOURCC('N', 'V', '1', '2')

template<typename T>
T clamp(T value, T min, T max) {
    return value < min ? min : (value > max ? max : value);
}

class DXVA2Player {
private:
    IDirect3D9* m_pD3D = nullptr;
    IDirect3DDevice9* m_pDevice = nullptr;
    IDirect3DSurface9* m_pSurface = nullptr;
    const int WIDTH = 640;
    const int HEIGHT = 360;
    std::ifstream m_file;
    std::vector<BYTE> m_frameBuffer;
    std::vector<BYTE> m_rgbBuffer;

    void YUVtoRGB(BYTE Y, BYTE U, BYTE V, BYTE& R, BYTE& G, BYTE& B) {
        int C = Y - 16;
        int D = U - 128;
        int E = V - 128;

        int R1 = (298 * C + 409 * E + 128) >> 8;
        int G1 = (298 * C - 100 * D - 208 * E + 128) >> 8;
        int B1 = (298 * C + 516 * D + 128) >> 8;

        R = (BYTE)clamp(R1, 0, 255);
        G = (BYTE)clamp(G1, 0, 255);
        B = (BYTE)clamp(B1, 0, 255);
    }

public:
    HRESULT Initialize(HWND hWnd, const std::string& filePath) {
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
            D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &d3dpp,
            &m_pDevice
        );
        if (FAILED(hr)) return hr;

        hr = m_pDevice->CreateOffscreenPlainSurface(
            WIDTH,
            HEIGHT,
            D3DFMT_X8R8G8B8,
            D3DPOOL_DEFAULT,
            &m_pSurface,
            NULL
        );

        m_frameBuffer.resize(WIDTH * HEIGHT * 3 / 2);
        m_rgbBuffer.resize(WIDTH * HEIGHT * 4);
        m_file.open(filePath, std::ios::binary);
        if (!m_file.is_open()) return E_FAIL;

        return hr;
    }

    bool RenderNextFrame() {
        if (!m_file.read(reinterpret_cast<char*>(m_frameBuffer.data()), m_frameBuffer.size())) {
            return false;
        }

        const BYTE* Y = m_frameBuffer.data();
        const BYTE* UV = Y + (WIDTH * HEIGHT);

        D3DLOCKED_RECT rect;
        HRESULT hr = m_pSurface->LockRect(&rect, NULL, 0);
        if (FAILED(hr)) return false;

        BYTE* dst = static_cast<BYTE*>(rect.pBits);
        for (int y = 0; y < HEIGHT; y++) {
            for (int x = 0; x < WIDTH; x++) {
                int uvIndex = (y / 2) * WIDTH + (x - (x % 2));
                BYTE r, g, b;
                YUVtoRGB(
                    Y[y * WIDTH + x],
                    UV[uvIndex],
                    UV[uvIndex + 1],
                    r, g, b
                );

                dst[y * rect.Pitch + x * 4 + 0] = b;
                dst[y * rect.Pitch + x * 4 + 1] = g;
                dst[y * rect.Pitch + x * 4 + 2] = r;
                dst[y * rect.Pitch + x * 4 + 3] = 255;
            }
        }

        m_pSurface->UnlockRect();

        IDirect3DSurface9* pBackBuffer = nullptr;
        m_pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);
        m_pDevice->StretchRect(m_pSurface, NULL, pBackBuffer, NULL, D3DTEXF_NONE);
        pBackBuffer->Release();
        m_pDevice->Present(NULL, NULL, NULL, NULL);

        return true;
    }

    ~DXVA2Player() {
        if (m_file.is_open()) m_file.close();
        if (m_pSurface) m_pSurface->Release();
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
    DWORD frameTime = GetTickCount();
    const DWORD frameDelay = 33; // ~30fps

    while (true) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        DWORD currentTime = GetTickCount();
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