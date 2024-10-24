#pragma once
#include <DirectXMath.h>

#include "Application.h"
#include <memory>
#include "Buffers.h"
#include "FrameResource.h"
#include "GraphicsUtil.h"

struct MeshGeometry;

struct RenderItem
{
    RenderItem() = default;

    // 월드 공간에서 도형의 위치, 회전, 스케일을 정의한 메트릭스입니다.
    DirectX::XMFLOAT4X4 World = DirectX::XMFLOAT4X4(
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f);

    // 오브젝트의 데이터가 변경됬는지를 나타내는 더티 플레그입니다.
    // 더티 플레그가 활성화되어 있으면 상수 버퍼를 업데이트 해줘야 합니다.
    // 매 프레임 자원마다 오브젝트 상수 버퍼가 존재하기 때문에
    // 오브젝트의 데이터가 변경됬을때 NumFramesDirty = gNumFrameResource로
    // 설정해야합니다. 이렇게 하므로써 모든 프레임 리소스가 업데이트가 됩니다.
    int NumFramesDirty = 3;//gNumFrameResources;

    // 렌더 아이템에 해당하는 물체 상수 버퍼의 인덱스 입니다.
    UINT ObjCBIndex = -1;

    MeshGeometry* Geo = nullptr;

    // 도형 토폴로지입니다.
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    // DrawIndexedInstance 파라미터들 입니다.
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;
};

struct ObjectConstants
{
	DirectX::XMFLOAT4X4 World = DirectX::XMFLOAT4X4(
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f);
};

struct PassConstants
{
	DirectX::XMFLOAT4X4 View = GraphicsUtil::Identity4x4();
	DirectX::XMFLOAT4X4 InvView = GraphicsUtil::Identity4x4();
	DirectX::XMFLOAT4X4 Proj = GraphicsUtil::Identity4x4();
	DirectX::XMFLOAT4X4 InvProj = GraphicsUtil::Identity4x4();
	DirectX::XMFLOAT4X4 ViewProj = GraphicsUtil::Identity4x4();
	DirectX::XMFLOAT4X4 InvViewProj = GraphicsUtil::Identity4x4();
	DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
	float cbPerObjectPad1 = 0.0f;
	DirectX::XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f };
	DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };
	float NearZ = 0.0f;
	float FarZ = 0.0f;
	float TotalTime = 0.0f;
	float DeltaTime = 0.0f;
};


class DemoApp :public Application
{
public:
	virtual bool Init(HINSTANCE hinstance) override;

private:
	virtual void OnResize() override;
	virtual void Update() override;
	virtual void Draw() override;
	
	void UpdateCamera();
	void UpdateObjectCBs();
	void UpdateMainPassCB();


	void BuildDescHeaps();
	void BuildConstantBuffers();
	void BuildRootSignature();
	void BuildShaderAndInputLayout();
    void BuildShapeGeometry();
    void BuildPSO();
	void BuildFrameResources();
	void BuildRenderItems();

	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);


private:
	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;

	PassConstants mMainPassCB;
	UINT mPassCbvOffset = 0;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mCbvHeap;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSig;

	Microsoft::WRL::ComPtr<ID3DBlob> mVSByteCode;
	Microsoft::WRL::ComPtr<ID3DBlob> mPSByteCode;
	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	std::vector<RenderItem*> mOpaqueRitems;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> mPSOs;


	DirectX::XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT4X4 mView = GraphicsUtil::Identity4x4();
	DirectX::XMFLOAT4X4 mProj = GraphicsUtil::Identity4x4();

	float mTheta = 1.5f * DirectX::XM_PI;
	float mPhi = 0.2f * DirectX::XM_PI;
	float mRadius = 15.0f;
};