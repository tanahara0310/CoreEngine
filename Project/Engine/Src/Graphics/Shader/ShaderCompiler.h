#pragma once
#include <string>
#include <vector>
#include <wrl.h>

#include <dxcapi.h>

#include "Cache/RecordingIncludeHandler.h"

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
        /// @param profile コンパイルプロファイル（例: L"vs_6_0", L"ps_6_0"など）
        /// @return コンパイル済みバイナリ（失敗時nullptr）
        /// @note 同じ内容・同じ引数なら Cache/ShaderCache から読み出して
        ///       DXC の呼び出しを丸ごと省く（ShaderCacheStore 参照）。
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
        /// @brief コンパイルの実体（通常シェーダとライブラリの共通経路）
        /// @param filePath   HLSLファイルパス
        /// @param profile    プロファイル文字列
        /// @param entryPoint エントリーポイント名（ライブラリは nullptr）
        /// @return コンパイル済みバイナリ（失敗時nullptr）
        IDxcBlob* CompileInternal(
            const std::wstring& filePath,
            const wchar_t* profile,
            const wchar_t* entryPoint);

        /// @brief AssetDatabase を使って HLSL の実パスを解決する
        std::wstring ResolveShaderPath(const std::wstring& filePath) const;

        /// @brief AssetDatabaseからシェーダーインクルードディレクトリを収集し引数リストを構築する
        std::vector<std::wstring> BuildIncludeArgs() const;

        /// @brief DXC のバージョン識別子を求める（キャッシュキーの一部）
        /// @details DXC を更新するとコード生成が変わるため、キーに含めないと
        ///          SDK 更新後に古い DXIL を使い続けてしまう。
        std::string QueryCompilerVersion() const;

        /// @brief バイト列を IDxcBlob として包む（キャッシュヒット時の返却用）
        /// @details 呼び出し元から見た戻り値の型と所有権はコンパイル時とまったく同じ。
        ///          このため全呼び出し元は無改修で済む。
        IDxcBlob* CreateBlobFromBytes(const std::vector<uint8_t>& bytes) const;

        Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils = nullptr;
        Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler = nullptr;
        Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler = nullptr;

        // include の依存追跡用。includeHandler へ委譲しつつ開けたファイルを記録する
        RecordingIncludeHandler recordingIncludeHandler_;

        // DXC のバージョン識別子（Initialize で 1 回だけ求める）
        std::string compilerVersion_;
    };
}
