#include "CubeRenderTarget.h"
#include "Application.h"
#include "GraphicsUtil.h"

CubeRenderTarget::CubeRenderTarget(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format)
    :md3dDevice(device), mWidth(width), mHeight(height), mFormat(format)
{
    mViewport = { 0.f, 0.f, (float)width, (float)height, 0.f, 1.f };
    mScissorRect = { 0, 0, (LONG)width, (LONG)height };

    BuildResource();
}

ID3D12Resource* CubeRenderTarget::Resource()
{
    return mCubeMap.Get();
}

CD3DX12_GPU_DESCRIPTOR_HANDLE CubeRenderTarget::Srv()
{
    return mhGpuSrv;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE CubeRenderTarget::Rtv(int faceIndex)
{
    return mhCpuRtv[faceIndex];
}

D3D12_VIEWPORT CubeRenderTarget::Viewport() const
{
    return mViewport;
}

D3D12_RECT CubeRenderTarget::ScissorRect() const
{
    return mScissorRect;
}

void CubeRenderTarget::BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv, CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv, CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuRtv[6])
{
    // 디스크립터의 레퍼런스를 저장합니다.
    mhCpuSrv = hCpuSrv;
    mhGpuSrv = hGpuSrv;

    for (int i = 0; i < 6; ++i)
        mhCpuRtv[i] = hCpuRtv[i];

    // 디스크립터를 생성합니다.
    BuildDescriptors();
}

void CubeRenderTarget::OnResize(UINT newWidth, UINT newHeight)
{
    if ((mWidth != newWidth) || (mHeight != newHeight))
    {
        mWidth = newWidth;
        mHeight = newHeight;

        BuildResource();
        BuildDescriptors();
    }
}

void CubeRenderTarget::BuildDescriptors()
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = mFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MostDetailedMip = 0;
    srvDesc.TextureCube.MipLevels = 1;
    srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

    md3dDevice->CreateShaderResourceView(mCubeMap.Get(), &srvDesc, mhCpuSrv);

    for (int i = 0; i < 6; ++i)
    {
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        rtvDesc.Format = mFormat;
        rtvDesc.Texture2DArray.MipSlice = 0;
        rtvDesc.Texture2DArray.PlaneSlice = 0;

        // 렌더 타겟을 i 번째를 위해 설정합니다.
        rtvDesc.Texture2DArray.FirstArraySlice = i;

        // 한 어레이만 허용합니다.
        rtvDesc.Texture2DArray.ArraySize = 1;

        // i 번째 렌더 타겟 뷰를 생성합니다.
        md3dDevice->CreateRenderTargetView(mCubeMap.Get(), &rtvDesc, mhCpuRtv[i]);
    }
}

void CubeRenderTarget::BuildResource()
{
    D3D12_RESOURCE_DESC texDesc;
    ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Alignment = 0;
    texDesc.Width = mWidth;
    texDesc.Height = mHeight;
    texDesc.DepthOrArraySize = 6;
    texDesc.MipLevels = 1;
    texDesc.Format = mFormat;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    auto heapProperty = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &heapProperty,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mCubeMap)));
}
