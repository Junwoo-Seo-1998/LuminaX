#include "Application.h"
#include "directx/d3dx12.h"
#include "dxgi.h"
#include <Windows.h>
using namespace Microsoft::WRL;

bool Application::CreateMainWindow()
{
	WNDCLASS wc{ 0, };
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.hInstance = mhInstance;
	wc.hIcon = LoadIcon(0, IDI_APPLICATION);
	wc.hCursor = LoadCursor(0, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
	wc.lpszClassName = L"Main";

	RegisterClass(&wc);

	RECT rect = { 0, 0, 800, 600 };
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, false);

	int width = rect.right - rect.left;
	int height = rect.bottom - rect.top;

	mhMainWindow = CreateWindow(wc.lpszClassName, L"LuminaX by Junwoo Seo", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
	                            CW_USEDEFAULT, width, height, 0, 0, mhInstance, 0);

	ShowWindow(mhMainWindow, SW_SHOW);
	UpdateWindow(mhMainWindow);
	return true;
}

bool Application::CreateDevice()
{
#if defined(DEBUG) || defined(_DEBUG)
	ComPtr<ID3D12Debug> debugCtrl;
	D3D12GetDebugInterface(IID_PPV_ARGS(&debugCtrl));
#endif

	CreateDXGIFactory1(IID_PPV_ARGS(&mdxgiFactory));

	//try to make hardware device

	HRESULT hwResult = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&md3dDevice));

	if (hwResult < 0)//if fail
	{
		ComPtr<IDXGIAdapter> wrap;
		mdxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&wrap));
		hwResult = D3D12CreateDevice(wrap.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&md3dDevice));
	}


	return true;
}
