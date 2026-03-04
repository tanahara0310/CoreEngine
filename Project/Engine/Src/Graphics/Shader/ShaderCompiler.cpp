#include "ShaderCompiler.h"

#include <cassert>

#include "Utility/Logger/Logger.h"


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
    }

    IDxcBlob* ShaderCompiler::CompileShader(const std::wstring& filePath, const wchar_t* profile)
    {

        // これからシェーダーをコンパイルする旨をログ出力
        Logger::GetInstance().Log(std::format(L"Begin CompileShader, path:{}, profile:{}", filePath, profile), LogLevel::INFO, LogCategory::Shader);

        // hlslファイルを読み込む
        IDxcBlobEncoding* shaderSource = nullptr;
        HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
        // 読めなかったら落とす
        assert(SUCCEEDED(hr));
        // 読み込んだファイルの内容を設定する
        DxcBuffer shaderSourceBuffer;
        shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
        shaderSourceBuffer.Size = shaderSource->GetBufferSize();
        // UTF-8の文字コード
        shaderSourceBuffer.Encoding = DXC_CP_UTF8;

        // コンパイルする
        LPCWSTR arguments[] = {

            filePath.c_str(), // コンパイル対象のhlslファイル
            L"-E",
            L"main", // エントリーポイント
            L"-T",
            profile, // ShaderProfileの設定
            L"-Zi", // デバッグ情報を埋め込む
            L"-Od", // 最適化を外す
            L"-Zpr", // メモリレイアウトは行優先
            L"-I", L"Application/Assets/Shader", // インクルードディレクトリを追加
            L"-I", L"Engine/Assets/Shaders", // インクルードディレクトリを追加
        };

        // 実際にshaderをcompileする
        IDxcResult* shaderResult = nullptr;
        hr = dxcCompiler->Compile(&shaderSourceBuffer, // 読み込んだファイル
            arguments, // コンパイルオプション
            _countof(arguments), // コンパイルオプションの数
            includeHandler.Get(), // includeの設定
            IID_PPV_ARGS(&shaderResult) // 結果
        );

        // コンパイルが上手く行かなかったら落とす
        assert(SUCCEEDED(hr));

        // 警告・エラーが出たらログ出力
        IDxcBlobUtf8* shaderError = nullptr;
        shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
        if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
            std::string errorMessage(shaderError->GetStringPointer());
            Logger::GetInstance().Log(errorMessage, LogLevel::Error, LogCategory::Shader);
            assert(false);
        }

        // コンパイル結果から実行用のバイナリを取得
        IDxcBlob* shaderBlob = nullptr;
        hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
        // バイナリが取得できなかったら落とす
        assert(SUCCEEDED(hr));

        // コンパイル成功ログ
        Logger::GetInstance().Log(std::format(L"Compile Succeeded, path:{}, profile:{}", filePath, profile), LogLevel::INFO, LogCategory::Shader);

        // 使わないリソースを解放
        shaderResult->Release();
        shaderSource->Release();

        // 生成したバイナリを返す
        return shaderBlob;
    }
}
