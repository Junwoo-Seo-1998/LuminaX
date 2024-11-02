#include "GraphicsUtil.h"

#include <d3dcompiler.h>
#include <filesystem>
#include <Windows.h>
#include <comdef.h>

#include "ResourceUploadBatch.h"
#include "DDSTextureLoader.h"
#include "WICTextureLoader.h"
#include "directx/d3dx12.h"


UINT GraphicsUtil::CalcConstantBufferByteSize(UINT byteSize)
{
	// 상수 버퍼의 크기는 하드웨어의 최소 메모리 할당 크기에 배수가 되어야 한다.
	// 그러므로 256배의 수로 올림 해야한다. 이것을 255을 더하고
	// 256보다 작은 모든 비트를 제거함으로써 계산할 수 있다.
	// (300 + 255) & ~255
	// 555 & ~255
	// 0x022B & ~0x00ff
	// 0x022B & 0xff00
	// 0x0200
	// 512
	return (byteSize + 255) & ~255;
}

Microsoft::WRL::ComPtr<ID3DBlob> GraphicsUtil::CompileShader(const std::wstring& fileName,
                                                             const D3D_SHADER_MACRO* defines, const std::string& entryPoint, const std::string& target)
{
	Microsoft::WRL::ComPtr<ID3DBlob> byteCode;
	Microsoft::WRL::ComPtr<ID3DBlob> errorCode;
	if (!std::filesystem::exists(fileName))
	{
		std::wstring msg = L"File Not found - " + fileName;
		MessageBox(NULL, fileName.c_str(), L"Error", MB_OK);
	}
	UINT flags = 0;
#if defined(DEBUG) || defined(_DEBUG)
	flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
	D3DCompileFromFile(fileName.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE, entryPoint.c_str(), target.c_str(), flags, 0, &byteCode, &errorCode);

	if (errorCode != nullptr)
	{
		MessageBoxA(NULL, (char*)errorCode->GetBufferPointer(), "Error", MB_OK);
		OutputDebugStringA((char*)errorCode->GetBufferPointer());
	}
	return byteCode;
}

DirectX::XMFLOAT4X4 GraphicsUtil::Identity4x4()
{
	static DirectX::XMFLOAT4X4 I(
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f);

	return I;
}

DirectX::XMVECTOR GraphicsUtil::SphericalToCartesian(float radius, float theta, float phi)
{
	return DirectX::XMVectorSet(
		radius * sinf(phi) * cosf(theta),
		radius * cosf(phi),
		radius * sinf(phi) * sinf(theta),
		1.0f);
}

bool GraphicsUtil::LoadTextureFromFile(const std::wstring& fileName, ID3D12Device* device,
	ID3D12GraphicsCommandList* cmdList, Microsoft::WRL::ComPtr<ID3D12Resource>& texture,
	Microsoft::WRL::ComPtr<ID3D12Resource>& textureUploadHeap)
{
	if (!std::filesystem::exists(fileName))
		return false;

	
	if (std::filesystem::path(fileName).extension()==".dds")
	{
		std::unique_ptr<uint8_t[]> ddsData;
		std::vector<D3D12_SUBRESOURCE_DATA> subresources;
		ThrowIfFailed(DirectX::LoadDDSTextureFromFile(device, fileName.c_str(), texture.ReleaseAndGetAddressOf(),
			ddsData, subresources));

		const UINT64 uploadBufferSize = GetRequiredIntermediateSize(texture.Get(), 0,
			static_cast<UINT>(subresources.size()));

		auto debug = texture->GetDesc();
		CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
		auto desc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);

		ThrowIfFailed(device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(textureUploadHeap.GetAddressOf())));

		UpdateSubresources(cmdList, texture.Get(), textureUploadHeap.Get(),
			0, 0, static_cast<UINT>(subresources.size()), subresources.data());

		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		cmdList->ResourceBarrier(1, &barrier);
		return true;
	}

	std::unique_ptr<uint8_t[]> decodedData;
	D3D12_SUBRESOURCE_DATA subresource;
	ThrowIfFailed(
		DirectX::LoadWICTextureFromFile(device, fileName.c_str(), texture.ReleaseAndGetAddressOf(),
			decodedData, subresource));

	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(texture.Get(), 0, 1);

	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);

	auto desc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);

	ThrowIfFailed(
		device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(textureUploadHeap.GetAddressOf())));

	UpdateSubresources(cmdList, texture.Get(), textureUploadHeap.Get(),
		0, 0, 1, &subresource);

	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	cmdList->ResourceBarrier(1, &barrier);

	return true;;
}

DxException::DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber)
	: ErrorCode(hr), FunctionName(functionName), Filename(filename), LineNumber(lineNumber)
{}

std::wstring DxException::ToString() const
{
	// 에러 코드에 대한 설명을 가져옵니다.
	_com_error err(ErrorCode);
	std::wstring msg = err.ErrorMessage();

	return FunctionName + L" failed in " + Filename + L"; line " + std::to_wstring(LineNumber) + L"; error: " + msg;
}
