#include "pch.h"
#include "Editor/Script/EditorGUIState.h"

#ifdef CORE_EDITOR

#include "Editor/Inspector/InspectorLayout.h"
#include "Editor/Inspector/InspectorRenderer.h"
#include "Editor/Scene/EditorSceneAccess.h"
#include "Editor/Script/EditorScriptBinding.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Asset/AssetInfo.h"
#include "Graphics/Asset/AssetRef.h"
#include "Graphics/Asset/AssetType.h"
#include "Graphics/Texture/TextureManager.h"
#include "Math/Vector/Vector2.h"
#include "Script/Binding/BindingRegistrar.h"
#include "Script/Binding/GameObjectBinding.h"
#include "Utility/Logger/Logger.h"

#include <angelscript.h>
#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CoreEngine::Editor::ScriptBinding
{
    namespace
    {
        using Script::ScriptGameObject;

        /// @brief 一覧の絞り込みの文字の長さ
        constexpr std::size_t kFilterLength = 128;

        /// @brief 読み込んだテクスチャ 1 枚
        struct EditorTexture
        {
            ImTextureID id = 0;
            float width = 0.0f;
            float height = 0.0f;
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        };

        /// パスごとの読み込んだテクスチャ（読めなかったものは空）
        std::unordered_map<std::string, std::optional<EditorTexture>> g_textures;

        /// @brief テクスチャを読み込む（同じパスは 2 回目から控えを返す）
        /// @return 読めなければ nullptr
        const EditorTexture* LoadEditorTexture(const std::string& path)
        {
            if (const auto found = g_textures.find(path); found != g_textures.end()) {
                return found->second ? &*found->second : nullptr;
            }

            std::optional<EditorTexture> loaded;
            if (!path.empty()) {
                try {
                    TextureManager& textures = TextureManager::GetInstance();
                    const TextureManager::LoadedTexture texture = textures.Load(path);
                    if (texture.texture) {
                        const DirectX::TexMetadata metadata = textures.GetMetadata(path);
                        loaded = EditorTexture{
                            static_cast<ImTextureID>(texture.gpuHandle.ptr),
                            static_cast<float>(metadata.width), static_cast<float>(metadata.height), texture.texture };
                    }
                } catch (const std::exception& exception) {
                    Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Script,
                        "EditorGUI: 画像 {} を読めません（{}）", path, exception.what());
                }
            }
            const auto [inserted, added] = g_textures.emplace(path, std::move(loaded));
            return inserted->second ? &*inserted->second : nullptr;
        }

        /// @brief 幅と高さの片方か両方が 0 以下なら、画像の縦横の比を保って埋める
        ImVec2 FitImageSize(const EditorTexture& texture, float width, float height)
        {
            const float aspect = texture.height > 0.0f ? texture.width / texture.height : 1.0f;
            if (width <= 0.0f && height <= 0.0f) {
                return ImVec2(texture.width, texture.height);
            }
            if (width <= 0.0f) {
                return ImVec2(height * aspect, height);
            }
            if (height <= 0.0f) {
                return ImVec2(width, aspect > 0.0f ? width / aspect : width);
            }
            return ImVec2(width, height);
        }

        /// @brief ASCII の大文字と小文字を区別せずに部分一致するか
        bool ContainsIgnoreCase(std::string_view text, std::string_view pattern)
        {
            if (pattern.empty()) {
                return true;
            }
            const auto lower = [](char c) {
                return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
                };
            const auto found = std::search(text.begin(), text.end(), pattern.begin(), pattern.end(),
                [&lower](char a, char b) { return lower(a) == lower(b); });
            return found != text.end();
        }

        /// @brief 参照の欄の枠を開き、中身（記号・名前）を枠に重ねて描く
        /// @return 一覧が開いたら true（EndCombo を呼ぶ）
        bool BeginReferenceCombo(const std::string& label, const char* icon, const std::string& name, bool invalid)
        {
            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const float frameHeight = ImGui::GetFrameHeight();
            const ImVec2 max(min.x + ImGui::CalcItemWidth() - frameHeight, min.y + frameHeight);
            const bool open = ImGui::BeginCombo(LabelFieldId(label).c_str(), "");
            InspectorLayout::DrawReferencePreview(drawList, min, max, icon, name.c_str(), nullptr, invalid);
            return open;
        }

        /// @brief 一覧の先頭の絞り込み欄（一覧を開いたときに空にする）
        std::string DrawFilter(char (&buffer)[kFilterLength])
        {
            if (ImGui::IsWindowAppearing()) {
                buffer[0] = '\0';
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##filter", "絞り込み", buffer, kFilterLength);
            return buffer;
        }

        /// @brief ProjectView からドラッグされるファイルのペイロード名（中身はファイル名）
        const char* FileDragPayloadOf(AssetType type)
        {
            switch (type) {
            case AssetType::Texture: return "TEXTURE_FILE";
            case AssetType::Model:   return "MODEL_FILE";
            case AssetType::Audio:   return "AUDIO_FILE";
            case AssetType::Prefab:  return "PREFAB_FILE";
            default:                 return nullptr;
            }
        }

        /// @brief 種類の名前を読む（空なら Unknown＝どの種類でもよい）
        /// @return 知らない名前なら空（スクリプトの例外にする）
        std::optional<AssetType> ParseAssetType(const std::string& type, const char* function)
        {
            if (type.empty()) {
                return AssetType::Unknown;
            }
            const AssetType assetType = StringToAssetType(type);
            if (assetType == AssetType::Unknown) {
                ThrowScriptException(std::string("EditorGUI::") + function + " の種類 " + type
                    + " はありません（Texture・Model・Audio・Prefab・Json・Csv など）");
                return std::nullopt;
            }
            return assetType;
        }

        // ---------------------------------------------------------------- オブジェクトの欄

        ScriptGameObject* ObjectField(const std::string& label, ScriptGameObject* value)
        {
            const auto keep = [value]() -> ScriptGameObject* {
                if (value) {
                    value->AddRef();
                }
                return value;
                };
            WidgetScope widget("ObjectField");
            if (!widget) {
                return keep();
            }

            const GameObjectManager* const objects = SceneAccess::Objects();
            const GameObject* const current = value ? value->Resolve() : nullptr;
            const bool missing = value && !current;
            const std::string name = current ? std::string(current->GetDisplayName()) : (missing ? "（見つかりません）" : "（なし）");

            BeginLabeledRow(label);
            const GameObject* chosen = current;
            bool picked = false;
            if (BeginReferenceCombo(label, current ? "◆" : nullptr, name, missing)) {
                static char filter[kFilterLength] = "";
                const std::string pattern = DrawFilter(filter);
                if (ImGui::Selectable("（なし）", !current)) {
                    chosen = nullptr;
                    picked = true;
                }
                if (objects) {
                    for (const auto& object : objects->GetAllObjects()) {
                        if (!object || object->IsMarkedForDestroy() || !ContainsIgnoreCase(object->GetDisplayName(), pattern)) {
                            continue;
                        }
                        ImGui::PushID(object.get());
                        if (ImGui::Selectable(object->GetDisplayName(), object.get() == current)) {
                            chosen = object.get();
                            picked = true;
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::EndCombo();
            } else if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* const payload = ImGui::AcceptDragDropPayload(InspectorRenderer::kObjectDragPayload)) {
                    std::uint64_t droppedId = 0;
                    if (objects && payload->DataSize == sizeof(droppedId)) {
                        std::memcpy(&droppedId, payload->Data, sizeof(droppedId));
                        chosen = objects->FindObject(ObjectId{ droppedId });
                        picked = chosen != nullptr;
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (!picked || chosen == current) {
                return keep();
            }
            FrameState().changed = true;
            return ScriptGameObject::CreateForObject(chosen);
        }

        // ---------------------------------------------------------------- アセットの欄

        std::string AssetField(const std::string& label, const std::string& path, const std::string& type)
        {
            WidgetScope widget("AssetField");
            if (!widget) {
                return path;
            }
            const std::optional<AssetType> assetType = ParseAssetType(type, "AssetField");
            if (!assetType) {
                return path;
            }

            const AssetInfo* const current = path.empty() ? nullptr : FindAssetInfo(path, *assetType);
            const bool missing = !path.empty() && !current;
            const std::string name = current ? current->fileName : (missing ? "（見つかりません）" : "（なし）");
            const AssetType iconType = current ? current->type : *assetType;

            BeginLabeledRow(label);
            std::string chosen = path;
            if (BeginReferenceCombo(label, iconType == AssetType::Unknown ? nullptr : InspectorLayout::AssetGlyph(iconType), name, missing)) {
                static char filter[kFilterLength] = "";
                const std::string pattern = DrawFilter(filter);
                if (ImGui::Selectable("（なし）", path.empty())) {
                    chosen.clear();
                }
                std::vector<AssetType> types;
                if (*assetType == AssetType::Unknown) {
                    for (int value = static_cast<int>(AssetType::Texture); value <= static_cast<int>(AssetType::PhysicsMaterial); ++value) {
                        types.push_back(static_cast<AssetType>(value));
                    }
                } else {
                    types.push_back(*assetType);
                }
                for (const AssetType listed : types) {
                    for (const AssetInfo* const info : AssetDatabase::GetInstance().GetAssetsOfType(listed)) {
                        const std::string infoPath = ToAssetPath(*info);
                        if (!ContainsIgnoreCase(infoPath, pattern)) {
                            continue;
                        }
                        ImGui::PushID(info->guid.c_str());
                        if (ImGui::Selectable(info->fileName.c_str(), info == current)) {
                            chosen = infoPath;
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s", infoPath.c_str());
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::EndCombo();
            } else {
                if (ImGui::BeginDragDropTarget()) {
                    const AssetType payloadTypes[] = { AssetType::Texture, AssetType::Model, AssetType::Audio, AssetType::Prefab };
                    for (const AssetType payloadType : payloadTypes) {
                        if (*assetType != AssetType::Unknown && *assetType != payloadType) {
                            continue;
                        }
                        if (const ImGuiPayload* const payload = ImGui::AcceptDragDropPayload(FileDragPayloadOf(payloadType))) {
                            const auto* const data = static_cast<const char*>(payload->Data);
                            const std::string fileName(data, strnlen(data, static_cast<std::size_t>(payload->DataSize)));
                            if (const AssetInfo* const dropped = FindAssetInfo(fileName, payloadType)) {
                                chosen = ToAssetPath(*dropped);
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                if (ImGui::IsItemHovered() && !path.empty()) {
                    ImGui::SetTooltip("%s", path.c_str());
                }
            }

            if (chosen != path) {
                FrameState().changed = true;
            }
            return chosen;
        }

        // ---------------------------------------------------------------- 画像

        void Image(const std::string& path, float width, float height)
        {
            WidgetScope widget("Image");
            if (!widget) {
                return;
            }
            const EditorTexture* const texture = LoadEditorTexture(path);
            if (!texture) {
                ImGui::TextDisabled("（画像を読めません: %s）", path.c_str());
                return;
            }
            ImGui::Image(texture->id, FitImageSize(*texture, width, height));
        }

        Vector2 GetImageSize(const std::string& path)
        {
            if (!RequireGUI("GetImageSize")) {
                return Vector2{};
            }
            const EditorTexture* const texture = LoadEditorTexture(path);
            return texture ? Vector2{ texture->width, texture->height } : Vector2{};
        }

        void DrawImage(const std::string& path, const Vector2& min, const Vector2& max)
        {
            if (!RequireGUI("DrawImage")) {
                return;
            }
            const GUIFrameState& state = FrameState();
            if (!state.hasCanvas) {
                ThrowScriptException("EditorGUI::DrawImage は EditorGUI::Canvas の後で呼びます");
                return;
            }
            const EditorTexture* const texture = LoadEditorTexture(path);
            if (!texture) {
                return;
            }
            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(state.canvasMin, state.canvasMax, true);
            drawList->AddImage(texture->id,
                ImVec2(state.canvasMin.x + min.x, state.canvasMin.y + min.y),
                ImVec2(state.canvasMin.x + max.x, state.canvasMin.y + max.y));
            drawList->PopClipRect();
        }
    }

    void ReleaseEditorGUITextures()
    {
        g_textures.clear();
    }

    void RegisterEditorGUIAssets(Script::BindingRegistrar& r)
    {
        r.Function("GameObject@ ObjectField(const string &in label, GameObject@+ value)", asFUNCTION(ObjectField));
        r.Function("string AssetField(const string &in label, const string &in path, const string &in type = \"\")", asFUNCTION(AssetField));
        r.Function("void Image(const string &in path, float width = 0, float height = 0)", asFUNCTION(Image));
        r.Function("Vector2 GetImageSize(const string &in path)", asFUNCTION(GetImageSize));
        r.Function("void DrawImage(const string &in path, const Vector2 &in min, const Vector2 &in max)", asFUNCTION(DrawImage));
    }
}

#endif // CORE_EDITOR
