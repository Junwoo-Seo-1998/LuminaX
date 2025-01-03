#include "DemoApp.h"

#include <d3dcompiler.h>

#include "directx/d3dx12.h"
#include "DirectXMath.h"
#include "DirectXColors.h"
#include "GraphicsUtil.h"
#include "MeshGenerator.h"
#include "Buffers.h"

#include <algorithm>
#include <numbers>


using Microsoft::WRL::ComPtr;
using namespace DirectX;
//using namespace DirectX::PackedVector;

const int GraphicsUtil::gNumFrameResources = 3;

struct Vertex
{
	XMFLOAT3 Pos;
	XMFLOAT3 Normal;
	XMFLOAT2 TexC;
};


bool DemoApp::Init(HINSTANCE hinstance)
{
	if (!Application::Init(hinstance))
		return false;

	// 초기화 명령들을 기록하기 위해 커맨드 리스트를 리셋합니다.
	ThrowIfFailed(mCommandList->Reset(mCommandListAlloc.Get(), nullptr));

	mCamera.SetPosition(0.0f, 2.0f, -15.0f);

	BuildCubeFaceCamera(0.f, 0.f, 0.f);
	mDynamicCubeMap = std::make_unique<CubeRenderTarget>(md3dDevice.Get(),
		CubeMapSize, CubeMapSize, DXGI_FORMAT_R8G8B8A8_UNORM);

	mBlurFilter = std::make_unique<BlurFilter>(md3dDevice.Get(),
		mWidth,
		mHeight,
		DXGI_FORMAT_R8G8B8A8_UNORM);

	LoadTextures();
	BuildDescHeaps();
	BuildDescViews();
	BuildCubeDepthStencil();
	BuildRootSignature();
	BuildPostProcessRootSignature();
	BuildShaderAndInputLayout();
	BuildShapeGeometry();
	BuildMaterials();
	BuildRenderItems();
	BuildFrameResources();
	BuildPSO();
	mGeneralFrameResource = std::make_unique<FrameResource>(
		md3dDevice.Get(), 6, 1, 1);

	// 초기화 명령들을 실행시킵니다.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(1, cmdLists);

	// 초기화가 종료될 때가지 기다립니다.
	FlushCommandQueue();


	

	if(true)
	{
		for (int i = 0; i < 6; ++i)
		{
			PassConstants cubeFacePassCB = mMainPassCB;

			XMMATRIX view = mCubeMapCamera[i].GetView();
			XMMATRIX proj = mCubeMapCamera[i].GetProj();

			XMMATRIX viewProj = XMMatrixMultiply(view, proj);
			auto det = XMMatrixDeterminant(view);
			XMMATRIX invView = XMMatrixInverse(&det, view);
			det = XMMatrixDeterminant(proj);
			XMMATRIX invProj = XMMatrixInverse(&det, proj);
			det = XMMatrixDeterminant(viewProj);
			XMMATRIX invViewProj = XMMatrixInverse(&det, viewProj);

			XMStoreFloat4x4(&cubeFacePassCB.View, XMMatrixTranspose(view));
			XMStoreFloat4x4(&cubeFacePassCB.InvView, XMMatrixTranspose(invView));
			XMStoreFloat4x4(&cubeFacePassCB.Proj, XMMatrixTranspose(proj));
			XMStoreFloat4x4(&cubeFacePassCB.InvProj, XMMatrixTranspose(invProj));
			XMStoreFloat4x4(&cubeFacePassCB.ViewProj, XMMatrixTranspose(viewProj));
			XMStoreFloat4x4(&cubeFacePassCB.InvViewProj, XMMatrixTranspose(invViewProj));
			cubeFacePassCB.EyePosW = mCubeMapCamera[i].GetPosition3f();
			cubeFacePassCB.RenderTargetSize = XMFLOAT2((float)CubeMapSize, (float)CubeMapSize);
			cubeFacePassCB.InvRenderTargetSize = XMFLOAT2(1.0f / CubeMapSize, 1.0f / CubeMapSize);

			auto currPassCB = mGeneralFrameResource->PassCB.get();

			// Cube map pass cbuffers are stored in elements 1-6.
			currPassCB->CopyData(i, cubeFacePassCB);
		}


		// 초기화 명령들을 기록하기 위해 커맨드 리스트를 리셋합니다.
		ThrowIfFailed(mCommandList->Reset(mGeneralFrameResource->CmdListAlloc.Get(), nullptr));
		auto viewport = mDynamicCubeMap->Viewport();
		mCommandList->RSSetViewports(1, &viewport);
		auto rect = mDynamicCubeMap->ScissorRect();
		mCommandList->RSSetScissorRects(1, &rect);

		auto toTarget = CD3DX12_RESOURCE_BARRIER::Transition(mDynamicCubeMap->Resource(),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			D3D12_RESOURCE_STATE_RENDER_TARGET);
		// RENDER_TARGET으로 변경합니다.
		mCommandList->ResourceBarrier(1, &toTarget);

		UINT passCBByteSize = GraphicsUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

		ID3D12DescriptorHeap* descriptorHeaps[] = { mGeneralDescHeap.Get() };
		mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		mCommandList->SetGraphicsRootSignature(mRootSig.Get());

		auto matBuffer = mGeneralFrameResource->MaterialBuffer->Resource();
		mCommandList->SetGraphicsRootShaderResourceView(2, matBuffer->GetGPUVirtualAddress());

		CD3DX12_GPU_DESCRIPTOR_HANDLE skyTexDescriptor(mGeneralDescHeap->GetGPUDescriptorHandleForHeapStart());
		skyTexDescriptor.Offset((int)mTextures.size()-1, mCbvSrvUavDescSize);
		mCommandList->SetGraphicsRootDescriptorTable(3, skyTexDescriptor);

		mCommandList->SetGraphicsRootDescriptorTable(4, mGeneralDescHeap->GetGPUDescriptorHandleForHeapStart());

		// 큐브맵 각 면에 대해:
		for (int i = 0; i < 6; ++i)
		{
			// 백버퍼와 뎁스 버퍼를 초기화 합니다.
			mCommandList->ClearRenderTargetView(mDynamicCubeMap->Rtv(i), Colors::BlueViolet, 0, nullptr);
			mCommandList->ClearDepthStencilView(mCubeDSV, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

			// 렌더링 하려는 큐브맵의 i번째 렌더타겟을 설정합니다.
			auto handle = mDynamicCubeMap->Rtv(i);
			mCommandList->OMSetRenderTargets(1, &handle, true, &mCubeDSV);

			// 이 큐브맵 면에 해당하는 상수 버퍼를 바인딩합니다.
			auto passCB = mGeneralFrameResource->PassCB->Resource();
			D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = passCB->GetGPUVirtualAddress() + (i) * passCBByteSize;
			mCommandList->SetGraphicsRootConstantBufferView(1, passCBAddress);

			//DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

			mCommandList->SetPipelineState(mPSOs["diffuseIBL"].Get());
			DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Sky], mGeneralFrameResource->ObjectCB->Resource());

			//mCommandList->SetPipelineState(mPSOs["opaque"].Get());
		}

		// GENERIC_READ로 변경합니다.
		auto toRead = CD3DX12_RESOURCE_BARRIER::Transition(mDynamicCubeMap->Resource(),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_GENERIC_READ);
		mCommandList->ResourceBarrier(1, &toRead);

		// 초기화 명령들을 실행시킵니다.
		ThrowIfFailed(mCommandList->Close());
		ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
		mCommandQueue->ExecuteCommandLists(1, cmdLists);
		FlushCommandQueue();
	}

	return true;
}

void DemoApp::OnResize()
{
	Application::OnResize();

	mCamera.SetLens(0.25f * (float)std::numbers::pi, (float)mWidth / mHeight, 1.0f, 1000.0f);

	if (mBlurFilter != nullptr)
	{
		mBlurFilter->OnResize(mWidth, mHeight);
	}
}

void DemoApp::Update()
{
	UpdateCamera();

	// 다음 프레임 리소스의 자원을 얻기위해 순환합니다.
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % GraphicsUtil::gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	// 현재 프레임 리소스에 대한 명령들이 GPU에서 처리 되었습니까?
	// 처리되지 않았다면 커맨드들의 펜스 지점까지 GPU가 처리할 때까지 기다려야합니다.
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	UpdateObjectCBs();
	UpdateMaterialBuffer();
	UpdateMainPassCB();
}

void DemoApp::Draw()
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	// 커맨드 기록을 위한 메모리를 재활용 합니다.
	// 제출한 커맨드들이 GPU에서 모두 끝났을때 리셋할 수 있습니다.
	ThrowIfFailed(cmdListAlloc->Reset());

	// ExecuteCommandList를 통해 커맨드 큐에 제출한 다음에 커맨드 리스트를 리셋할 수 있습니다.
	if (false)
	{
		ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque_wireframe"].Get()));
	}
	else
	{
		ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));
	}

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// 리소스의 상태를 렌더링을 할 수 있도록 변경합니다.
	{
		auto transition = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET);
		mCommandList->ResourceBarrier(1, &transition);
	}
	// 백 버퍼와 뎁스 버퍼를 클리어 합니다.
	mCommandList->ClearRenderTargetView(GetCurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(GetDepthStencilBufferView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// 어디에 렌더링을 할지 설정합니다.
	{
		auto backBufferView = GetCurrentBackBufferView();
		auto depthBufferView = GetDepthStencilBufferView();
		mCommandList->OMSetRenderTargets(1, &backBufferView, true, &depthBufferView);
	}
	ID3D12DescriptorHeap* descriptorHeaps[] = { mGeneralDescHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSig.Get());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

	auto matBuffer = mCurrFrameResource->MaterialBuffer->Resource();
	mCommandList->SetGraphicsRootShaderResourceView(2, matBuffer->GetGPUVirtualAddress());

	CD3DX12_GPU_DESCRIPTOR_HANDLE skyTexDescriptor(mGeneralDescHeap->GetGPUDescriptorHandleForHeapStart());
	skyTexDescriptor.Offset((int)mTextures.size()-1, mCbvSrvUavDescSize);
	mCommandList->SetGraphicsRootDescriptorTable(3, skyTexDescriptor);

	mCommandList->SetGraphicsRootDescriptorTable(4, mGeneralDescHeap->GetGPUDescriptorHandleForHeapStart());

	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

	mCommandList->SetPipelineState(mPSOs["sky"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Sky]);
	//blur
	if(false)
	{
		mBlurFilter->Execute(mCommandList.Get(), mPostProcessRootSignature.Get(),
			mPSOs["blurH"].Get(), mPSOs["blurV"].Get(), CurrentBackBuffer(), 4);

		auto toDest = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
			D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
		mCommandList->ResourceBarrier(1, &toDest);

		mCommandList->CopyResource(CurrentBackBuffer(), mBlurFilter->Output());

		auto toPresent = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
		mCommandList->ResourceBarrier(1, &toPresent);
	}

	// 리소스의 상태를 출력할 수 있도록 변경합니다.
	{
		auto transition = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PRESENT);
		mCommandList->ResourceBarrier(1, &transition);
	}
	// 커맨드 기록을 종료합니다.
	ThrowIfFailed(mCommandList->Close());

	// 커맨드 리스트의 실행을 위해 큐에 제출합니다.
	ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(1, cmdLists);

	// 백 버퍼와 프론트 버퍼를 교체합니다.
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % 2;

	// 이 펜스 지점까지 커맨드들을 표시하기 위해 펜스 값을 증가합니다,
	mCurrFrameResource->Fence = ++mCurrentFence;

	// 새 펜스 지점을 설정하는 인스트럭션을 커맨드 큐에 추가합니다.
	// 어플리케이션은 GPU 시간축에 있지 않기 때문에,
	// GPU가 모든 커맨드들의 처리가 완료되기 전까지 Signal()을 처리하지 않습니다.
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void DemoApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWindow);
}

void DemoApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void DemoApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// 마우스 한 픽셀의 이동을 0.25도에 대응시킵니다.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		// 입력에 기초에 각도를 갱신해서 카메라가 상자 중심으로 공전하게 합니다.
		mTheta += dx;
		mPhi += dy;

		// mPhi의 각도를 제한합니다.
		mPhi = std::clamp(mPhi, 0.1f, (float)std::numbers::pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		// 마우스 한 픽셀의 이동을 0.005 단위에 대응시킵니다.
		float dx = 0.2f * static_cast<float>(x - mLastMousePos.x);
		float dy = 0.2f * static_cast<float>(y - mLastMousePos.y);

		// 인력에 의해 카메라의 반지름을 업데이트 합니다.
		mRadius += dx - dy;

		// 반지름을 제한합니다.
		mRadius = std::clamp(mRadius, 5.0f, 150.0f);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

bool DemoApp::CreateRtvAndDsvDescHeap()
{
	D3D12_DESCRIPTOR_HEAP_DESC rtvDesc;
	rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvDesc.NumDescriptors = SwapChainBufferCount + 6; //for dynamic cubemap
	rtvDesc.NodeMask = 0;

	D3D12_DESCRIPTOR_HEAP_DESC dsvDesc;
	dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvDesc.NumDescriptors = 2; //for dynamic cubemap
	dsvDesc.NodeMask = 0;

	md3dDevice->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&mRtvDescHeap));
	md3dDevice->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&mDsvDescHeap));

	//dsv to be used for cubemap capturing
	mCubeDSV = CD3DX12_CPU_DESCRIPTOR_HANDLE(
		mDsvDescHeap->GetCPUDescriptorHandleForHeapStart(),
		1,
		mDsvDescSize);

	return true;
}

void DemoApp::LoadTextures()
{
	auto skyTex = std::make_unique<Texture>();
	skyTex->Name = "skyTex";
	skyTex->Filename = L"./Assets/Textures/cube.dds";
	GraphicsUtil::LoadTextureFromFile(skyTex->Filename, md3dDevice.Get(), mCommandList.Get(), skyTex->Resource, skyTex->UploadHeap);

	auto grassTex = std::make_unique<Texture>();
	grassTex->Name = "grassTex";
	grassTex->Filename = L"./Assets/Textures/grass.png";
	GraphicsUtil::LoadTextureFromFile(grassTex->Filename, md3dDevice.Get(), mCommandList.Get(), grassTex->Resource, grassTex->UploadHeap);

	mTextures[skyTex->Name] = std::move(skyTex);
	mTextures[grassTex->Name] = std::move(grassTex);
}

void DemoApp::UpdateCamera()
{
	// 뷰 메트릭스를 계산합니다.
	XMVECTOR pos = XMVectorSet(mRadius * sinf(mPhi) * cosf(mTheta), mRadius * cosf(mPhi), mRadius * sinf(mPhi) * sinf(mTheta), 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	mCamera.LookAt(pos, target, up);
	mCamera.UpdateViewMatrix();
}

void DemoApp::UpdateObjectCBs()
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		// 상수들이 바뀌었 때만 상수 버퍼 데이터를 업데이트 합니다.
		// 이것은 매 프레임 자원마다 수행해야 합니다.
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
			objConstants.MaterialIndex = e->Mat->MatCBIndex;

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// 다음 프레임 리소스도 마찬가지로 업데이트 되어야 합니다.
			e->NumFramesDirty--;
		}
	}
}

void DemoApp::UpdateMaterialBuffer()
{
	auto currMaterialBuffer = mCurrFrameResource->MaterialBuffer.get();
	for (auto& [name, mat]:mMaterials)
	{
		//update only when it was updated
		if(mat->NumFramesDirty>0)
		{
			MaterialData matData;

			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);
			XMStoreFloat4x4(&matData.MatTransform, XMMatrixTranspose(matTransform));
			
			matData.DiffuseAlbedo = mat->DiffuseAlbedo;
			matData.FresnelR0 = mat->FresnelR0;
			matData.Roughness = mat->Roughness;
			matData.DiffuseTexIndex = mat->DiffuseSrvHeapIndex;

			currMaterialBuffer->CopyData(mat->MatCBIndex, matData);
			mat->NumFramesDirty--;
		}
	}
}

void DemoApp::UpdateMainPassCB()
{
	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	auto det = XMMatrixDeterminant(view);
	XMMATRIX invView = XMMatrixInverse(&det, view);

	det = XMMatrixDeterminant(proj);
	XMMATRIX invProj = XMMatrixInverse(&det, proj);

	det = XMMatrixDeterminant(viewProj);
	XMMATRIX invViewProj = XMMatrixInverse(&det, viewProj);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.EyePosW = mCamera.GetPosition3f();
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mWidth, (float)mHeight);
	mMainPassCB.RenderTargetSize = XMFLOAT2(1.0f / mWidth, 1.0f / mHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = .0f;//gt.TotalTime();
	mMainPassCB.DeltaTime = .0f;//gt.DeltaTime();

	XMVECTOR lightDir = -GraphicsUtil::SphericalToCartesian(1.0f, mSunTheta, mSunPhi);

	XMStoreFloat3(&mMainPassCB.Lights[0].Direction, lightDir);
	mMainPassCB.Lights[0].Strength = { 1.0f, 1.0f, 0.9f };

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> DemoApp::GetStaticSamplers()
{
	// 어플리케이션은 보통 몇개의 샘플러만 필요합니다. 그래서 자주 사용되는 샘플러 몇개를 정의하고
	// 루트 시그네쳐의 일부분으로 유지합니다.

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp };
}

void DemoApp::BuildCubeFaceCamera(float x, float y, float z)
{
	XMFLOAT3 center(x, y, z);
	XMFLOAT3 worldUp(0.0, 1.0f, 0.0f);

	// 각 좌표축으로 향하는 시선 벡터입니다.
	XMFLOAT3 targets[6] =
	{
		XMFLOAT3(x + 1.0f, y,        z), // +X
		XMFLOAT3(x - 1.0f, y,        z), // -X
		XMFLOAT3(x,        y + 1.0f, z), // +Y
		XMFLOAT3(x,        y - 1.0f, z), // -Y
		XMFLOAT3(x,        y,        z + 1.0f), // +Z
		XMFLOAT3(x,        y,        z - 1.0f)  // -Z
	};

	// +Y/-Y 방향인 경우 다른 업 벡터를 사용합니다.
	XMFLOAT3 ups[6] =
	{
		XMFLOAT3(0.0f, 1.0f,  0.0f), // +X
		XMFLOAT3(0.0f, 1.0f,  0.0f), // -X
		XMFLOAT3(0.0f, 0.0f, -1.0f), // +Y
		XMFLOAT3(0.0f, 0.0f, +1.0f), // -Y
		XMFLOAT3(0.0f, 1.0f,  0.0f), // +Z
		XMFLOAT3(0.0f, 1.0f,  0.0f)  // -Z
	};

	for (int i = 0; i < 6; ++i)
	{
		mCubeMapCamera[i].LookAt(center, targets[i], ups[i]);
		mCubeMapCamera[i].SetLens(0.5 * XM_PI, 1.0f, 0.1f, 1000.0f);
		mCubeMapCamera[i].UpdateViewMatrix();
	}

}

void DemoApp::BuildDescHeaps()
{
	//sky cube map is included
	const UINT textureDescriptorCount = (UINT)mTextures.size();
	const UINT dynamicCubeMapDesriptorCount = (UINT)1;
	const UINT blurDescriptorCount = (UINT)4;

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = textureDescriptorCount + dynamicCubeMapDesriptorCount + blurDescriptorCount;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mGeneralDescHeap)));
}

void DemoApp::BuildDescViews()
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE handle(mGeneralDescHeap->GetCPUDescriptorHandleForHeapStart());
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = -1;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	int index = 0;
	for (auto& m : mTextures)
	{
		if(m.first=="skyTex")
			continue;
		srvDesc.Format = m.second->Resource->GetDesc().Format;
		md3dDevice->CreateShaderResourceView(m.second->Resource.Get(), &srvDesc, handle);
		handle.Offset(1, mCbvSrvUavDescSize);
		m.second->heapIndex = index++;
	}

	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.Format = mTextures["skyTex"]->Resource->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(mTextures["skyTex"]->Resource.Get(), &srvDesc, handle);
	mTextures["skyTex"]->heapIndex = index++;


	// 스왑 체인 디스크립터 다음에 다이나믹 큐브맵 RTV가 생성됩니다,
	int rtvOffset = SwapChainBufferCount;

	CD3DX12_CPU_DESCRIPTOR_HANDLE cubeRtvHandles[6];
	for (int i = 0; i < 6; ++i)
		cubeRtvHandles[i] = CD3DX12_CPU_DESCRIPTOR_HANDLE(mRtvDescHeap->GetCPUDescriptorHandleForHeapStart(), rtvOffset + i, mRtvDescSize);


	// 하늘 SRV다음에 다이나믹 큐브맵 SRV가 생성됩니다.
	mDynamicTexHeapIndex = index++;
	mDynamicCubeMap->BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE(mGeneralDescHeap->GetCPUDescriptorHandleForHeapStart(), mDynamicTexHeapIndex, mCbvSrvUavDescSize),
		CD3DX12_GPU_DESCRIPTOR_HANDLE(mGeneralDescHeap->GetGPUDescriptorHandleForHeapStart(), mDynamicTexHeapIndex, mCbvSrvUavDescSize),
		cubeRtvHandles);


	mBlurFilter->BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE(mGeneralDescHeap->GetCPUDescriptorHandleForHeapStart(), (int)index, mCbvSrvUavDescSize),
		CD3DX12_GPU_DESCRIPTOR_HANDLE(mGeneralDescHeap->GetGPUDescriptorHandleForHeapStart(), (int)index, mCbvSrvUavDescSize),
		mCbvSrvUavDescSize);
}

void DemoApp::BuildCubeDepthStencil()
{
	D3D12_RESOURCE_DESC depthStencilDesc;
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = CubeMapSize;
	depthStencilDesc.Height = CubeMapSize;
	depthStencilDesc.DepthOrArraySize = 1;
	depthStencilDesc.MipLevels = 1;
	depthStencilDesc.Format = mDepthStencilFormat;
	depthStencilDesc.SampleDesc.Count = 1;
	depthStencilDesc.SampleDesc.Quality = 0;
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optClear;
	optClear.Format = mDepthStencilFormat;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;
	auto heapProperty = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&heapProperty,
		D3D12_HEAP_FLAG_NONE,
		&depthStencilDesc,
		D3D12_RESOURCE_STATE_COMMON,
		&optClear,
		IID_PPV_ARGS(mCubeDepthStencilBuffer.GetAddressOf())));

	md3dDevice->CreateDepthStencilView(mCubeDepthStencilBuffer.Get(), nullptr, mCubeDSV);
	auto transition = CD3DX12_RESOURCE_BARRIER::Transition(mCubeDepthStencilBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	mCommandList->ResourceBarrier(1, &transition);
}

void DemoApp::BuildRootSignature()
{
	CD3DX12_ROOT_PARAMETER slotRootParams[5];

	//texture
	CD3DX12_DESCRIPTOR_RANGE cubeMaps;
	cubeMaps.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);

	CD3DX12_DESCRIPTOR_RANGE tex;
	tex.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 1, 1);

	slotRootParams[0].InitAsConstantBufferView(0);
	slotRootParams[1].InitAsConstantBufferView(1);
	slotRootParams[2].InitAsShaderResourceView(0, 2);
	slotRootParams[3].InitAsDescriptorTable(1, &cubeMaps, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParams[4].InitAsDescriptorTable(1, &tex, D3D12_SHADER_VISIBILITY_PIXEL);

	auto staticSamplers = GetStaticSamplers();

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(_countof(slotRootParams), slotRootParams, (UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;


	HRESULT result = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
	                                             serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	auto res=md3dDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&mRootSig));

}

void DemoApp::BuildPostProcessRootSignature()
{
	CD3DX12_ROOT_PARAMETER slotRootParams[3];

	CD3DX12_DESCRIPTOR_RANGE srvTable;
	srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE uavTable;
	uavTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

	slotRootParams[0].InitAsConstants(12, 0);
	slotRootParams[1].InitAsDescriptorTable(1, &srvTable);
	slotRootParams[2].InitAsDescriptorTable(1, &uavTable);

	Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(_countof(slotRootParams), slotRootParams, 0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	md3dDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(),
	                                IID_PPV_ARGS(mPostProcessRootSignature.GetAddressOf()));
}

void DemoApp::BuildShaderAndInputLayout()
{
	HRESULT result = S_OK;

	mShaders["brdf_VS"] = GraphicsUtil::CompileShader(L"./Assets/Shaders/brdf.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["brdf_PS"] = GraphicsUtil::CompileShader(L"./Assets/Shaders/brdf.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["skyVS"] = GraphicsUtil::CompileShader(L"./Assets/Shaders/Sky.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["skyPS"] = GraphicsUtil::CompileShader(L"./Assets/Shaders/Sky.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["diffuseIBLVS"] = GraphicsUtil::CompileShader(L"./Assets/Shaders/diffuseIBL.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["diffuseIBLPS"] = GraphicsUtil::CompileShader(L"./Assets/Shaders/diffuseIBL.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["blurH_CS"] = GraphicsUtil::CompileShader(L"./Assets/Shaders/blur.hlsl", nullptr, "HorzBlurCS", "cs_5_1");
	mShaders["blurV_CS"] = GraphicsUtil::CompileShader(L"./Assets/Shaders/blur.hlsl", nullptr, "VertBlurCS", "cs_5_1");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{ "NORMAL",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
}

void DemoApp::BuildShapeGeometry()
{
	auto box = MeshGenerator::CreateBox(1.5f, 0.5f, 1.5f, 3);
	auto grid = MeshGenerator::CreateGrid(20.0f, 30.0f, 60, 40);
	auto sphere = MeshGenerator::CreateSphere(0.5f, 20, 20);
	auto cylinder = MeshGenerator::CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

	//
	// 모든 지오메트리를 하나의 큰 버텍스/인덱스 버퍼에 연결해서 저장합니다.
	// 그러므로 각각의 서브메쉬가 버퍼에서 차지하는 영역을 정의합니다.
	//

	// 연결된 정점 버퍼에서 각 오브젝트의 버텍스 오프셋들을 캐시합니다.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

	// 연결된 인덱스 버퍼에서 각 오브젝트의 시작 인덱스를 캐시합니다.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

	// 버텍스/인덱스 버퍼에서 각 도형을 정의하는 서브메쉬 지오메트리를 정의합니다.

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	//
	// 필요한 버텍스 엘리먼트들을 추출하고
	// 모든 메쉬의 버텍스를 한 버텍스 버퍼에 저장합니다.
	//

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		vertices[k].TexC = box.Vertices[i].TexC;
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
		vertices[k].TexC = grid.Vertices[i].TexC;
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].TexC = sphere.Vertices[i].TexC;
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].TexC = cylinder.Vertices[i].TexC;
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = Buffers::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(),
		vertices.data(), vbByteSize,
		geo->VertexBufferUploader);

	geo->IndexBufferGPU = Buffers::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(),
		indices.data(), ibByteSize,
		geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void DemoApp::BuildPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePSODesc;

	ZeroMemory(&opaquePSODesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePSODesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaquePSODesc.pRootSignature = mRootSig.Get();
	
	opaquePSODesc.VS = {
		reinterpret_cast<BYTE*>(mShaders["brdf_VS"]->GetBufferPointer()),
		mShaders["brdf_VS"]->GetBufferSize()
	};

	opaquePSODesc.PS = {
		reinterpret_cast<BYTE*>(mShaders["brdf_PS"]->GetBufferPointer()),
		mShaders["brdf_PS"]->GetBufferSize()
	};

	opaquePSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePSODesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePSODesc.SampleMask = UINT_MAX;
	opaquePSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePSODesc.NumRenderTargets = 1;
	opaquePSODesc.RTVFormats[0] = mBackBufferFormat;
	opaquePSODesc.SampleDesc.Count= m4xMsaaState ? 4 : 1;
	opaquePSODesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePSODesc.DSVFormat = mDepthStencilFormat;

	auto reuslt = md3dDevice->CreateGraphicsPipelineState(&opaquePSODesc, IID_PPV_ARGS(&mPSOs["opaque"]));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueWireframePsoDesc = opaquePSODesc;
	opaqueWireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaqueWireframePsoDesc, IID_PPV_ARGS(&mPSOs["opaque_wireframe"])));

	//skybox
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC skyPsoDesc = opaquePSODesc;

		// 카메라에서 구의 안쪽을 렌더링하기 때문에 컬링을 꺼줍니다.
		skyPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

		skyPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		skyPsoDesc.VS =
		{
			reinterpret_cast<BYTE*>(mShaders["skyVS"]->GetBufferPointer()),
			mShaders["skyVS"]->GetBufferSize()
		};
		skyPsoDesc.PS =
		{
			reinterpret_cast<BYTE*>(mShaders["skyPS"]->GetBufferPointer()),
			mShaders["skyPS"]->GetBufferSize()
		};
		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&skyPsoDesc, IID_PPV_ARGS(&mPSOs["sky"])));
	}

	//ibl map
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC diffuseIBLPsoDesc = opaquePSODesc;

		// 카메라에서 구의 안쪽을 렌더링하기 때문에 컬링을 꺼줍니다.
		diffuseIBLPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

		diffuseIBLPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		diffuseIBLPsoDesc.VS =
		{
			reinterpret_cast<BYTE*>(mShaders["diffuseIBLVS"]->GetBufferPointer()),
			mShaders["diffuseIBLVS"]->GetBufferSize()
		};
		diffuseIBLPsoDesc.PS =
		{
			reinterpret_cast<BYTE*>(mShaders["diffuseIBLPS"]->GetBufferPointer()),
			mShaders["diffuseIBLPS"]->GetBufferSize()
		};
		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&diffuseIBLPsoDesc, IID_PPV_ARGS(&mPSOs["diffuseIBL"])));
	}


	//blur 
	{
		D3D12_COMPUTE_PIPELINE_STATE_DESC horzBlurPSO = {};
		horzBlurPSO.pRootSignature = mPostProcessRootSignature.Get();
		horzBlurPSO.CS =
		{
			reinterpret_cast<BYTE*>(mShaders["blurH_CS"]->GetBufferPointer()),
			mShaders["blurH_CS"]->GetBufferSize()
		};
		horzBlurPSO.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		ThrowIfFailed(md3dDevice->CreateComputePipelineState(&horzBlurPSO, IID_PPV_ARGS(&mPSOs["blurH"])));

		D3D12_COMPUTE_PIPELINE_STATE_DESC vertBlurPSO = {};
		vertBlurPSO.pRootSignature = mPostProcessRootSignature.Get();
		vertBlurPSO.CS =
		{
			reinterpret_cast<BYTE*>(mShaders["blurV_CS"]->GetBufferPointer()),
			mShaders["blurV_CS"]->GetBufferSize()
		};
		vertBlurPSO.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		ThrowIfFailed(md3dDevice->CreateComputePipelineState(&vertBlurPSO, IID_PPV_ARGS(&mPSOs["blurV"])));
	}
}

void DemoApp::BuildFrameResources()
{
	for (int i = 0; i < GraphicsUtil::gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(
			md3dDevice.Get(), 1, (UINT)mAllRitems.size(), (UINT)mMaterials.size()));
	}
}

void DemoApp::BuildMaterials()
{
	int matCBIndex = 0;

	auto sky = std::make_unique<Material>();
	sky->Name = "sky";
	sky->MatCBIndex = matCBIndex++;
	sky->DiffuseSrvHeapIndex = mTextures["skyTex"]->heapIndex;
	sky->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	sky->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	sky->Roughness = 1.0f;

	
	auto box = std::make_unique<Material>();
	box->Name = "box";
	box->MatCBIndex = matCBIndex++;
	box->DiffuseAlbedo = XMFLOAT4(0.2f, 0.6f, 0.2f, 1.0f);
	box->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	box->Roughness = 0.125f;
	box->DiffuseSrvHeapIndex = -1;

	auto grass = std::make_unique<Material>();
	grass->Name = "grass";
	grass->MatCBIndex = matCBIndex++;
	grass->DiffuseSrvHeapIndex = mTextures["grassTex"]->heapIndex;

	auto cylinder = std::make_unique<Material>();
	cylinder->Name = "cylinder";
	cylinder->MatCBIndex = matCBIndex++;
	cylinder->DiffuseSrvHeapIndex = -1;

	auto sphere = std::make_unique<Material>();
	sphere->Name = "sphere";
	sphere->MatCBIndex = matCBIndex++;
	sphere->DiffuseSrvHeapIndex = -1;

	mMaterials["box"] = std::move(box);
	mMaterials["grass"] = std::move(grass);
	mMaterials["cylinder"] = std::move(cylinder);
	mMaterials["sphere"] = std::move(sphere);
	mMaterials["sky"] = std::move(sky);
}

void DemoApp::BuildRenderItems()
{
	UINT objCBIndex = 0;

	auto skyRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&skyRitem->World, XMMatrixScaling(5000.0f, 5000.0f, 5000.0f));
	skyRitem->TexTransform = GraphicsUtil::Identity4x4();
	skyRitem->ObjCBIndex = objCBIndex++;
	skyRitem->Mat = mMaterials["sky"].get();
	skyRitem->Geo = mGeometries["shapeGeo"].get();
	skyRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	skyRitem->IndexCount = skyRitem->Geo->DrawArgs["sphere"].IndexCount;
	skyRitem->StartIndexLocation = skyRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	skyRitem->BaseVertexLocation = skyRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Sky].push_back(skyRitem.get());
	mAllRitems.push_back(std::move(skyRitem));


	auto boxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, -3.0f));
	boxRitem->ObjCBIndex = objCBIndex++;
	boxRitem->Geo = mGeometries["shapeGeo"].get();
	boxRitem->Mat = mMaterials["box"].get();
	boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = (UINT)boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Opaque].push_back(boxRitem.get());
	mAllRitems.push_back(std::move(boxRitem));


	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->ObjCBIndex = objCBIndex++;
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->Mat = mMaterials["grass"].get();
	gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());
	mAllRitems.push_back(std::move(gridRitem));

	for (int i = 0; i < 5; ++i)
	{
		auto leftCylRitem = std::make_unique<RenderItem>();
		auto rightCylRitem = std::make_unique<RenderItem>();
		auto leftSphereRitem = std::make_unique<RenderItem>();
		auto rightSphereRitem = std::make_unique<RenderItem>();

		XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
		XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);

		XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
		XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

		XMStoreFloat4x4(&leftCylRitem->World, rightCylWorld);
		leftCylRitem->ObjCBIndex = objCBIndex++;
		leftCylRitem->Geo = mGeometries["shapeGeo"].get();
		leftCylRitem->Mat = mMaterials["cylinder"].get();
		leftCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);
		rightCylRitem->ObjCBIndex = objCBIndex++;
		rightCylRitem->Geo = mGeometries["shapeGeo"].get();
		rightCylRitem->Mat = mMaterials["cylinder"].get();
		rightCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
		leftSphereRitem->ObjCBIndex = objCBIndex++;
		leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
		leftSphereRitem->Mat = mMaterials["sphere"].get();
		leftSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
		rightSphereRitem->ObjCBIndex = objCBIndex++;
		rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
		rightSphereRitem->Mat = mMaterials["sphere"].get();
		rightSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftCylRitem.get());
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightCylRitem.get());
		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftSphereRitem.get());
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightSphereRitem.get());

		mAllRitems.push_back(std::move(leftCylRitem));
		mAllRitems.push_back(std::move(rightCylRitem));
		mAllRitems.push_back(std::move(leftSphereRitem));
		mAllRitems.push_back(std::move(rightSphereRitem));
	}
}

void DemoApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems, ID3D12Resource* objectCB)
{
	UINT objCBByteSize = GraphicsUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	ID3D12Resource* objCB = (objectCB == nullptr) ? mCurrFrameResource->ObjectCB->Resource() : objectCB;

	// 각 렌더 항목에 대해서...
	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];

		auto vertexVIew = ri->Geo->VertexBufferView();
		cmdList->IASetVertexBuffers(0, 1, &vertexVIew);
		auto indexView = ri->Geo->IndexBufferView();
		cmdList->IASetIndexBuffer(&indexView);
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

void DemoApp::BakeIrradianceMap()
{
}
