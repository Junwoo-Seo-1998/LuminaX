#include "Buffers.h"
#include "directx/d3dx12.h"
using namespace Microsoft::WRL;
Microsoft::WRL::ComPtr<ID3D12Resource> Buffers::CreateDefaultBuffer(
	ID3D12Device* device,
	ID3D12GraphicsCommandList* cmdList,
	const void* initData,
	UINT64 byteSize,
	Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer
)
{
	ComPtr<ID3D12Resource> defaultBuffer;
	auto properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
	device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE,
		&bufferDesc, D3D12_RESOURCE_STATE_COMMON, nullptr,
		IID_PPV_ARGS(defaultBuffer.GetAddressOf()));

	//temp heap for init data
	properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE,
		&bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(uploadBuffer.GetAddressOf()));

	D3D12_SUBRESOURCE_DATA subResourceData;
	subResourceData.pData = initData;
	subResourceData.RowPitch = static_cast<LONG_PTR>(byteSize);
	subResourceData.SlicePitch = subResourceData.RowPitch;

	//barrier
	auto commonToDest =CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
	cmdList->ResourceBarrier(1, &commonToDest);

	UpdateSubresources<1>(cmdList, defaultBuffer.Get(), uploadBuffer.Get(),
		0, 0, 1, &subResourceData);

	auto destToRead = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
	cmdList->ResourceBarrier(1, &destToRead);

	return defaultBuffer;
}

