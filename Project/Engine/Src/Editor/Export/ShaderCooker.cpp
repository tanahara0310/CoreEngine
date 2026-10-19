#include "pch.h"
#include "Editor/Export/ShaderCooker.h"

#ifdef CORE_EDITOR

#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Material/ShaderMaterial.h"
#include "Graphics/Material/ShaderMaterialPipeline.h"
#include "Graphics/Shader/Cook/CookedShader.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Utility/Logger/Logger.h"

#include <format>
#include <fstream>
#include <memory>
#include <set>

namespace CoreEngine::Editor
{
    namespace
    {
        /// @brief Windows で扱えるパスの長さの上限（終端の文字を除く）
        constexpr size_t kMaxPathLength = 259;
    }

    std::vector<ShaderManifest::Entry> ShaderCooker::PlanTargets()
    {
        // 一度も描いていないマテリアルのシェーダも入れる（.mat が指すピクセルシェーダと頂点シェーダ）
        const std::vector<ShaderManifest::Entry> recorded = ShaderManifest::GetInstance().Collect();
        std::set<ShaderManifest::Entry> targets(recorded.begin(), recorded.end());
        Logger& log = Logger::GetInstance();
        for (const AssetInfo* info : AssetDatabase::GetInstance().GetAssetsOfType(AssetType::Material)) {
            std::string shaderPath;
            std::string vertexShaderPath;
            if (info->fullPath.extension() != ".mat"
                || !ShaderMaterial::ReadShaderPaths(info->fullPath, shaderPath, vertexShaderPath)
                || shaderPath.empty()) {
                continue;
            }
            if (vertexShaderPath.empty()) {
                vertexShaderPath = kStandardMaterialVertexShader;
            }
            targets.insert({ log.Utf8ToPath(shaderPath).generic_wstring(), L"ps_6_0", L"main" });
            targets.insert({ log.Utf8ToPath(vertexShaderPath).generic_wstring(), L"vs_6_0", L"main" });
        }
        return std::vector<ShaderManifest::Entry>(targets.begin(), targets.end());
    }

    std::string ShaderCooker::Cook(const ShaderManifest::Entry& target, const std::filesystem::path& destination,
        int& writtenFiles, std::uintmax_t& writtenBytes)
    {
        Logger& log = Logger::GetInstance();
        const std::filesystem::path cookedRelative =
            CookedShader::ToCookedPath(std::filesystem::path(target.filePath), target.profile, target.entryPoint);
        if (cookedRelative.empty()) {
            log.Logf(LogLevel::WARNING, LogCategory::Shader, "{}",
                "アセットのフォルダの外にあるシェーダは書き出しません: " + log.WideToUtf8(target.filePath));
            return {};
        }
        const std::filesystem::path cooked = destination / cookedRelative;
        if (cooked.native().size() > kMaxPathLength) {
            return std::format("{} は {} 文字あり、Windows のパスの上限（{} 文字）を超えるので書けません。"
                "プロジェクトをもっと短いパスの場所へ移してください",
                log.PathToUtf8(cooked), cooked.native().size(), kMaxPathLength);
        }

        // IDxcCompiler3 はスレッドをまたいで使えないので、呼んだスレッドごとに 1 つ持つ
        thread_local std::unique_ptr<ShaderCompiler> compiler;
        if (!compiler) {
            compiler = std::make_unique<ShaderCompiler>();
            compiler->Initialize();
        }

        std::string error;
        const Microsoft::WRL::ComPtr<IDxcBlob> blob =
            compiler->CompileForCooking(target.filePath, target.profile, target.entryPoint, error);
        if (!blob) {
            return error;
        }

        std::error_code ec;
        std::filesystem::create_directories(cooked.parent_path(), ec);
        if (ec) {
            return log.PathToUtf8(cooked.parent_path()) + " を作れませんでした";
        }
        std::ofstream file(cooked, std::ios::binary | std::ios::trunc);
        file.write(static_cast<const char*>(blob->GetBufferPointer()), static_cast<std::streamsize>(blob->GetBufferSize()));
        if (!file) {
            return log.PathToUtf8(cooked) + " を書けませんでした";
        }

        writtenBytes += blob->GetBufferSize();
        ++writtenFiles;
        return {};
    }
}

#endif // CORE_EDITOR
