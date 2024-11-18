#pragma once
#include <DirectXMath.h>

#include "Application.h"
#include <memory>
#include <array>
#include "Buffers.h"
#include "FrameResource.h"
#include "GraphicsUtil.h"
#include "BlurFilter.h"
#include "Camera.h"
#include "CubeRenderTarget.h"
struct MeshGeometry;

struct RenderItem
{
    RenderItem() = default;

    // ���� �������� ������ ��ġ, ȸ��, �������� ������ ��Ʈ�����Դϴ�.
    DirectX::XMFLOAT4X4 World = DirectX::XMFLOAT4X4(
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f);

    DirectX::XMFLOAT4X4 TexTransform = GraphicsUtil::Identity4x4();

    // ������Ʈ�� �����Ͱ� ���������� ��Ÿ���� ��Ƽ �÷����Դϴ�.
    // ��Ƽ �÷��װ� Ȱ��ȭ�Ǿ� ������ ��� ���۸� ������Ʈ ����� �մϴ�.
    // �� ������ �ڿ����� ������Ʈ ��� ���۰� �����ϱ� ������
    // ������Ʈ�� �����Ͱ� ��������� NumFramesDirty = gNumFrameResource��
    // �����ؾ��մϴ�. �̷��� �ϹǷν� ��� ������ ���ҽ��� ������Ʈ�� �˴ϴ�.
    int NumFramesDirty = 3;//gNumFrameResources;

    // ���� �����ۿ� �ش��ϴ� ��ü ��� ������ �ε��� �Դϴ�.
    UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
    MeshGeometry* Geo = nullptr;

    // ���� ���������Դϴ�.
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    // DrawIndexedInstance �Ķ���͵� �Դϴ�.
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

	DirectX::XMFLOAT4X4 TexTransform = GraphicsUtil::Identity4x4();
	UINT MaterialIndex;
	UINT ObjPad0;
	UINT ObjPad1;
	UINT ObjPad2;
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

	DirectX::XMFLOAT4 AmbientLight = { 0.0f, 0.0f, 0.0f, 1.0f };

	// Indices [0, NUM_DIR_LIGHTS) are directional lights;
	// indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
	// indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)
	// are spot lights for a maximum of MaxLights per object.
	Light Lights[MaxLights];
};


struct MaterialData
{
	DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
	float Roughness = 0.25f;

	// �ؽ��� ���ο��� ���˴ϴ�.
	DirectX::XMFLOAT4X4 MatTransform = GraphicsUtil::Identity4x4();

	int DiffuseTexIndex = -1;
	int PadTexIndex0 = -1;
	int PadTexIndex1 = -1;
	int PadTexIndex2 = -1;
};


class DemoApp :public Application
{
public:
	virtual bool Init(HINSTANCE hinstance) override;

private:
	virtual void OnResize() override;
	virtual void Update() override;
	virtual void Draw() override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

	virtual bool CreateRtvAndDsvDescHeap() override;

	void LoadTextures();
	void UpdateCamera();
	void UpdateObjectCBs();
	void UpdateMaterialBuffer();
	void UpdateMainPassCB();

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

	void BuildCubeFaceCamera(float x, float y, float z);

	void BuildDescHeaps();
	void BuildDescViews();
	void BuildCubeDepthStencil();
	void BuildRootSignature();
	void BuildPostProcessRootSignature();
	void BuildShaderAndInputLayout();
    void BuildShapeGeometry();
    void BuildPSO();
	void BuildFrameResources();
	void BuildMaterials();
	void BuildRenderItems();

	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems, ID3D12Resource* objectCB = nullptr);
	void BakeIrradianceMap();


private:
	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;
	//to be used for baking 
	std::unique_ptr<FrameResource> mGeneralFrameResource;
	int mCurrFrameResourceIndex = 0;

	PassConstants mMainPassCB;
	UINT mPassCbvOffset = 0;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mGeneralDescHeap;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSig;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> mmDynamicCubeMapRootSig;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> mPostProcessRootSignature;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	enum class RenderLayer : int
	{
		Opaque = 0,
		Sky,
		Count
	};

	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> mPSOs;
	std::unique_ptr<CubeRenderTarget> mDynamicCubeMap = nullptr;
	int mDynamicTexHeapIndex = -1;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mCubeDSV;
	Microsoft::WRL::ComPtr<ID3D12Resource> mCubeDepthStencilBuffer;
	const UINT CubeMapSize = 512;

	std::unique_ptr<BlurFilter> mBlurFilter;

	Camera mCamera;
	Camera mCubeMapCamera[6];

	float mTheta = 1.5f * DirectX::XM_PI;
	float mPhi = 0.2f * DirectX::XM_PI;
	float mRadius = 15.0f;

	float mSunTheta = 1.25f * DirectX::XM_PI;
	float mSunPhi = DirectX::XM_PIDIV4;

	POINT mLastMousePos;
};