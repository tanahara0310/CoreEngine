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
    /// @brief パス解決と引数構築まで終わったコンパイル要求
    /// @details 分けてあるのは AssetDatabase をワーカースレッドから触らせないため。
    ///          `FindAssetPath` は operator[] で挿入するので並列に呼ぶとレースする。
    struct PreparedShaderCompile {
        std::wstring resolvedPath;                  ///< 解決済みの .hlsl フルパス
        std::wstring profile;                       ///< "ps_6_0" など
        std::wstring entryPoint;                    ///< 空ならライブラリ（-E なし）
        std::vector<std::wstring> argumentStrings;  ///< DXC へ渡す引数一式（-I 群を含む）

        bool IsValid() const { return !resolvedPath.empty() && !profile.empty(); }
    };

    /// @brief DXC による HLSL コンパイルの窓口（キャッシュへの出し入れも含む）
    class ShaderCompiler {
    public:
        /// @brief 初期化
        void Initialize();

        /// @brief シェーダーコンパイル
        /// @param profile コンパイルプロファイル（例: L"vs_6_0", L"ps_6_0"）
        /// @return コンパイル済みバイナリ（失敗時 nullptr）
        /// @note 同じ内容・同じ引数なら ShaderCacheStore から読み出して DXC 呼び出しを省く
        IDxcBlob* CompileShader(
            const std::wstring& filePath,
            const wchar_t* profile);

        /// @brief シェーダーライブラリのコンパイル（エントリーポイントなし・lib_6_6）
        IDxcBlob* CompileShaderLibrary(const std::wstring& filePath);

        /// @brief DXCユーティリティを取得（リフレクション用）
        /// @return IDxcUtilsポインタ
        IDxcUtils* GetDxcUtils() const { return dxcUtils.Get(); }

        /// @brief パス解決と引数構築を行う（メインスレッド専用）
        /// @return ワーカーへ渡せる状態になったコンパイル要求
        /// @warning AssetDatabase を触るのでワーカースレッドから呼んではいけない
        PreparedShaderCompile Prepare(
            const std::wstring& filePath,
            const wchar_t* profile,
            const wchar_t* entryPoint) const;

        /// @brief 準備済み要求をコンパイルする（任意のスレッドから呼べる）
        /// @warning ShaderCompiler のインスタンスはスレッドごとに 1 つ用意すること。
        ///          IDxcCompiler3 はスレッドセーフとして文書化されておらず、
        ///          RecordingIncludeHandler もインスタンス状態を持つため .deps が混ざる。
        IDxcBlob* CompilePrepared(const PreparedShaderCompile& prepared);

    private:
        /// @brief コンパイルの実体（通常シェーダとライブラリの共通経路）
        IDxcBlob* CompileInternal(
            const std::wstring& filePath,
            const wchar_t* profile,
            const wchar_t* entryPoint);

        /// @brief AssetDatabase を使って HLSL の実パスを解決する
        std::wstring ResolveShaderPath(const std::wstring& filePath) const;

        /// @brief AssetDatabaseからシェーダーインクルードディレクトリを収集し引数リストを構築する
        std::vector<std::wstring> BuildIncludeArgs() const;

        /// @brief DXC へ渡す引数一式を組み立てる
        /// @warning 通常経路と事前コンパイルの唯一の共通点。引数はキャッシュ一次キーの材料なので、
        ///          片方だけ変えるとキーが食い違い、事前コンパイルの結果が二度と使われなくなる。
        static std::vector<std::wstring> BuildArgumentStrings(
            const std::wstring& resolvedPath,
            const std::wstring& profile,
            const std::wstring& entryPoint,
            const std::vector<std::wstring>& includeArgs);

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
