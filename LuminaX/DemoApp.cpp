#include "DemoApp.h"

#include <d3dcompiler.h>

#include "directx/d3dx12.h"
#include "DirectXMath.h"
#include "DirectXColors.h"
#include "GraphicsUtil.h"
#include "MeshGenerator.h"
#include "Buffers.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
//using namespace DirectX::PackedVector;

const int GraphicsUtil::gNumFrameResources = 3;

struct Vertex
{
	XMFLOAT3 Pos;
	XMFLOAT4 Color;
};


bool DemoApp::Init(HINSTANCE hinstance)
{
	if (!Application::Init(hinstance))
		return false;

	// 초기화 명령들을 기록하기 위해 커맨드 리스트를 리셋합니다.
	ThrowIfFailed(mCommandList->Reset(mCommandListAlloc.Get(), nullptr));

	BuildRootSignature();
	BuildShaderAndInputLayout();
	BuildShapeGeometry();
	BuildRenderItems();
	BuildFrameResources();
	BuildDescHeaps();
	BuildConstantBuffers();
	BuildPSO();

	// 초기화 명령들을 실행시킵니다.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(1, cmdLists);

	// 초기화가 종료될 때가지 기다립니다.
	FlushCommandQueue();

	return true;
}

void DemoApp::OnResize()
{
	Application::OnResize();
	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * DirectX::XM_PI, (float)mWidth/mHeight , 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
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
	UpdateMainPassCB();
}

void DemoApp::Draw()
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	// 커맨드 기록을 위한 메모리를 재활용 합니다.
	// 제출한 커맨드들이 GPU에서 모두 끝났을때 리셋할 수 있습니다.
	ThrowIfFailed(cmdListAlloc->Reset());

	// ExecuteCommandList를 통해 커맨드 큐에 제출한 다음에 커맨드 리스트를 리셋할 수 있습니다.
	if (true)
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
	ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSig.Get());

	int passCbvIndex = mPassCbvOffset + mCurrFrameResourceIndex;
	auto passCbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
	passCbvHandle.Offset(passCbvIndex, mCbvSrvUavDescSize);
	mCommandList->SetGraphicsRootDescriptorTable(1, passCbvHandle);

	DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

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

void DemoApp::UpdateCamera()
{
	// 구 좌표계를 카타시안 좌표계로 변환합니다.
	mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
	mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
	mEyePos.y = mRadius * cosf(mPhi);

	// 뷰 메트릭스를 계산합니다.
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
		// 상수들이 바뀌었 때만 상수 버퍼 데이터를 업데이트 합니다.
		// 이것은 매 프레임 자원마다 수행해야 합니다.
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// 다음 프레임 리소스도 마찬가지로 업데이트 되어야 합니다.
			e->NumFramesDirty--;
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

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void DemoApp::BuildDescHeaps()
{
	UINT objCount=(UINT)mOpaqueRitems.size();

	// 프레임 마다 각 오브젝트의 CBV가 필요하고
	// 프레임 마다 한 패스의 CBV가 필요합니다.
	UINT numDescriptors = (objCount + 1) * GraphicsUtil::gNumFrameResources;
	mPassCbvOffset = objCount * GraphicsUtil::gNumFrameResources;

	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.NodeMask = 0;
	cbvHeapDesc.NumDescriptors = numDescriptors;

	md3dDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&mCbvHeap));
}

void DemoApp::BuildConstantBuffers()
{
	UINT objCBByteSize = (sizeof(ObjectConstants) + 255) & (~255);

	UINT objCount = (UINT)mOpaqueRitems.size();

	// 매 프레임마다 각 오브젝트의 CBV가 필요합니다.
	for (int frameIndex = 0; frameIndex < GraphicsUtil::gNumFrameResources; ++frameIndex)
	{
		auto objectCB = mFrameResources[frameIndex]->ObjectCB->Resource();

		for (UINT i = 0; i < objCount; ++i)
		{
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress = objectCB->GetGPUVirtualAddress();
			cbAddress += i * objCBByteSize;

			int heapIndex = frameIndex * objCount + i;
			auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
			handle.Offset(heapIndex, mCbvSrvUavDescSize);

			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
			cbvDesc.BufferLocation = cbAddress;
			cbvDesc.SizeInBytes = objCBByteSize;
			md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
		}
	}
	
	UINT passCBByteSize = (sizeof(PassConstants) + 255) & (~255);

	// 마지막 세개의 디스크립터들은 매 프레임을 위한 패스 CBV입니다.
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
	}
}

void DemoApp::BuildRootSignature()
{
	CD3DX12_ROOT_PARAMETER slotRootParams[2];

	//CBV desc table
	CD3DX12_DESCRIPTOR_RANGE cbvTable;
	cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE cbvTable2;
	cbvTable2.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);

	slotRootParams[0].InitAsDescriptorTable(1, &cbvTable);
	slotRootParams[1].InitAsDescriptorTable(1, &cbvTable2);


	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParams, 0, nullptr,
	                                        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;


	HRESULT result = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
	                                             serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	md3dDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&mRootSig));
}

void DemoApp::BuildShaderAndInputLayout()
{
	HRESULT result = S_OK;

	mVSByteCode = GraphicsUtil::CompileShader(L"Shaders/color.hlsl", nullptr, "VS", "vs_5_0");
	mPSByteCode = GraphicsUtil::CompileShader(L"Shaders/color.hlsl", nullptr, "PS", "ps_5_0");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
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
		vertices[k].Color = XMFLOAT4(DirectX::Colors::DarkGreen);
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::ForestGreen);
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Crimson);
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::SteelBlue);
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
		reinterpret_cast<BYTE*>(mVSByteCode->GetBufferPointer()),
		mVSByteCode->GetBufferSize()
	};

	opaquePSODesc.PS = {
		reinterpret_cast<BYTE*>(mPSByteCode->GetBufferPointer()),
		mPSByteCode->GetBufferSize()
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

	md3dDevice->CreateGraphicsPipelineState(&opaquePSODesc, IID_PPV_ARGS(&mPSOs["opaque"]));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueWireframePsoDesc = opaquePSODesc;
	opaqueWireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	md3dDevice->CreateGraphicsPipelineState(&opaqueWireframePsoDesc, IID_PPV_ARGS(&mPSOs["opaque_wireframe"]));
}

void DemoApp::BuildFrameResources()
{
	for (int i = 0; i < GraphicsUtil::gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(
			md3dDevice.Get(), 1, (UINT)mAllRitems.size()));
	}
}

void DemoApp::BuildRenderItems()
{
	auto boxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, -3.0f));
	boxRitem->ObjCBIndex = 0;
	boxRitem->Geo = mGeometries["shapeGeo"].get();
	boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = (UINT)boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(boxRitem));


	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->ObjCBIndex = 1;
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(gridRitem));

	UINT objCBIndex = 2;
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
		leftCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);
		rightCylRitem->ObjCBIndex = objCBIndex++;
		rightCylRitem->Geo = mGeometries["shapeGeo"].get();
		rightCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
		leftSphereRitem->ObjCBIndex = objCBIndex++;
		leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
		leftSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
		rightSphereRitem->ObjCBIndex = objCBIndex++;
		rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
		rightSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		mAllRitems.push_back(std::move(leftCylRitem));
		mAllRitems.push_back(std::move(rightCylRitem));
		mAllRitems.push_back(std::move(leftSphereRitem));
		mAllRitems.push_back(std::move(rightSphereRitem));
	}

	// 모든 렌더 아이템들은 불투명합니다. 일단은..
	for (auto& e : mAllRitems)
		mOpaqueRitems.push_back(e.get());
}

void DemoApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	// 각 렌더 항목에 대해서...
	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];

		auto vertexVIew = ri->Geo->VertexBufferView();
		cmdList->IASetVertexBuffers(0, 1, &vertexVIew);
		auto indexView = ri->Geo->IndexBufferView();
		cmdList->IASetIndexBuffer(&indexView);
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		// 현 프레임 리소스에 해당하는 오브젝트의 CBV의 오프셋을 계산합니다.
		UINT cbvIndex = mCurrFrameResourceIndex * (UINT)mOpaqueRitems.size() + ri->ObjCBIndex;
		auto cbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
		cbvHandle.Offset(cbvIndex, mCbvSrvUavDescSize);

		cmdList->SetGraphicsRootDescriptorTable(0, cbvHandle);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}
