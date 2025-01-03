#pragma once
#include <wrl/client.h>
#include "directx/d3d12.h"
#include <memory>

#include "Buffers.h"


struct PassConstants;
struct ObjectConstants;
struct MaterialData;

class FrameResource
{
public:
	FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount);
	FrameResource(const FrameResource& right) = delete;
	FrameResource& operator=(const FrameResource& right) = delete;


	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc;
	std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
	std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;

	std::unique_ptr<UploadBuffer<MaterialData>> MaterialBuffer = nullptr;

	UINT64 Fence = 0;
};
