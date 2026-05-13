#pragma once
#include <string>
#include <vector>
#include <wrl.h>

#include <dxcapi.h>

// 前方宣言
namespace CoreEngine {
    class ShaderReflectionBuilder;
}

namespace CoreEngine
{
    class ShaderCompiler {
    public:
        /// @brief 初期化
        void Initialize();

        /// @brief シェーダーコンパイル
        /// @param filePath コンパイルするHLSLファイルパス
        /// @param profile コンパイルプロファイル（例: L"vs_6_0", L"ps_6_0", L"lib_6_6"など）   
        /// @return コンパイル済みバイナリ（失敗時nullptr）
        IDxcBlob* CompileShader(
            const std::wstring& filePath,
            const wchar_t* profile);

        /// @brief シェーダーライブラリのコンパイル（エントリーポイントなし、lib_6_6でコンパイル）
        /// @param filePath コンパイルするHLSLファイルパス
        /// @return コンパイル済みバイナリ（失敗時nullptr）
        IDxcBlob* CompileShaderLibrary(const std::wstring& filePath);

        /// @brief DXCユーティリティを取得（リフレクション用）
        /// @return IDxcUtilsポインタ
        IDxcUtils* GetDxcUtils() const { return dxcUtils.Get(); }

    private:
        /// @brief AssetDatabaseからシェーダーインクルードディレクトリを収集し引数リストを構築する
        std::vector<std::wstring> BuildIncludeArgs() const;

        Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils = nullptr;
        Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler = nullptr;
        Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler = nullptr;
    };
}
