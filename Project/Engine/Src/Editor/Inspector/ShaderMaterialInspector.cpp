#include "pch.h"
#include "ShaderMaterialInspector.h"

#ifdef CORE_EDITOR

#include "EngineSystem/EngineSystem.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/Scene/EditorSceneAccess.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Asset/AssetRef.h"
#include "Graphics/Material/ShaderMaterial.h"
#include "Graphics/Material/ShaderMaterialLibrary.h"
#include "Graphics/Material/ShaderMaterialPipeline.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/ProjectPostEffect/ProjectPostEffect.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/Sprite/SpriteRenderer.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace CoreEngine::Editor::ShaderMaterialInspector
{
    namespace
    {
        /// @brief ピクセルシェーダの雛形
        constexpr const char* kShaderTemplate = "Engine/Templates/Shaders/SpriteMaterial.PS.hlsl.txt";

        /// @brief 雛形のシェーダに合わせた値の初め
        constexpr const char* kTemplateValues = R"({ "tint": [1.0, 1.0, 1.0, 1.0], "speed": 2.0 })";

        /// @brief マテリアルのシェーダから値の並びを引く（組めなければ nullptr）
        /// @details プロジェクトのポストエフェクトのマテリアルなら、そのコンピュートシェーダの並びを使う
        const MaterialParamLayout* FindLayout(const ShaderMaterial& material)
        {
            EngineSystem* const engine = SceneAccess::Engine();
            if (PostEffectManager* const postEffects = engine ? engine->GetService<PostEffectManager>() : nullptr) {
                if (const ProjectPostEffect* const effect = postEffects->FindProjectEffectByMaterial(material.GetAssetPath())) {
                    return &effect->GetLayout();
                }
            }
            RenderManager* const renderManager = engine ? engine->GetService<RenderManager>() : nullptr;
            auto* const sprites = renderManager
                ? dynamic_cast<SpriteRenderer*>(renderManager->GetRenderer(RenderPassType::Sprite)) : nullptr;
            const ShaderMaterialPipeline* const pipeline = sprites ? sprites->GetMaterialPipeline(material) : nullptr;
            return pipeline ? &pipeline->GetLayout() : nullptr;
        }

        /// @brief 色として出す項目か（名前に color を含む 3〜4 成分の数）
        bool IsColorField(const MaterialParamField& field)
        {
            if (field.kind != MaterialParamField::Kind::Float || field.components < 3) {
                return false;
            }
            std::string lower = field.name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; });
            return lower.find("color") != std::string::npos;
        }

        /// @brief 値 1 つ（配列なら要素 1 つ）の欄
        /// @param commit 編集を終えたら true にする
        /// @return 値を変えたら true
        bool DrawValue(ShaderMaterial& material, const MaterialParamField& field, const std::string& key, bool& commit)
        {
            std::array<float, 4> numbers = { 0.0f, 0.0f, 0.0f, 0.0f };
            if (const MaterialNumbers* const current = material.FindNumbers(key)) {
                for (size_t index = 0; index < current->size() && index < numbers.size(); ++index) {
                    numbers[index] = (*current)[index];
                }
            }
            const int count = static_cast<int>(field.components);

            bool changed = false;
            ImGui::PushID(key.c_str());
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (IsColorField(field)) {
                const ImGuiColorEditFlags flags = ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR;
                changed = (count >= 4) ? ImGui::ColorEdit4("##value", numbers.data(), flags)
                                       : ImGui::ColorEdit3("##value", numbers.data(), flags);
                commit |= ImGui::IsItemDeactivatedAfterEdit();
            } else if (field.kind == MaterialParamField::Kind::Float) {
                changed = ImGui::DragScalarN("##value", ImGuiDataType_Float, numbers.data(), count, 0.01f,
                    nullptr, nullptr, "%.3f");
                commit |= ImGui::IsItemDeactivatedAfterEdit();
            } else if (field.kind == MaterialParamField::Kind::Bool) {
                for (int index = 0; index < count; ++index) {
                    ImGui::PushID(index);
                    bool value = numbers[static_cast<size_t>(index)] != 0.0f;
                    if (index > 0) {
                        UI::SameLine();
                    }
                    if (ImGui::Checkbox("##value", &value)) {
                        numbers[static_cast<size_t>(index)] = value ? 1.0f : 0.0f;
                        changed = true;
                        commit = true;
                    }
                    ImGui::PopID();
                }
            } else {
                std::array<int, 4> integers{};
                for (size_t index = 0; index < integers.size(); ++index) {
                    integers[index] = static_cast<int>(std::lround(numbers[index]));
                }
                const int minimum = 0;
                const bool isUnsigned = field.kind == MaterialParamField::Kind::Uint;
                changed = ImGui::DragScalarN("##value", ImGuiDataType_S32, integers.data(), count, 0.1f,
                    isUnsigned ? &minimum : nullptr, nullptr);
                commit |= ImGui::IsItemDeactivatedAfterEdit();
                for (size_t index = 0; index < integers.size(); ++index) {
                    numbers[index] = static_cast<float>(integers[index]);
                }
            }
            ImGui::PopID();

            if (changed) {
                material.SetNumbers(key, std::span<const float>(numbers.data(), static_cast<size_t>(count)));
            }
            return changed;
        }

        /// @brief 左にラベル・右に欄の 1 行を始める
        void BeginRow(const char* label)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
        }

        /// @brief シェーダを選ぶ欄（プロジェクトの .hlsl から選ぶ）
        /// @return 選び直したら true
        bool DrawShaderPicker(const char* id, std::string& path, const char* emptyLabel)
        {
            bool changed = false;
            ImGui::SetNextItemWidth(-FLT_MIN);
            const std::string preview = path.empty() ? std::string(emptyLabel) : path;
            if (ImGui::BeginCombo(id, preview.c_str())) {
                if (ImGui::Selectable(emptyLabel, path.empty()) && !path.empty()) {
                    path.clear();
                    changed = true;
                }
                std::vector<std::string> candidates;
                for (const AssetInfo* info : AssetDatabase::GetInstance().GetAssetsOfType(AssetType::Shader)) {
                    const std::string candidate = ToAssetPath(*info);
                    if (candidate.starts_with("Application/") && info->fullPath.extension() == ".hlsl") {
                        candidates.push_back(candidate);
                    }
                }
                std::sort(candidates.begin(), candidates.end());
                for (const std::string& candidate : candidates) {
                    if (ImGui::Selectable(candidate.c_str(), candidate == path) && candidate != path) {
                        path = candidate;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        /// @brief テクスチャ 1 枚の欄（パスを書くか Project のテクスチャを落とす）
        /// @return 変えたら true
        bool DrawTexture(ShaderMaterial& material, const std::string& name)
        {
            MaterialTexture texture;
            if (const MaterialTexture* const current = material.FindTexture(name)) {
                texture = *current;
            }

            bool changed = false;
            ImGui::PushID(name.c_str());
            ImGui::SetNextItemWidth(-FLT_MIN);
            std::string path = texture.path;
            if (UI::InputStringWithHint("##path", "（白）", path, ImGuiInputTextFlags_EnterReturnsTrue)) {
                texture.path = path;
                changed = true;
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_FILE")) {
                    const auto* data = static_cast<const char*>(payload->Data);
                    const std::string fileName(data, strnlen(data, static_cast<size_t>(payload->DataSize)));
                    if (const AssetInfo* const dropped = FindAssetInfo(fileName, AssetType::Texture)) {
                        texture.path = ToAssetPath(*dropped);
                        changed = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::Checkbox("データとして読む（リニア）", &texture.linear) && !texture.path.empty()) {
                changed = true;
            }
            ImGui::PopID();

            if (changed) {
                material.SetTexture(name, texture);
            }
            return changed;
        }

        /// @brief 欄の幅で折り返す補足の文
        void WrappedHint(const char* text)
        {
            ImGui::PushTextWrapPos(0.0f);
            UI::Hint(text);
            ImGui::PopTextWrapPos();
        }

        /// @brief ファイルの中身を全部読む（読めなければ空）
        std::string ReadText(const std::filesystem::path& path)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in) {
                return {};
            }
            std::stringstream text;
            text << in.rdbuf();
            return text.str();
        }

        /// @brief ファイルへ書く
        bool WriteText(const std::filesystem::path& path, const std::string& text)
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << text;
            return static_cast<bool>(out);
        }
    }

    bool Draw(ShaderMaterial& material, const MaterialParamLayout* givenLayout)
    {
        bool changed = false;
        bool commit = false;

        if (ImGui::BeginTable("##shaderMaterial", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);

            BeginRow("シェーダ");
            std::string shader = material.GetShaderPath();
            if (DrawShaderPicker("##shader", shader, "（なし）")) {
                material.SetShaderPath(shader);
                changed = commit = true;
            }

            BeginRow("頂点シェーダ");
            std::string vertexShader = material.GetVertexShaderPath();
            if (DrawShaderPicker("##vertexShader", vertexShader, "（標準）")) {
                material.SetVertexShaderPath(vertexShader);
                changed = commit = true;
            }
            ImGui::EndTable();
        }

        if (material.GetShaderPath().empty()) {
            WrappedHint("シェーダを選ぶと、シェーダの値（register(b0) の定数バッファ）の欄が出ます。");
        } else if (const MaterialParamLayout* const layout = givenLayout ? givenLayout : FindLayout(material)) {
            if (!layout->fields.empty() || !layout->textures.empty()) {
                UI::SectionHeader("値");
            }
            if (ImGui::BeginTable("##shaderMaterialValues", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
                for (const MaterialParamField& field : layout->fields) {
                    if (field.elements == 0) {
                        BeginRow(field.name.c_str());
                        changed |= DrawValue(material, field, field.name, commit);
                        continue;
                    }
                    for (uint32_t index = 0; index < field.elements; ++index) {
                        const std::string key = field.name + "[" + std::to_string(index) + "]";
                        BeginRow(key.c_str());
                        changed |= DrawValue(material, field, key, commit);
                    }
                }
                for (const std::string& name : layout->textures) {
                    BeginRow(name.c_str());
                    if (DrawTexture(material, name)) {
                        changed = commit = true;
                    }
                }
                ImGui::EndTable();
            }
        } else {
            WrappedHint("シェーダをコンパイルできません。コンソールのエラーを見てください。");
        }

        if (commit) {
            material.Save();
        }
        return changed;
    }

    bool DrawFile(const std::filesystem::path& file)
    {
        if (file.extension() != ".mat") {
            return false;
        }
        const std::string assetPath = Logger::GetInstance().PathToUtf8(ProjectPaths::MakeRelative(file));
        const std::shared_ptr<ShaderMaterial> material = ShaderMaterialLibrary::GetInstance().Get(assetPath);
        UI::Separator();
        UI::SectionHeader("マテリアル");
        if (!material) {
            WrappedHint("マテリアルとして読めません（JSON を確かめてください）。");
            return true;
        }
        Draw(*material);
        return true;
    }

    bool Create(const std::filesystem::path& folder, const std::string& name, bool withShader,
        std::filesystem::path* created, std::string* error)
    {
        const auto fail = [error](std::string message) {
            if (error) {
                *error = std::move(message);
            }
            return false;
        };
        if (name.empty() || name.find_first_of("\\/:*?\"<>|.") != std::string::npos) {
            return fail("名前に使えない文字があります（\\ / : * ? \" < > | . は使えません）。");
        }
        const std::filesystem::path base =
            folder.is_absolute() ? folder : ProjectPaths::Resolve(Logger::GetInstance().PathToUtf8(folder));
        std::error_code ignored;
        std::filesystem::create_directories(base, ignored);

        const std::filesystem::path materialPath = base / Logger::GetInstance().Utf8ToPath(name + ".mat");
        const std::filesystem::path shaderPath = base / Logger::GetInstance().Utf8ToPath(name + ".PS.hlsl");
        if (std::filesystem::exists(materialPath)) {
            return fail("同じ名前のマテリアルがあります。");
        }
        if (withShader && std::filesystem::exists(shaderPath)) {
            return fail("同じ名前のシェーダがあります。");
        }

        json root = json::object();
        root["shader"] = nullptr;
        root["values"] = json::object();
        root["textures"] = json::object();
        if (withShader) {
            const std::string shaderText = ReadText(ProjectPaths::Resolve(kShaderTemplate));
            if (shaderText.empty()) {
                return fail(std::string("シェーダの雛形が見つかりません（") + kShaderTemplate + "）。");
            }
            if (!WriteText(shaderPath, shaderText)) {
                return fail("シェーダを書けません。");
            }
            AssetDatabase::GetInstance().ImportAsset(shaderPath);
            root["shader"] = AssetPathToJson(Logger::GetInstance().PathToUtf8(ProjectPaths::MakeRelative(shaderPath)));
            root["values"] = json::parse(kTemplateValues);
        }
        if (!WriteText(materialPath, root.dump(4) + "\n")) {
            return fail("マテリアルを書けません。");
        }
        AssetDatabase::GetInstance().ImportAsset(materialPath);

        if (created) {
            *created = materialPath;
        }
        return true;
    }
}

#endif // CORE_EDITOR
