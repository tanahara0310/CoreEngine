#pragma once

//========================================================================================
// ShaderProgram.h
//
// 「コンパイル済みシェーダー ＋ そのリフレクション」を 1 単位にまとめ、同じシェーダーなら
// 使い回す。従来は 17 のサブシステムが下記を各自で書き写していた。
//
//   ShaderCompiler compiler;  compiler.Initialize();          // DXC を毎回作る
//   auto blob = compiler.CompileShader(path, L"cs_6_0");
//   ShaderReflectionBuilder builder;
//   builder.Initialize(compiler.GetDxcUtils());               // 生ポインタを保持する
//   reflectionData_ = builder.BuildFromComputeShader(blob, name);
//
// この形の問題は 2 つ。
//   1. DXC（IDxcUtils / IDxcCompiler3）がサブシステムの数だけ生成される
//   2. ShaderReflectionBuilder が IDxcUtils の生ポインタを持つので、ローカルの
//      ShaderCompiler と組み合わせるとダングリングを書けてしまう
//
// ShaderProgramCache が ShaderCompiler と ShaderReflectionBuilder を 1 つずつ抱えることで
// どちらも構造的に消える。
//
// 【ルートシグネチャを含めていない理由】
// 設計案（§4.5）では RootSignature も ShaderProgram に含める想定だったが、
// RootSignatureConfig（戦略・サンプラー・テーブルグループ）をキーに含めないと
// 「同じシェーダーだが設定違い」を取り違える。設定のハッシュ化は誤ると GPU が壊れる側の
// 間違い方をするので、まずは blob とリフレクションだけを共有し、RootSignature は
// 各サブシステムが自分の config で作る形に留める。
//
// 詳細: Docs/Engine/Graphics/Shader/ShaderBinding_Design_Review.md §4.5
//========================================================================================

#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/Shader/ShaderReflectionData.h"

#include <dxcapi.h>
#include <map>
#include <memory>
#include <string>

namespace CoreEngine
{
    /// @brief コンパイル済みシェーダーとそのリフレクション結果
    /// @note ShaderProgramCache が所有し、複数のサブシステムから共有される（読み取り専用）
    class ShaderProgram {
    public:
        /// @brief 頂点シェーダーの blob（グラフィックスのみ。それ以外は nullptr）
        IDxcBlob* GetVS() const { return vs_; }
        /// @brief ピクセルシェーダーの blob（グラフィックスのみ。それ以外は nullptr）
        IDxcBlob* GetPS() const { return ps_; }
        /// @brief コンピュートシェーダーの blob（コンピュートのみ。それ以外は nullptr）
        IDxcBlob* GetCS() const { return cs_; }

        /// @brief VS+PS もしくは CS をマージしたリフレクション結果
        const ShaderReflectionData& GetReflection() const { return *reflection_; }

        /// @brief ログ用の識別名
        const std::string& GetDebugName() const { return debugName_; }

    private:
        friend class ShaderProgramCache;

        IDxcBlob* vs_ = nullptr;
        IDxcBlob* ps_ = nullptr;
        IDxcBlob* cs_ = nullptr;
        std::unique_ptr<ShaderReflectionData> reflection_;
        std::string debugName_;
    };

    /// @brief シェーダーのコンパイルとリフレクションを一元化し、結果を共有するキャッシュ
    /// @warning メインスレッド専用。ShaderCompiler は「インスタンスをスレッドごとに 1 つ」
    ///          という制約があるため、ワーカースレッドから呼んではいけない
    ///          （並列事前コンパイルは ShaderPrewarm が自前のコンパイラを持つ）。
    class ShaderProgramCache {
    public:
        /// @brief DXC の初期化。エンジン起動時に 1 回だけ呼ぶ
        void Initialize();

        /// @brief VS+PS のプログラムを取得する（無ければコンパイルして作る）
        /// @param vsPath    頂点シェーダーのパス（AssetDatabase が解決できるファイル名も可）
        /// @param psPath    ピクセルシェーダーのパス
        /// @param debugName ログ用の識別名。同じシェーダー対に別名を付けても最初の名前が残る
        /// @return 失敗時は nullptr（コンパイルエラーはログ済み）
        const ShaderProgram* GetOrCreateGraphics(
            const std::wstring& vsPath, const std::wstring& psPath, const std::string& debugName);

        /// @brief CS のプログラムを取得する（無ければコンパイルして作る）
        /// @return 失敗時は nullptr
        const ShaderProgram* GetOrCreateCompute(
            const std::wstring& csPath, const std::string& debugName);

        /// @brief シェーダーライブラリ（lib_6_6 = DXR）のプログラムを取得する
        /// @return 失敗時は nullptr
        /// @note blob は GetCS() で取れる（DXR の State Object へ渡す用）
        const ShaderProgram* GetOrCreateLibrary(
            const std::wstring& libPath, const std::string& debugName);

        /// @brief 生の ShaderCompiler を取得する
        /// @note CustomShaderPipeline::Build() のように ShaderCompiler& を要求する API 用。
        ///       単にコンパイルしたいだけなら GetOrCreate* / GetOrCompile を使うこと。
        ShaderCompiler& GetCompiler() { return compiler_; }

        /// @brief 生の ShaderReflectionBuilder を取得する
        /// @note こちらも CustomShaderPipeline::Build() 用。IDxcUtils の寿命が compiler_ と
        ///       揃っているので、ローカルに作るのと違ってダングリングしない。
        ShaderReflectionBuilder& GetReflectionBuilder() { return reflectionBuilder_; }

        // ---- 診断用 ----
        /// @brief 保持しているプログラム数（＝実際にコンパイル＋リフレクションした回数）
        size_t GetProgramCount() const { return programs_.size(); }
        /// @brief キャッシュヒット数（＝コンパイルとリフレクションを省けた回数）
        size_t GetHitCount() const { return hitCount_; }

        /// @brief 保持数とヒット数をログへ出す（起動時の効き具合の確認用）
        void LogSummary() const;

    private:
        /// @brief パスの組み合わせからキーを作る
        static std::wstring MakeKey(const std::wstring& a, const std::wstring& b);

        /// @brief パス＋プロファイル単位で blob を使い回す
        /// @details プログラムのキーは (VS, PS) の組なので、PS だけ違う 30 個のポストエフェクトは
        ///          プログラムとしては全部ミスする。一方 FullScreen.VS.hlsl は 30 回とも同じなので、
        ///          blob 単位で持てばそこは 1 回で済む。
        IDxcBlob* GetOrCompile(const std::wstring& path, const wchar_t* profile);

        ShaderCompiler compiler_;
        ShaderReflectionBuilder reflectionBuilder_;
        std::map<std::wstring, std::unique_ptr<ShaderProgram>> programs_;
        std::map<std::wstring, IDxcBlob*> blobs_;
        size_t hitCount_ = 0;
        size_t blobHitCount_ = 0;
        bool initialized_ = false;
    };
}
