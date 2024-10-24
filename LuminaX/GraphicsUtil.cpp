#include "GraphicsUtil.h"
#include <d3dcompiler.h>
#include <filesystem>
#include <Windows.h>
#include <comdef.h>

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
	D3DCompileFromFile(fileName.c_str(), defines, nullptr, entryPoint.c_str(), target.c_str(), flags, 0, &byteCode, &errorCode);

	if (errorCode != nullptr)
		OutputDebugStringA((char*)errorCode->GetBufferPointer());

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
