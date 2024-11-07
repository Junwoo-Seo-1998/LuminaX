#pragma once
#include "GraphicsUtil.h"
#include "directx/d3dx12.h"
class BlurFilter
{
public:
    // �ʺ�� ���̴� ������ ������ �Է� �ؽ�ó�� ũ��� �����ؾ��մϴ�.
    // ��ũ�� ũ�Ⱑ ����Ǹ� �ٽ� �����ؾ��մϴ�.
    BlurFilter(ID3D12Device* device,
        UINT width, UINT height,
        DXGI_FORMAT format);
    BlurFilter(const BlurFilter& rhs) = delete;
    BlurFilter& operator=(const BlurFilter& rhs) = delete;

    ID3D12Resource* Output();

    void BuildDescriptors(
        CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDescriptor,
        CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuDescriptor,
        UINT descriptorSize);

    void OnResize(UINT newWidth, UINT newHeight);

    // �Է� �ؽ�ó�� blurCount��ŭ ������ �ݺ��մϴ�.
    void Execute(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12RootSignature* rootSig,
        ID3D12PipelineState* horzBlurPSO,
        ID3D12PipelineState* vertBlurPSO,
        ID3D12Resource* input,
        int blurCount);

private:
    std::vector<float> CalcGaussWeights(float sigma);

    void BuildDescriptors();
    void BuildResources();

private:
    const int MaxBlurRadius = 5;

    ID3D12Device* md3dDevice = nullptr;

    UINT mWidth = 0;
    UINT mHeight = 0;
    DXGI_FORMAT mFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    CD3DX12_CPU_DESCRIPTOR_HANDLE mBlur0CpuSrv;
    CD3DX12_CPU_DESCRIPTOR_HANDLE mBlur0CpuUav;

    CD3DX12_CPU_DESCRIPTOR_HANDLE mBlur1CpuSrv;
    CD3DX12_CPU_DESCRIPTOR_HANDLE mBlur1CpuUav;

    CD3DX12_GPU_DESCRIPTOR_HANDLE mBlur0GpuSrv;
    CD3DX12_GPU_DESCRIPTOR_HANDLE mBlur0GpuUav;

    CD3DX12_GPU_DESCRIPTOR_HANDLE mBlur1GpuSrv;
    CD3DX12_GPU_DESCRIPTOR_HANDLE mBlur1GpuUav;

    Microsoft::WRL::ComPtr<ID3D12Resource> mBlurMap0 = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> mBlurMap1 = nullptr;
};