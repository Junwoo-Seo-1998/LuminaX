#pragma once 
#include <DirectXMath.h>
#include <wrl/client.h>
#include "directx/d3d12.h"
#include <string>
#include <unordered_map>

class GraphicsUtil
{
public:
    static UINT CalcConstantBufferByteSize(UINT byteSize);
    

	static Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(const std::wstring& fileName, const D3D_SHADER_MACRO* defines,
	                                               const std::string& entryPoint, const std::string& target);
    static DirectX::XMFLOAT4X4 Identity4x4();
    static DirectX::XMVECTOR SphericalToCartesian(float radius, float theta, float phi);
    static const int gNumFrameResources;


    static bool LoadTextureFromFile(const std::wstring& fileName,
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        Microsoft::WRL::ComPtr<ID3D12Resource>& texture, Microsoft::WRL::ComPtr<ID3D12Resource>& textureUploadHeap);
};

// Defines a subrange of geometry in a MeshGeometry.  This is for when multiple
// geometries are stored in one vertex and index buffer.  It provides the offsets
// and data needed to draw a subset of geometry stores in the vertex and index 
// buffers so that we can implement the technique described by Figure 6.3.
struct SubmeshGeometry
{
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    INT BaseVertexLocation = 0;

    // Bounding box of the geometry defined by this submesh. 
    // This is used in later chapters of the book.
    // DirectX::BoundingBox Bounds;
};
struct MeshGeometry
{
    std::string Name;

    // 시스템 메모리 복사본입니다. 버텍스와 인덱스 포멧은 평범하기 때문에 블랍을 사용합니다.
    // 개발자가 올바르게 케스팅 해야합니다.
    Microsoft::WRL::ComPtr<ID3DBlob> VertexBufferCPU = nullptr;
    Microsoft::WRL::ComPtr<ID3DBlob> IndexBufferCPU = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferGPU = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferGPU = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferUploader = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferUploader = nullptr;

    // Data about the buffers.
    UINT VertexByteStride = 0;
    UINT VertexBufferByteSize = 0;
    DXGI_FORMAT IndexFormat = DXGI_FORMAT_R16_UINT;
    UINT IndexBufferByteSize = 0;

    // A MeshGeometry may store multiple geometries in one vertex/index buffer.
    // Use this container to define the Submesh geometries so we can draw
    // the Submeshes individually.
    std::unordered_map<std::string, SubmeshGeometry> DrawArgs;

    D3D12_VERTEX_BUFFER_VIEW VertexBufferView()const
    {
        D3D12_VERTEX_BUFFER_VIEW vbv;
        vbv.BufferLocation = VertexBufferGPU->GetGPUVirtualAddress();
        vbv.StrideInBytes = VertexByteStride;
        vbv.SizeInBytes = VertexBufferByteSize;

        return vbv;
    }

    D3D12_INDEX_BUFFER_VIEW IndexBufferView()const
    {
        D3D12_INDEX_BUFFER_VIEW ibv;
        ibv.BufferLocation = IndexBufferGPU->GetGPUVirtualAddress();
        ibv.Format = IndexFormat;
        ibv.SizeInBytes = IndexBufferByteSize;

        return ibv;
    }

    // We can free this memory after we finish upload to the GPU.
    void DisposeUploaders()
    {
        VertexBufferUploader = nullptr;
        IndexBufferUploader = nullptr;
    }
};

struct Light
{
    DirectX::XMFLOAT3 Strength = { 0.5f, 0.5f, 0.5f };
    float FalloffStart = 1.0;                         // Point, Spot 라이트만 사용합니다.
    DirectX::XMFLOAT3 Direction = { 0.0, -1.0f, 0.0f }; // Directional, Spot 라이트만 사용합니다.
    float FalloffEnd = 10.0f;                         // Point, Spot 라이트만 사용합니다.
    DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };  // Point, Spot 라이트만 사용합니다.
    float SpotPower = 64.0f;                          // Spot 라이트만 사용합니다.
};

#define MaxLights 16

// 예제에서 사용하기 위한 간단한 메터리얼 구조체입니다.
// 실제 3D 엔진은 여러 메터리얼들을 상속받음으로써 생성됩니다.
struct Material
{
    // 검색을 위한 고유한 메터리얼 이름입니다.
    std::string Name;

    // 이 메터리얼에 해당되는 상수 버퍼의 인덱스입니다.
    int MatCBIndex;

    // 디퓨즈 텍스쳐에 해당하는 SRV 힙의 인덱스입니다.
    int DiffuseSrvHeapIndex = -1;

    // 노말 텍스쳐에 해당하는 SRV 힙의 인덱스입니다.
    int NormalSrvHeapIndex = -1;

    // 메터리얼이 변경되었다는 것을 나타내는 더티 플래그입니다.
    // 그리고 변경됬을 경우 상수 버퍼를 업데이트 해야합니다.
    // 메터리얼 상수 버퍼는 매 프레임마다 존재하기 때문에 모든 프레임 리소스를
    // 업데이트 해야합니다. 그러므로 메터리얼이 변경됬을 때 NumFramesDirty = gNumFrameResources로
    // 설정해서 모든 프레임 리소스가 업데이트 되도록 해야합니다.
    int NumFramesDirty = GraphicsUtil::gNumFrameResources;

    // 셰이딩에 사용되는 메터리얼 상수 버퍼 데이터입니다.
    DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
    float Roughness = 0.25f;
    DirectX::XMFLOAT4X4 MatTransform = GraphicsUtil::Identity4x4();
};

struct Texture
{
    // 검색을 위한 고유한 메터리얼 이름입니다.
    std::string Name;

    std::wstring Filename;

    int heapIndex = -1;

    Microsoft::WRL::ComPtr<ID3D12Resource> Resource = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> UploadHeap = nullptr;
};

inline std::wstring AnsiToWString(const std::string& str)
{
    WCHAR buffer[512];
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, buffer, 512);
    return std::wstring(buffer);
}

class DxException
{
public:
    DxException() = default;
    DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber);

    std::wstring ToString()const;

    HRESULT ErrorCode = S_OK;
    std::wstring FunctionName;
    std::wstring Filename;
    int LineNumber = -1;
};

#ifndef ThrowIfFailed
#define ThrowIfFailed(x)                                              \
{                                                                     \
    HRESULT hr__ = (x);                                               \
    std::wstring wfn = AnsiToWString(__FILE__);                       \
    if(FAILED(hr__)) { throw DxException(hr__, L#x, wfn, __LINE__); } \
}
#endif