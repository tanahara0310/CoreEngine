#pragma once
#include <string>
#include <wrl.h>

#include <dxcapi.h>
#pragma comment(lib, "dxcompiler.lib")


namespace CoreEngine
{
	class ShaderCompiler {
	public:
		/// <summary>
		/// 初期化
		/// </summary>
		void Initialize();

		/// <summary>
		/// シェーダーコンパイル
		/// </summary>
		/// <param name="filePath">Compileするシェーダのファイルパス</param>
		/// <param name="profile">compileに使用するprofile</param>
		/// <returns></returns>
		IDxcBlob* CompileShader(
			const std::wstring& filePath,
			const wchar_t* profile);

	private:
		Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils = nullptr;
		Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler = nullptr;
		Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler = nullptr;
	};
}
