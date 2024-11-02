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

    // �ý��� �޸� ���纻�Դϴ�. ���ؽ��� �ε��� ������ ����ϱ� ������ ����� ����մϴ�.
    // �����ڰ� �ùٸ��� �ɽ��� �ؾ��մϴ�.
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
    float FalloffStart = 1.0;                         // Point, Spot ����Ʈ�� ����մϴ�.
    DirectX::XMFLOAT3 Direction = { 0.0, -1.0f, 0.0f }; // Directional, Spot ����Ʈ�� ����մϴ�.
    float FalloffEnd = 10.0f;                         // Point, Spot ����Ʈ�� ����մϴ�.
    DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };  // Point, Spot ����Ʈ�� ����մϴ�.
    float SpotPower = 64.0f;                          // Spot ����Ʈ�� ����մϴ�.
};

#define MaxLights 16

// �������� ����ϱ� ���� ������ ���͸��� ����ü�Դϴ�.
// ���� 3D ������ ���� ���͸������ ��ӹ������ν� �����˴ϴ�.
struct Material
{
    // �˻��� ���� ������ ���͸��� �̸��Դϴ�.
    std::string Name;

    // �� ���͸��� �ش�Ǵ� ��� ������ �ε����Դϴ�.
    int MatCBIndex;

    // ��ǻ�� �ؽ��Ŀ� �ش��ϴ� SRV ���� �ε����Դϴ�.
    int DiffuseSrvHeapIndex = -1;

    // �븻 �ؽ��Ŀ� �ش��ϴ� SRV ���� �ε����Դϴ�.
    int NormalSrvHeapIndex = -1;

    // ���͸����� ����Ǿ��ٴ� ���� ��Ÿ���� ��Ƽ �÷����Դϴ�.
    // �׸��� ������� ��� ��� ���۸� ������Ʈ �ؾ��մϴ�.
    // ���͸��� ��� ���۴� �� �����Ӹ��� �����ϱ� ������ ��� ������ ���ҽ���
    // ������Ʈ �ؾ��մϴ�. �׷��Ƿ� ���͸����� ������� �� NumFramesDirty = gNumFrameResources��
    // �����ؼ� ��� ������ ���ҽ��� ������Ʈ �ǵ��� �ؾ��մϴ�.
    int NumFramesDirty = GraphicsUtil::gNumFrameResources;

    // ���̵��� ���Ǵ� ���͸��� ��� ���� �������Դϴ�.
    DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
    float Roughness = 0.25f;
    DirectX::XMFLOAT4X4 MatTransform = GraphicsUtil::Identity4x4();
};

struct Texture
{
    // �˻��� ���� ������ ���͸��� �̸��Դϴ�.
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