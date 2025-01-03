#include "Application.h"
#include "directx/d3dx12.h"
#include "dxgi.h"
#include <Windows.h>
#include <windowsx.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
using namespace Microsoft::WRL;
LRESULT CALLBACK
MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	return Application::Get()->MsgProc(hwnd, msg, wParam, lParam);
}

Application* Application::App = nullptr;
Application::Application()
{
	//ensured
	assert(App == nullptr);
	App = this;
}

Application::~Application()
{
	App = nullptr;
}

Application* Application::Get()
{
	return App;
}

bool Application::Init(HINSTANCE hinstance)
{
	mhInstance = hinstance;
	CreateMainWindow();

	//d3d12
	CreateDevice();
	CreateCommandObjects();
	CreateSwapChain();
	CreateRtvAndDsvDescHeap();

	OnResize();
	return true;
}

int Application::Run()
{
	// Setup Dear ImGui context
	/*IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // IF using Docking Branch*/

	/*// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(mhMainWindow);
	ImGui_ImplDX12_Init(md3dDevice, 2, mBackBufferFormat,
		YOUR_SRV_DESC_HEAP,
		// You'll need to designate a descriptor from your descriptor heap for Dear ImGui to use internally for its font texture's SRV
		YOUR_CPU_DESCRIPTOR_HANDLE_FOR_FONT_SRV,
		YOUR_GPU_DESCRIPTOR_HANDLE_FOR_FONT_SRV);*/


	MSG msg = { 0 };

	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			if (!mPaused)
			{
				//CalculateFrameStats();
				Update();
				Draw();
			}
			else
			{
				Sleep(100);
			}
		}
	}

	/*ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();*/
	//ImGui::DestroyContext();

	return (int)msg.wParam;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT Application::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
		return true;

	switch(msg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	case WM_SIZE:
		mWidth = LOWORD(lParam);
		mHeight = HIWORD(lParam);

		if (md3dDevice)
		{
			if (wParam == SIZE_MINIMIZED)
			{
			}
			else if (wParam == SIZE_MAXIMIZED)
			{
				OnResize();
			}
			else if (wParam == SIZE_RESTORED)
			{
				OnResize();
			}
		}
		return 0;

		// 윈도우가 너무 작아지는것을 방지합니다.
	case WM_GETMINMAXINFO:
		((MINMAXINFO*)lParam)->ptMinTrackSize.x = 200;
		((MINMAXINFO*)lParam)->ptMinTrackSize.y = 200;
		return 0;

	case WM_LBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
		OnMouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;

	case WM_LBUTTONUP:
	case WM_MBUTTONUP:
	case WM_RBUTTONUP:
		OnMouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;

	case WM_MOUSEMOVE:
		OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

void Application::FlushCommandQueue()
{
	mCurrentFence++;

	mCommandQueue->Signal(mFence.Get(), mCurrentFence);

	if (mFence->GetCompletedValue() < mCurrentFence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
		mFence->SetEventOnCompletion(mCurrentFence, eventHandle);
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
}

void Application::OnResize()
{
	FlushCommandQueue();
	mCommandList->Reset(mCommandListAlloc.Get(), nullptr);

	for (int i = 0; i < SwapChainBufferCount; ++i)
		mSwapChainBuffer[i].Reset();
	mDepthStencilBuffer.Reset();

	mSwapChain->ResizeBuffers(SwapChainBufferCount, mWidth, mHeight, mBackBufferFormat, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);

	mCurrBackBuffer = 0;

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(mRtvDescHeap->GetCPUDescriptorHandleForHeapStart());
	for(UINT i=0; i< SwapChainBufferCount; ++i)
	{
		mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i]));
		md3dDevice->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
		rtvHeapHandle.Offset(1, mRtvDescSize);
	}

	//depth stencil
	D3D12_RESOURCE_DESC dsDesc;
	dsDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	dsDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	dsDesc.Alignment = 0;
	dsDesc.Width = mWidth;
	dsDesc.Height = mHeight;
	dsDesc.MipLevels = 1;
	dsDesc.DepthOrArraySize = 1;
	dsDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;

	dsDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	dsDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	dsDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

	D3D12_CLEAR_VALUE clearVal;
	clearVal.Format = mDepthStencilFormat;
	clearVal.DepthStencil.Depth = 1.f;
	clearVal.DepthStencil.Stencil = (UINT8)0.f;

	auto heapProperty = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	md3dDevice->CreateCommittedResource(&heapProperty, D3D12_HEAP_FLAG_NONE,
	                                    &dsDesc, D3D12_RESOURCE_STATE_COMMON, &clearVal,
	                                    IID_PPV_ARGS((&mDepthStencilBuffer)));
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = mDepthStencilFormat;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.Texture2D.MipSlice = 0;
	md3dDevice->CreateDepthStencilView(mDepthStencilBuffer.Get(), &dsvDesc,
	                                   mDsvDescHeap->GetCPUDescriptorHandleForHeapStart());

	auto transition = CD3DX12_RESOURCE_BARRIER::Transition(mDepthStencilBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	mCommandList->ResourceBarrier(1, &transition);

	mCommandList->Close();
	ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);
	FlushCommandQueue();

	mScreenViewport.TopLeftX = 0;
	mScreenViewport.TopLeftY = 0;
	mScreenViewport.Width = static_cast<float>(mWidth);
	mScreenViewport.Height = static_cast<float>(mHeight);
	mScreenViewport.MinDepth = 0.0f;
	mScreenViewport.MaxDepth = 1.0f;

	mScissorRect = { 0, 0, mWidth, mHeight };


}


void Application::DrawImGui()
{
	/*ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), mCommandList.Get());*/
}

bool Application::CreateMainWindow()
{
	WNDCLASS wc{ 0, };
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.hInstance = mhInstance;
	wc.hIcon = LoadIcon(0, IDI_APPLICATION);
	wc.hCursor = LoadCursor(0, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
	wc.lpszClassName = L"Main";
	wc.lpfnWndProc = MainWndProc;
	RegisterClass(&wc);

	RECT rect = { 0, 0, mWidth, mHeight };
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

	//create fence
	md3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence));

	mRtvDescSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	mDsvDescSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	mCbvSrvUavDescSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	//test 4x MSAA supported
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevel;
	qualityLevel.Format = mBackBufferFormat;
	qualityLevel.SampleCount = 4;
	qualityLevel.NumQualityLevels = 0;
	qualityLevel.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;

	md3dDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qualityLevel, sizeof(qualityLevel));

	m4xMsaaQuality = qualityLevel.NumQualityLevels;
	return true;
}

bool Application::CreateCommandObjects()
{
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

	md3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQueue));

	md3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(mCommandListAlloc.GetAddressOf()));
	md3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, mCommandListAlloc.Get(), nullptr, IID_PPV_ARGS(mCommandList.GetAddressOf()));

	mCommandList->Close();
	return true;
}

bool Application::CreateSwapChain()
{
	mSwapChain.Reset();

	DXGI_SWAP_CHAIN_DESC swapChainDesc;
	swapChainDesc.BufferDesc.Width = mWidth;
	swapChainDesc.BufferDesc.Height = mHeight;
	swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
	swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
	swapChainDesc.BufferDesc.Format = mBackBufferFormat;
	swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	swapChainDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	swapChainDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = SwapChainBufferCount;//double buffering
	swapChainDesc.OutputWindow = mhMainWindow;
	swapChainDesc.Windowed = true;
	swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	mdxgiFactory->CreateSwapChain(mCommandQueue.Get(), &swapChainDesc, mSwapChain.GetAddressOf());

	return true;
}

bool Application::CreateRtvAndDsvDescHeap()
{
	D3D12_DESCRIPTOR_HEAP_DESC rtvDesc;
	rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvDesc.NumDescriptors = SwapChainBufferCount;
	rtvDesc.NodeMask = 0;

	D3D12_DESCRIPTOR_HEAP_DESC dsvDesc;
	dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvDesc.NumDescriptors = 1;
	dsvDesc.NodeMask = 0;

	md3dDevice->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&mRtvDescHeap)); 
	md3dDevice->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&mDsvDescHeap));
	return true;
}

ID3D12Resource* Application::CurrentBackBuffer()
{
	return mSwapChainBuffer[mCurrBackBuffer].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE Application::GetCurrentBackBufferView()
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(mRtvDescHeap->GetCPUDescriptorHandleForHeapStart(), mCurrBackBuffer, mRtvDescSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE Application::GetDepthStencilBufferView()
{
	return mDsvDescHeap->GetCPUDescriptorHandleForHeapStart();
}
