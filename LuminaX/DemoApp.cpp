#include "DemoApp.h"

#include "directx/d3dx12.h"
#include "DirectXColors.h"
void DemoApp::Draw()
{
	mCommandListAlloc->Reset();
	mCommandList->Reset(mCommandListAlloc.Get(), nullptr);

	auto transition = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT,
	                                                       D3D12_RESOURCE_STATE_RENDER_TARGET);

	mCommandList->ResourceBarrier(1, &transition);

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	mCommandList->ClearRenderTargetView(GetCurrentBackBufferView(), DirectX::Colors::LightBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(GetDepthStencilBufferView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
	                                    1.0f, 0, 0, nullptr);
	//where to render
	auto currentRtv = GetCurrentBackBufferView();
	auto currentDsv = GetDepthStencilBufferView();
	mCommandList->OMSetRenderTargets(1, &currentRtv, true, &currentDsv);

	//rt to present
	transition = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET,
	                                                  D3D12_RESOURCE_STATE_PRESENT);
	mCommandList->ResourceBarrier(1, &transition);

	mCommandList->Close();

	ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

	mSwapChain->Present(0, 0);
	mCurrBackBuffer = (mCurrBackBuffer + 1) % 2;

	FlushCommandQueue();
}
