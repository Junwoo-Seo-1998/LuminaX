#pragma once
#include "GraphicsUtil.h"
class Camera
{
public:
    Camera();
    ~Camera();

    // ���� ī�޶� ��ġ�� ��ų� �����մϴ�.
    DirectX::XMVECTOR GetPosition() const;
    DirectX::XMFLOAT3 GetPosition3f() const;
    void SetPosition(float x, float y, float z);
    void SetPosition(const DirectX::XMFLOAT3& v);

    // ī�޶� ���� ���͵��� ����ϴ�.
    DirectX::XMVECTOR GetRight() const;
    DirectX::XMFLOAT3 GetRight3f() const;
    DirectX::XMVECTOR GetUp() const;
    DirectX::XMFLOAT3 GetUp3f() const;
    DirectX::XMVECTOR GetLook() const;
    DirectX::XMFLOAT3 GetLook3f() const;

    // �������� �Ӽ����� ����ϴ�.
    float GetNearZ() const;
    float GetFarZ() const;
    float GetAspect() const;
    float GetFovY() const;
    float GetFovX() const;

    // �� �����̽� ��ǥ�迡�� �����ų� �� ����� �Ÿ��� ����ϴ�.
    float GetNearWindowWidth() const;
    float GetNearWindowHeight() const;
    float GetFarWindowWidth() const;
    float GetFarWindowHeight() const;

    // ������Ʈ���� �����մϴ�.
    void SetLens(float fovY, float aspect, float zn, float zf);

    // LookAt �Ķ���͸� ���� ī�޶� �����̽��� �����մϴ�.
    void LookAt(DirectX::FXMVECTOR pos, DirectX::FXMVECTOR target, DirectX::FXMVECTOR worldUp);
    void LookAt(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& target, const DirectX::XMFLOAT3& up);

    // ��/�������� ��Ʈ������ ����ϴ�.
    DirectX::XMMATRIX GetView() const;
    DirectX::XMMATRIX GetProj() const;

    DirectX::XMFLOAT4X4 GetView4x4f() const;
    DirectX::XMFLOAT4X4 GetProj4x4f() const;

    // ī�޶� �Ÿ� d��ŭ Ⱦ/�� �̵��մϴ�.
    void Strafe(float d);
    void Walk(float d);

    // ī�޶� ȸ����ŵ�ϴ�.
    void Pitch(float angle);
    void RotateY(float angle);

    // ī�޶��� ��ġ/������ ������ �ڿ� �� ��Ʈ������ �ٽ� ����ϱ� ���� ȣ���մϴ�.
    void UpdateViewMatrix();

private:
    // ���� ���������� ī�޶� ��ǥ��.
    DirectX::XMFLOAT3 mPosition = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 mRight = { 1.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 mUp = { 0.0f, 1.0f, 0.0f };
    DirectX::XMFLOAT3 mLook = { 0.0f, 0.0f, 1.0f };

    // ������Ʈ�� �Ӽ� ĳ��.
    float mNearZ = 0.0f;
    float mFarZ = 0.0f;
    float mAspect = 0.0f;
    float mFovY = 0.0f;
    float mNearWindowHeight = 0.0f;
    float mFarWindowHeight = 0.0f;

    bool mViewDirty = true;

    // ��/�������� ��Ʈ���� ĳ��.
    DirectX::XMFLOAT4X4 mView = GraphicsUtil::Identity4x4();
    DirectX::XMFLOAT4X4 mProj = GraphicsUtil::Identity4x4();
};