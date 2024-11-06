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

	// �ʱ�ȭ ��ɵ��� ����ϱ� ���� Ŀ�ǵ� ����Ʈ�� �����մϴ�.
	ThrowIfFailed(mCommandList->Reset(mCommandListAlloc.Get(), nullptr));

	mBlurFilter = std::make_unique<BlurFilter>(md3dDevice.Get(),
		mWidth,
		mHeight,
		DXGI_FORMAT_R8G8B8A8_UNORM);

	LoadTextures();
	BuildDescHeaps();
	BuildDescViews();
	BuildRootSignature();
	BuildPostProcessRootSignature();
	BuildShaderAndInputLayout();
	BuildShapeGeometry();
	BuildMaterials();
	BuildRenderItems();
	BuildFrameResources();
	BuildConstantBuffers();
	BuildPSO();

	// �ʱ�ȭ ��ɵ��� �����ŵ�ϴ�.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(1, cmdLists);

	// �ʱ�ȭ�� ����� ������ ��ٸ��ϴ�.
	FlushCommandQueue();

	return true;
}

void DemoApp::OnResize()
{
	Application::OnResize();
	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * DirectX::XM_PI, (float)mWidth/mHeight , 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);

	if (mBlurFilter != nullptr)
	{
		mBlurFilter->OnResize(mWidth, mHeight);
	}
}

void DemoApp::Update()
{
	UpdateCamera();

	// ���� ������ ���ҽ��� �ڿ��� ������� ��ȯ�մϴ�.
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % GraphicsUtil::gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	// ���� ������ ���ҽ��� ���� ��ɵ��� GPU���� ó�� �Ǿ����ϱ�?
	// ó������ �ʾҴٸ� Ŀ�ǵ���� �潺 �������� GPU�� ó���� ������ ��ٷ����մϴ�.
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

	// Ŀ�ǵ� ����� ���� �޸𸮸� ��Ȱ�� �մϴ�.
	// ������ Ŀ�ǵ���� GPU���� ��� �������� ������ �� �ֽ��ϴ�.
	ThrowIfFailed(cmdListAlloc->Reset());

	// ExecuteCommandList�� ���� Ŀ�ǵ� ť�� ������ ������ Ŀ�ǵ� ����Ʈ�� ������ �� �ֽ��ϴ�.
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

	// ���ҽ��� ���¸� �������� �� �� �ֵ��� �����մϴ�.
	{
		auto transition = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET);
		mCommandList->ResourceBarrier(1, &transition);
	}
	// �� ���ۿ� ���� ���۸� Ŭ���� �մϴ�.
	mCommandList->ClearRenderTargetView(GetCurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(GetDepthStencilBufferView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// ��� �������� ���� �����մϴ�.
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
	skyTexDescriptor.Offset((int)mTextures.size() - 1, mCbvSrvUavDescSize);
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

	// ���ҽ��� ���¸� ����� �� �ֵ��� �����մϴ�.
	{
		auto transition = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PRESENT);
		mCommandList->ResourceBarrier(1, &transition);
	}
	// Ŀ�ǵ� ����� �����մϴ�.
	ThrowIfFailed(mCommandList->Close());

	// Ŀ�ǵ� ����Ʈ�� ������ ���� ť�� �����մϴ�.
	ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(1, cmdLists);

	// �� ���ۿ� ����Ʈ ���۸� ��ü�մϴ�.
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % 2;

	// �� �潺 �������� Ŀ�ǵ���� ǥ���ϱ� ���� �潺 ���� �����մϴ�,
	mCurrFrameResource->Fence = ++mCurrentFence;

	// �� �潺 ������ �����ϴ� �ν�Ʈ������ Ŀ�ǵ� ť�� �߰��մϴ�.
	// ���ø����̼��� GPU �ð��࿡ ���� �ʱ� ������,
	// GPU�� ��� Ŀ�ǵ���� ó���� �Ϸ�Ǳ� ������ Signal()�� ó������ �ʽ��ϴ�.
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
		// ���콺 �� �ȼ��� �̵��� 0.25���� ������ŵ�ϴ�.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		// �Է¿� ���ʿ� ������ �����ؼ� ī�޶� ���� �߽����� �����ϰ� �մϴ�.
		mTheta += dx;
		mPhi += dy;

		// mPhi�� ������ �����մϴ�.
		mPhi = std::clamp(mPhi, 0.1f, (float)std::numbers::pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		// ���콺 �� �ȼ��� �̵��� 0.005 ������ ������ŵ�ϴ�.
		float dx = 0.2f * static_cast<float>(x - mLastMousePos.x);
		float dy = 0.2f * static_cast<float>(y - mLastMousePos.y);

		// �η¿� ���� ī�޶��� �������� ������Ʈ �մϴ�.
		mRadius += dx - dy;

		// �������� �����մϴ�.
		mRadius = std::clamp(mRadius, 5.0f, 150.0f);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
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
	// �� ��ǥ�踦 īŸ�þ� ��ǥ��� ��ȯ�մϴ�.
	mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
	mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
	mEyePos.y = mRadius * cosf(mPhi);

	// �� ��Ʈ������ ����մϴ�.
	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}

void DemoApp::UpdateObjectCBs()
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		// ������� �ٲ�� ���� ��� ���� �����͸� ������Ʈ �մϴ�.
		// �̰��� �� ������ �ڿ����� �����ؾ� �մϴ�.
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
			objConstants.MaterialIndex = e->Mat->MatCBIndex;

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// ���� ������ ���ҽ��� ���������� ������Ʈ �Ǿ�� �մϴ�.
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
	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);

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
	mMainPassCB.EyePosW = mEyePos;
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
	// ���ø����̼��� ���� ��� ���÷��� �ʿ��մϴ�. �׷��� ���� ���Ǵ� ���÷� ��� �����ϰ�
	// ��Ʈ �ñ׳����� �Ϻκ����� �����մϴ�.

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

void DemoApp::BuildDescHeaps()
{
	const UINT textureDescriptorCount = (UINT)mTextures.size();
	const UINT blurDescriptorCount = (UINT)4;

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = textureDescriptorCount + blurDescriptorCount;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mGeneralDescHeap)));
}

void DemoApp::BuildConstantBuffers()
{
	/*UINT passCBByteSize = (sizeof(PassConstants) + 255) & (~255);

	// ������ ������ ��ũ���͵��� �� �������� ���� �н� CBV�Դϴ�.
	for (int frameIndex = 0; frameIndex < GraphicsUtil::gNumFrameResources; ++frameIndex)
	{
		auto passCB = mFrameResources[frameIndex]->PassCB->Resource();


		D3D12_GPU_VIRTUAL_ADDRESS passCBAddr = passCB->GetGPUVirtualAddress();

		int heapIndex = mPassCbvOffset + frameIndex;
		auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
		handle.Offset(heapIndex, mCbvSrvUavDescSize);

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
		cbvDesc.BufferLocation = passCBAddr;
		cbvDesc.SizeInBytes = passCBByteSize;
		md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
	}*/
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
	mTextures["skyTex"]->heapIndex = index;

	mBlurFilter->BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE(mGeneralDescHeap->GetCPUDescriptorHandleForHeapStart(), (int)mTextures.size(), mCbvSrvUavDescSize),
		CD3DX12_GPU_DESCRIPTOR_HANDLE(mGeneralDescHeap->GetGPUDescriptorHandleForHeapStart(), (int)mTextures.size(), mCbvSrvUavDescSize),
		mCbvSrvUavDescSize);
}

void DemoApp::BuildRootSignature()
{
	CD3DX12_ROOT_PARAMETER slotRootParams[5];

	//texture
	CD3DX12_DESCRIPTOR_RANGE texSky;
	texSky.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);

	CD3DX12_DESCRIPTOR_RANGE tex;
	tex.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 1, 0);

	slotRootParams[0].InitAsConstantBufferView(0);
	slotRootParams[1].InitAsConstantBufferView(1);
	slotRootParams[2].InitAsShaderResourceView(0, 1);
	slotRootParams[3].InitAsDescriptorTable(1, &texSky, D3D12_SHADER_VISIBILITY_PIXEL);
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
	// ��� ������Ʈ���� �ϳ��� ū ���ؽ�/�ε��� ���ۿ� �����ؼ� �����մϴ�.
	// �׷��Ƿ� ������ ����޽��� ���ۿ��� �����ϴ� ������ �����մϴ�.
	//

	// ����� ���� ���ۿ��� �� ������Ʈ�� ���ؽ� �����µ��� ĳ���մϴ�.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

	// ����� �ε��� ���ۿ��� �� ������Ʈ�� ���� �ε����� ĳ���մϴ�.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

	// ���ؽ�/�ε��� ���ۿ��� �� ������ �����ϴ� ����޽� ������Ʈ���� �����մϴ�.

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
	// �ʿ��� ���ؽ� ������Ʈ���� �����ϰ�
	// ��� �޽��� ���ؽ��� �� ���ؽ� ���ۿ� �����մϴ�.
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

		// ī�޶󿡼� ���� ������ �������ϱ� ������ �ø��� ���ݴϴ�.
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
			md3dDevice.Get(), 1, (UINT)mAllRitems.size()));
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

void DemoApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = GraphicsUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	auto objCB = mCurrFrameResource->ObjectCB->Resource();
	// �� ���� �׸� ���ؼ�...
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
