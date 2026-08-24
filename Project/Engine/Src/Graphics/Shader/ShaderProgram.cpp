#include "pch.h"
#include "ShaderProgram.h"
#include "Utility/Logger/Logger.h"

#include <cassert>

namespace CoreEngine
{
    void ShaderProgramCache::Initialize()
    {
        if (initialized_) {
            return;
        }
        compiler_.Initialize();
        // ここが要点: リフレクションビルダーが持つ IDxcUtils の生ポインタは、
        // 同じオブジェクトが所有する compiler_ のものなので寿命が必ず一致する。
        reflectionBuilder_.Initialize(compiler_.GetDxcUtils());
        initialized_ = true;
    }

    const ShaderProgram* ShaderProgramCache::GetOrCreateLibrary(
        const std::wstring& libPath, const std::string& debugName)
    {
        assert(initialized_ && "ShaderProgramCache::Initialize() を先に呼ぶこと");

        const std::wstring key = MakeKey(libPath, L"lib");
        if (auto it = programs_.find(key); it != programs_.end()) {
            ++hitCount_;
            return it->second.get();
        }

        // ライブラリはエントリーポイント指定なし（-E を付けない）
        IDxcBlob* lib = compiler_.CompileShaderLibrary(libPath);
        if (!lib) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Shader,
                "シェーダーライブラリのコンパイルに失敗しました: name={}", debugName);
            return nullptr;
        }

        auto program = std::make_unique<ShaderProgram>();
        program->cs_ = lib;   // DXR の State Object へ渡す blob はここから取る
        program->debugName_ = debugName;
        program->reflection_ = reflectionBuilder_.BuildFromLibrary(lib, debugName);

        const ShaderProgram* result = program.get();
        programs_.emplace(key, std::move(program));
        return result;
    }

    void ShaderProgramCache::LogSummary() const
    {
        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Shader,
            "ShaderProgramCache: プログラム {} 本（ヒット {} 回） / blob {} 本（ヒット {} 回）"
            "。blob のヒット分だけ DXC 呼び出しを省けている",
            programs_.size(), hitCount_, blobs_.size(), blobHitCount_);
    }

    IDxcBlob* ShaderProgramCache::GetOrCompile(const std::wstring& path, const wchar_t* profile)
    {
        const std::wstring key = MakeKey(path, profile);
        if (auto it = blobs_.find(key); it != blobs_.end()) {
            ++blobHitCount_;
            return it->second;
        }
        IDxcBlob* blob = compiler_.CompileShader(path, profile);
        if (blob) {
            blobs_.emplace(key, blob);
        }
        return blob;
    }

    std::wstring ShaderProgramCache::MakeKey(const std::wstring& a, const std::wstring& b)
    {
        // パスに現れない区切りを使う
        return a + L"\n" + b;
    }

    const ShaderProgram* ShaderProgramCache::GetOrCreateGraphics(
        const std::wstring& vsPath, const std::wstring& psPath, const std::string& debugName)
    {
        assert(initialized_ && "ShaderProgramCache::Initialize() を先に呼ぶこと");

        const std::wstring key = MakeKey(vsPath, psPath);
        if (auto it = programs_.find(key); it != programs_.end()) {
            ++hitCount_;
            return it->second.get();
        }

        IDxcBlob* vs = GetOrCompile(vsPath, L"vs_6_0");
        IDxcBlob* ps = GetOrCompile(psPath, L"ps_6_0");
        if (!vs || !ps) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Shader,
                "シェーダーのコンパイルに失敗しました: name={} (vs={}, ps={})",
                debugName, static_cast<const void*>(vs), static_cast<const void*>(ps));
            return nullptr;
        }

        auto program = std::make_unique<ShaderProgram>();
        program->vs_ = vs;
        program->ps_ = ps;
        program->debugName_ = debugName;
        program->reflection_ = reflectionBuilder_.BuildFromShaders(vs, ps, debugName);

        const ShaderProgram* result = program.get();
        programs_.emplace(key, std::move(program));
        return result;
    }

    const ShaderProgram* ShaderProgramCache::GetOrCreateCompute(
        const std::wstring& csPath, const std::string& debugName)
    {
        assert(initialized_ && "ShaderProgramCache::Initialize() を先に呼ぶこと");

        const std::wstring key = MakeKey(csPath, L"cs");
        if (auto it = programs_.find(key); it != programs_.end()) {
            ++hitCount_;
            return it->second.get();
        }

        IDxcBlob* cs = GetOrCompile(csPath, L"cs_6_0");
        if (!cs) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Shader,
                "コンピュートシェーダーのコンパイルに失敗しました: name={}", debugName);
            return nullptr;
        }

        auto program = std::make_unique<ShaderProgram>();
        program->cs_ = cs;
        program->debugName_ = debugName;
        program->reflection_ = reflectionBuilder_.BuildFromComputeShader(cs, debugName);

        const ShaderProgram* result = program.get();
        programs_.emplace(key, std::move(program));
        return result;
    }
}
