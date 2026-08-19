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
    ///
    /// @details **これを分けてある理由は「AssetDatabase をワーカースレッドから
    ///          触らせないこと」の一点。** `AssetDatabase::FindAssetPath` は
    ///          `unordered_map::operator[]` で要素を挿入するため、読み取りに見えて
    ///          書き込みであり、並列に呼ぶと rehash でレースする。
    ///          パス解決と `-I` の収集はメインスレッドで済ませ、
    ///          ワーカーへ渡すのはこの完成品だけにする。
    struct PreparedShaderCompile {
        std::wstring resolvedPath;                  ///< 解決済みの .hlsl フルパス
        std::wstring profile;                       ///< "ps_6_0" など
        std::wstring entryPoint;                    ///< 空ならライブラリ（-E なし）
        std::vector<std::wstring> argumentStrings;  ///< DXC へ渡す引数一式（-I 群を含む）

        bool IsValid() const { return !resolvedPath.empty() && !profile.empty(); }
    };

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

        /// @brief パス解決と引数構築を行う（**メインスレッド専用**）
        /// @param filePath   呼び出し側が渡すパス（解決前でよい）
        /// @param profile    プロファイル文字列
        /// @param entryPoint エントリーポイント名（ライブラリは nullptr）
        /// @return ワーカーへ渡せる状態になったコンパイル要求
        /// @warning AssetDatabase を触るのでワーカースレッドから呼んではいけない
        ///          （PreparedShaderCompile の説明を参照）。
        PreparedShaderCompile Prepare(
            const std::wstring& filePath,
            const wchar_t* profile,
            const wchar_t* entryPoint) const;

        /// @brief 準備済み要求をコンパイルする（**任意のスレッドから呼べる**）
        /// @param prepared Prepare の戻り値
        /// @return コンパイル済みバイナリ（失敗時 nullptr）
        /// @note ShaderCompiler のインスタンスは**スレッドごとに 1 つ**用意すること。
        ///       DXC の IDxcCompiler3 はスレッドセーフとして文書化されておらず、
        ///       さらに RecordingIncludeHandler が「直前のコンパイルで開いた
        ///       include」というインスタンス状態を持つため、共有すると
        ///       依存マニフェスト（.deps）が別シェーダのものと混ざる。
        ///       混ざると「.hlsli を直したのに反映されない」という最悪の壊れ方をする。
        IDxcBlob* CompilePrepared(const PreparedShaderCompile& prepared);

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

        /// @brief DXC へ渡す引数一式を組み立てる
        /// @details **通常経路と事前コンパイルの唯一の共通点。**
        ///          引数はキャッシュの一次キーの材料なので、片方だけ引数を変えると
        ///          キーが食い違い、事前コンパイルの結果が二度と使われなくなる
        ///          （しかも「遅いだけで動く」ので気付けない）。
        ///          両者がこの 1 関数を通ることでバイト単位の一致を保証する。
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
