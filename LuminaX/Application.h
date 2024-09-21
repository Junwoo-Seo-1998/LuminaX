#pragma once
#include <dxgi1_4.h>
#include <wrl/client.h>
#include "directx//d3d12.h"

class Application
{
public:
	Application();
	~Application();

private:
	bool CreateMainWindow();
	bool CreateDevice();
	static Application* window;

private:
	HINSTANCE mhInstance;
	HWND mhMainWindow;
	Microsoft::WRL::ComPtr<IDXGIFactory4> mdxgiFactory;
	Microsoft::WRL::ComPtr<ID3D12Device>   md3dDevice;
};

