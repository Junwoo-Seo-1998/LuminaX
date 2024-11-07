#pragma once
#include <dxgi1_4.h>
#include <wrl.h>
#include "directx//d3d12.h"

class Application
{
public:
	static constexpr int SwapChainBufferCount = 2;
	Application();
	~Application();
	static Application* Get();

	virtual bool Init(HINSTANCE hinstance);
	int Run();
	virtual LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
protected:
	void FlushCommandQueue();
	virtual void OnResize();

	virtual void Update() = 0;
	virtual void Draw() = 0;

	virtual void OnMouseDown(WPARAM btnState, int x, int y) { }
	virtual void OnMouseUp(WPARAM btnState, int x, int y) { }
	virtual void OnMouseMove(WPARAM btnState, int x, int y) { }

	void DrawImGui();

	bool CreateMainWindow();
	bool CreateDevice();
	bool CreateCommandObjects();
	bool CreateSwapChain();
	virtual bool CreateRtvAndDsvDescHeap();

	ID3D12Resource* CurrentBackBuffer();
	D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentBackBufferView();
	D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilBufferView();
protected:
	static Application* App;
	bool mPaused = false;
	int mWidth = 800;
	int mHeight = 600;

	HINSTANCE mhInstance;
	HWND mhMainWindow;
	Microsoft::WRL::ComPtr<IDXGIFactory4> mdxgiFactory;
	Microsoft::WRL::ComPtr<ID3D12Device>   md3dDevice;
	Microsoft::WRL::ComPtr<IDXGISwapChain> mSwapChain;

	UINT64 mCurrentFence = 0;
	int mCurrBackBuffer = 0;

	Microsoft::WRL::ComPtr<ID3D12Resource> mSwapChainBuffer[SwapChainBufferCount];
	Microsoft::WRL::ComPtr<ID3D12Resource> mDepthStencilBuffer;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRtvDescHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDsvDescHeap;

	Microsoft::WRL::ComPtr<ID3D12Fence> mFence;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCommandQueue;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator>mCommandListAlloc;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>mCommandList;

	D3D12_VIEWPORT mScreenViewport;
	D3D12_RECT     mScissorRect;
	//desc
	UINT mRtvDescSize;
	UINT mDsvDescSize;
	UINT mCbvSrvUavDescSize;

	bool m4xMsaaState = false;
	UINT m4xMsaaQuality = 0;

	D3D_DRIVER_TYPE md3dDriverType = D3D_DRIVER_TYPE_HARDWARE;
	DXGI_FORMAT     mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT     mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
};

