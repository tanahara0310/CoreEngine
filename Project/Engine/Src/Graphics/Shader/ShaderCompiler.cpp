#include "pch.h"
#include "ShaderCompiler.h"

#include <cassert>
#include <filesystem>

#include "Utility/Logger/Logger.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "EngineSystem/Startup/StartupProgress.h"
#include "Cache/ShaderCacheKey.h"
#include "Cache/ShaderCacheStore.h"


#pragma comment(lib, "dxcompiler.lib")


namespace CoreEngine
{
    void ShaderCompiler::Initialize()
    {

        //========================================================
        // DXCの初期化
        //========================================================

        // dxcCompilerを初期化
        HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
        assert(SUCCEEDED(hr));
        hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
        assert(SUCCEEDED(hr));

        // 現時点でincludeしない為、includeに対応する為の設定を行う
        hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
        assert(SUCCEEDED(hr));

        // 既定ハンドラを包んで「実際に開いたファイル」を記録できるようにする。
        // この記録がキャッシュの依存マニフェスト（.deps）になる
        recordingIncludeHandler_.SetInner(includeHandler.Get());

        compilerVersion_ = QueryCompilerVersion();
    }

    std::string ShaderCompiler::QueryCompilerVersion() const
    {
        std::string version;

        Microsoft::WRL::ComPtr<IDxcVersionInfo> versionInfo;
        if (SUCCEEDED(dxcCompiler.As(&versionInfo)) && versionInfo) {
            UINT32 major = 0;
            UINT32 minor = 0;
            if (SUCCEEDED(versionInfo->GetVersion(&major, &minor))) {
                version = std::to_string(major) + "." + std::to_string(minor);
            }

            Microsoft::WRL::ComPtr<IDxcVersionInfo2> versionInfo2;
            if (SUCCEEDED(versionInfo.As(&versionInfo2)) && versionInfo2) {
                UINT32 commitCount = 0;
                char* commitHash = nullptr;
                if (SUCCEEDED(versionInfo2->GetCommitInfo(&commitCount, &commitHash)) && commitHash) {
                    version += "-";
                    version += commitHash;
                    CoTaskMemFree(commitHash);
                }
            }
        }

        if (version.empty()) {
            // バージョン情報を取れないビルドでも DLL の差し替えは検出したいので、
            // dxcompiler.dll のサイズと更新時刻で代用する
            HMODULE module = GetModuleHandleW(L"dxcompiler.dll");
            wchar_t modulePath[MAX_PATH] = {};
            if (module && GetModuleFileNameW(module, modulePath, MAX_PATH)) {
                std::error_code errorCode;
                const std::filesystem::path path(modulePath);
                const auto fileSize = std::filesystem::file_size(path, errorCode);
                if (!errorCode) {
                    const auto writeTime = std::filesystem::last_write_time(path, errorCode);
                    if (!errorCode) {
                        version = "dll:" + std::to_string(fileSize) + ":" +
                            std::to_string(writeTime.time_since_epoch().count());
                    }
                }
            }
        }

        return version.empty() ? std::string("unknown") : version;
    }

    std::wstring ShaderCompiler::ResolveShaderPath(const std::wstring& filePath) const
    {
        // AssetDatabaseでパス解決を試みる
        std::wstring resolvedPath = filePath;
        std::filesystem::path fsPath(filePath);
        if (fsPath.is_relative()) {
            fsPath = std::filesystem::absolute(fsPath);
        }
        if (!std::filesystem::exists(fsPath)) {
            // 検索キーは UTF-8 のテキストとして渡す（AssetDatabase の登録名も UTF-8）
            std::string fileName = Logger::GetInstance().PathToUtf8(std::filesystem::path(filePath).filename());
            std::filesystem::path assetPath = AssetDatabase::GetInstance().FindAssetPath(fileName);
            if (!assetPath.empty()) {
                resolvedPath = assetPath.wstring();
            }
        } else {
            resolvedPath = fsPath.wstring();
        }
        return resolvedPath;
    }

    std::vector<std::wstring> ShaderCompiler::BuildIncludeArgs() const
    {
        std::vector<std::wstring> includeArgs;
        for (const auto& dir : AssetDatabase::GetInstance().GetShaderIncludeDirectories())
        {
            includeArgs.push_back(L"-I");
            includeArgs.push_back(dir.wstring());
        }
        return includeArgs;
    }

    IDxcBlob* ShaderCompiler::CreateBlobFromBytes(const std::vector<uint8_t>& bytes) const
    {
        if (bytes.empty() || !dxcUtils) {
            return nullptr;
        }

        Microsoft::WRL::ComPtr<IDxcBlobEncoding> blob;
        const HRESULT hr = dxcUtils->CreateBlob(
            bytes.data(), static_cast<UINT32>(bytes.size()), DXC_CP_ACP, &blob);
        if (FAILED(hr) || !blob) {
            return nullptr;
        }

        // IDxcBlobEncoding は IDxcBlob を継承しているので、そのまま返せる。
        // 呼び出し元から見た型・所有権はコンパイル経路とまったく同じ
        return blob.Detach();
    }

    IDxcBlob* ShaderCompiler::CompileShader(const std::wstring& filePath, const wchar_t* profile)
    {
        return CompileInternal(filePath, profile, L"main");
    }

    IDxcBlob* ShaderCompiler::CompileShaderLibrary(const std::wstring& filePath)
    {
        // DXRライブラリはlib_6_6でコンパイル（-Eによるエントリーポイント指定なし）
        return CompileInternal(filePath, L"lib_6_6", nullptr);
    }

    IDxcBlob* ShaderCompiler::CompileInternal(
        const std::wstring& filePath,
        const wchar_t* profile,
        const wchar_t* entryPoint)
    {
        const std::wstring resolvedPath = ResolveShaderPath(filePath);

        // これからシェーダーを用意する旨をログ出力
        Logger::GetInstance().Log(
            std::format(L"Begin CompileShader, path:{}, profile:{}", resolvedPath, profile),
            LogLevel::INFO,
            LogCategory::Shader);

        // 起動中はここが最大の滞留点。ローディング画面を刻んで
        // 「応答なし」を避ける。起動シーケンス外では空判定 1 回で戻る
        if (StartupProgress::IsActive()) {
            StartupProgress::Tick(
                Logger::GetInstance().PathToUtf8(std::filesystem::path(resolvedPath).filename()).c_str());
        }

        // hlslファイルを読み込む
        Microsoft::WRL::ComPtr<IDxcBlobEncoding> shaderSource;
        HRESULT hr = dxcUtils->LoadFile(resolvedPath.c_str(), nullptr, &shaderSource);
        // 読めなかったら落とす
        assert(SUCCEEDED(hr));
        // 読み込んだファイルの内容を設定する
        DxcBuffer shaderSourceBuffer;
        shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
        shaderSourceBuffer.Size = shaderSource->GetBufferSize();
        // UTF-8の文字コード
        shaderSourceBuffer.Encoding = DXC_CP_UTF8;

        // ===== コンパイル引数の組み立て =====
        // キャッシュキーの材料にするため、まず所有権のある wstring で作ってから
        // LPCWSTR の配列へ落とす（ポインタだけ持つとキーが取れない）
        std::vector<std::wstring> argumentStrings;
        argumentStrings.push_back(resolvedPath);   // コンパイル対象のhlslファイル
        if (entryPoint) {
            argumentStrings.push_back(L"-E");
            argumentStrings.push_back(entryPoint); // エントリーポイント
        }
        argumentStrings.push_back(L"-T");
        argumentStrings.push_back(profile);        // ShaderProfileの設定
        argumentStrings.push_back(L"-Zi");         // デバッグ情報を埋め込む
        argumentStrings.push_back(L"-Od");         // 最適化を外す
        argumentStrings.push_back(L"-Zpr");        // メモリレイアウトは行優先

        for (const std::wstring& includeArg : BuildIncludeArgs()) {
            argumentStrings.push_back(includeArg);
        }

        std::vector<LPCWSTR> arguments;
        arguments.reserve(argumentStrings.size());
        for (const std::wstring& argument : argumentStrings) {
            arguments.push_back(argument.c_str());
        }

        // ===== キャッシュ照会 =====
        // 一次キーは「本体のバイト列 + 引数一式 + DXCバージョン」。
        // include の中身は .deps 側で二段階に検証される（ShaderCacheStore 参照）
        ShaderCacheStore& cacheStore = ShaderCacheStore::GetInstance();
        ShaderCacheStore::EntryInfo cacheEntry;
        if (cacheStore.IsEnabled()) {
            cacheEntry.primaryKey = ShaderCacheKey::ComputePrimaryKey(
                shaderSourceBuffer.Ptr, shaderSourceBuffer.Size, argumentStrings, compilerVersion_);
            cacheEntry.sourcePathUtf8 = Logger::GetInstance().PathToUtf8(resolvedPath);
            cacheEntry.profile = Logger::GetInstance().WideToUtf8(profile);

            std::vector<uint8_t> cachedBytes;
            if (cacheStore.TryLoad(cacheEntry, cachedBytes)) {
                if (IDxcBlob* cachedBlob = CreateBlobFromBytes(cachedBytes)) {
                    Logger::GetInstance().Log(
                        std::format(L"Compile Cached, path:{}, profile:{}", resolvedPath, profile),
                        LogLevel::INFO,
                        LogCategory::Shader);
                    return cachedBlob;
                }
                // 包めなかった場合はキャッシュを無かったことにして通常コンパイルへ落ちる
            }
        }

        // ===== 実際にshaderをcompileする =====
        // 依存追跡のため、既定ハンドラではなく記録付きハンドラを渡す
        recordingIncludeHandler_.Reset();

        Microsoft::WRL::ComPtr<IDxcResult> shaderResult;
        hr = dxcCompiler->Compile(&shaderSourceBuffer, // 読み込んだファイル
            arguments.data(),                      // コンパイルオプション
            static_cast<UINT32>(arguments.size()), // コンパイルオプションの数
            &recordingIncludeHandler_,             // includeの設定（開いたファイルを記録する）
            IID_PPV_ARGS(&shaderResult)            // 結果
        );

        // コンパイルが上手く行かなかったら落とす
        assert(SUCCEEDED(hr));

        // 警告・エラーが出たらログ出力
        Microsoft::WRL::ComPtr<IDxcBlobUtf8> shaderError;
        shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
        if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
            std::string errorMessage(shaderError->GetStringPointer());
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Shader, "{}", errorMessage);
            assert(false);
        }

        // コンパイル結果から実行用のバイナリを取得
        IDxcBlob* shaderBlob = nullptr;
        hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
        // バイナリが取得できなかったら落とす
        assert(SUCCEEDED(hr));

        // ===== キャッシュへ保存 =====
        // 実際に開いた include を依存として記録する。自前の #include スキャンでは
        // 条件付きインクルードや検索パス解決を取りこぼす
        if (cacheStore.IsEnabled() && shaderBlob) {
            cacheStore.Save(
                cacheEntry,
                shaderBlob->GetBufferPointer(),
                shaderBlob->GetBufferSize(),
                recordingIncludeHandler_.GetOpenedFiles());
        }

        // コンパイル成功ログ
        Logger::GetInstance().Log(
            std::format(L"Compile Succeeded, path:{}, profile:{}", resolvedPath, profile),
            LogLevel::INFO,
            LogCategory::Shader);

        // 生成したバイナリを返す
        return shaderBlob;
    }
}
