#pragma once
#include <string>
#include <wrl.h>

#include <dxcapi.h>
#pragma comment(lib, "dxcompiler.lib")

// 前方宣言
namespace CoreEngine {
    class ShaderReflectionBuilder;
}

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

        /// <summary>
        /// DXCユーティリティを取得（リフレクション用）
        /// </summary>
        /// <returns>IDxcUtilsポインタ</returns>
        IDxcUtils* GetDxcUtils() const { return dxcUtils.Get(); }

    private:
        Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils = nullptr;
        Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler = nullptr;
        Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler = nullptr;
    };
}
