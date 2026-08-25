#pragma once

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
    /// @warning メインスレッド専用（ShaderCompiler はスレッドごとに 1 インスタンスが必要）
    class ShaderProgramCache {
    public:
        /// @brief DXC の初期化。エンジン起動時に 1 回だけ呼ぶ
        void Initialize();

        /// @brief VS+PS のプログラムを取得する（無ければコンパイルして作る）
        /// @param vsPath    頂点シェーダーのパス（AssetDatabase が解決できるファイル名も可）
        /// @param psPath    ピクセルシェーダーのパス
        /// @param debugName ログ用の識別名
        /// @return 失敗時は nullptr
        const ShaderProgram* GetOrCreateGraphics(
            const std::wstring& vsPath, const std::wstring& psPath, const std::string& debugName);

        /// @brief CS のプログラムを取得する（無ければコンパイルして作る）
        /// @param profile シェーダープロファイル（キーに含まれる）
        /// @return 失敗時は nullptr
        const ShaderProgram* GetOrCreateCompute(
            const std::wstring& csPath, const std::string& debugName,
            const wchar_t* profile = L"cs_6_0");

        /// @brief シェーダーライブラリ（lib_6_6 = DXR）のプログラムを取得する
        /// @return 失敗時は nullptr。blob は GetCS() で取れる
        const ShaderProgram* GetOrCreateLibrary(
            const std::wstring& libPath, const std::string& debugName);

        /// @brief 生の ShaderCompiler を取得する
        /// @note ShaderCompiler& を要求する API 用。通常は GetOrCreate* を使うこと
        ShaderCompiler& GetCompiler() { return compiler_; }

        /// @brief 生の ShaderReflectionBuilder を取得する
        /// @note IDxcUtils の寿命が compiler_ と揃っているのでダングリングしない
        ShaderReflectionBuilder& GetReflectionBuilder() { return reflectionBuilder_; }

        /// @brief 保持しているプログラム数
        size_t GetProgramCount() const { return programs_.size(); }
        /// @brief キャッシュヒット数
        size_t GetHitCount() const { return hitCount_; }

        /// @brief 保持数とヒット数をログへ出す
        void LogSummary() const;

    private:
        /// @brief パスの組み合わせからキーを作る
        static std::wstring MakeKey(const std::wstring& a, const std::wstring& b);

        /// @brief パス＋プロファイル単位で blob を使い回す
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
