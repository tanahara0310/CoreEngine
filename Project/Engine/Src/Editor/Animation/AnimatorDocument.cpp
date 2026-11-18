#include "pch.h"
#include "AnimatorDocument.h"

#ifdef CORE_EDITOR

#include "Editor/Scene/EditorSceneAccess.h"
#include "Editor/Scene/SceneDebugEditor.h"
#include "GameObject/Component/Render/SpriteAnimatorComponent.h"
#include "GameObject/GameObject.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Model/Animation/Animation.h"
#include "Graphics/Model/ModelManager.h"
#include "Utility/JsonManager/JsonManager.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <system_error>

namespace CoreEngine::Editor
{
    namespace
    {
        std::string StemOf(const std::string& assetPath)
        {
            std::string name = AnimatorFileNameOf(assetPath);
            const size_t dot = name.find_last_of('.');
            return dot == std::string::npos ? name : name.substr(0, dot);
        }

        const char* TypeBaseName(AnimatorParameterType type)
        {
            switch (type) {
            case AnimatorParameterType::Float: return "Value";
            case AnimatorParameterType::Int: return "Count";
            case AnimatorParameterType::Bool: return "Flag";
            case AnimatorParameterType::Trigger: return "Trigger";
            }
            return "Parameter";
        }
    }

    int AnimatorStateIndexOf(const AnimatorLayer& layer, const std::string& name)
    {
        for (size_t i = 0; i < layer.states.size(); ++i) {
            if (layer.states[i].name == name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    std::string AnimatorFileNameOf(const std::string& assetPath)
    {
        const size_t slash = assetPath.find_last_of("/\\");
        return slash == std::string::npos ? assetPath : assetPath.substr(slash + 1);
    }

    // ---------------------------------------------------------------- 読み書き

    std::unique_ptr<AnimatorDocument> AnimatorDocument::Load(const std::filesystem::path& sourceFile, ModelManager* modelManager)
    {
        Logger& log = Logger::GetInstance();
        const json root = JsonManager::ParseTextFile(sourceFile);
        AnimatorControllerData loaded;
        std::vector<std::string> warnings;
        if (root.is_discarded() || !AnimatorControllerFormat::Read(root, loaded, &warnings)) {
            log.Warnf(LogCategory::Resource, "アニメーター設定を開けません: {}", log.PathToUtf8(sourceFile));
            return nullptr;
        }
        if (loaded.layers.empty()) {
            loaded.layers.emplace_back();
        }

        auto document = std::make_unique<AnimatorDocument>();
        document->file = sourceFile;
        document->assetPath = ToAssetPath(sourceFile);
        document->title = log.PathToUtf8(sourceFile.stem());
        document->data = std::move(loaded);
        document->models = modelManager;
        document->currentText = ToText(document->data);
        document->savedText = document->currentText;
        document->committedText = document->currentText;
        for (const std::string& warning : warnings) {
            document->notice += (document->notice.empty() ? "" : " / ") + warning;
        }
        document->selectOnNextFrame = true;
        return document;
    }

    std::string AnimatorDocument::ToText(const AnimatorControllerData& controller)
    {
        return JsonManager::ToFileText(AnimatorControllerFormat::Write(controller));
    }

    bool AnimatorDocument::WriteText(const std::filesystem::path& target, const std::string& text)
    {
        std::error_code ec;
        std::filesystem::create_directories(target.parent_path(), ec);
        std::filesystem::path temporary = target;
        temporary += L".tmp";
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out << text;
            out.close();
            if (!out) {
                std::filesystem::remove(temporary, ec);
                return false;
            }
        }
        std::filesystem::rename(temporary, target, ec);
        if (ec) {
            std::filesystem::remove(temporary, ec);
            return false;
        }
        return true;
    }

    std::string AnimatorDocument::ToAssetPath(const std::filesystem::path& fullPath)
    {
        const std::filesystem::path relative = ProjectPaths::MakeRelative(fullPath);
        std::string text = Logger::GetInstance().PathToUtf8(relative.empty() ? fullPath : relative);
        std::replace(text.begin(), text.end(), '\\', '/');
        return text;
    }

    bool AnimatorDocument::Restore(const std::string& text)
    {
        const json root = json::parse(text, nullptr, false);
        AnimatorControllerData restored;
        if (root.is_discarded() || !AnimatorControllerFormat::Read(root, restored, nullptr)) {
            return false;
        }
        if (restored.layers.empty()) {
            restored.layers.emplace_back();
        }
        data = std::move(restored);
        currentText = ToText(data);
        committedText = currentText;
        pendingLabel.clear();
        linkFrom.reset();
        dragging = {};
        nameEditId = 0;
        activeLayer = std::clamp(activeLayer, 0, static_cast<int>(data.layers.size()) - 1);
        const AnimatorLayer& layer = Layer();
        const bool stateGone = selection == AnimatorSelection::State
            && (selected < 0 || selected >= static_cast<int>(layer.states.size()));
        const bool transitionGone = selection == AnimatorSelection::Transition
            && (selected < 0 || selected >= static_cast<int>(layer.transitions.size()));
        const bool layerGone = selection == AnimatorSelection::Layer
            && (selected < 0 || selected >= static_cast<int>(data.layers.size()));
        if (stateGone || transitionGone || layerGone) {
            Select(AnimatorSelection::None, -1);
        }
        return true;
    }

    void AnimatorDocument::Save()
    {
        Logger& log = Logger::GetInstance();
        const std::string text = ToText(data);
        if (!WriteText(file, text)) {
            notice = "保存できません（ファイルを書けません）。";
            log.Warnf(LogCategory::Resource, "アニメーター設定「{}」: {}", title, notice);
            return;
        }
        currentText = text;
        savedText = text;
        AnimatorControllerLibrary::Get().Store(assetPath, data);
        AssetDatabase::GetInstance().ImportAsset(file);
        notice = "保存しました。";
        log.Infof(LogCategory::Resource, "アニメーター設定「{}」を保存しました", title);
    }

    void AnimatorDocument::MarkChanged(const char* label)
    {
        if (pendingLabel.empty()) {
            pendingLabel = label;
        }
    }

    const std::vector<std::string>& AnimatorDocument::ClipNamesOf(const std::string& clipFile)
    {
        const auto found = clipNames.find(clipFile);
        if (found != clipNames.end()) {
            return found->second;
        }
        std::vector<std::string> names;
        if (models && !clipFile.empty()) {
            names = models->GetClipNames(clipFile);
        }
        return clipNames.emplace(clipFile, std::move(names)).first->second;
    }

    float AnimatorDocument::ClipDurationOf(const AnimatorClip& clip)
    {
        if (clip.IsSprite()) {
            const SpriteAnimatorComponent* const sprite = SelectedSprite();
            const SpriteAnimationClip* const source = sprite ? sprite->FindClip(clip.sprite) : nullptr;
            return source ? SpriteAnimatorComponent::ClipDuration(*source) : 0.0f;
        }
        const std::string key = clip.file + "|" + clip.source;
        if (const auto found = clipDurations.find(key); found != clipDurations.end()) {
            return found->second;
        }
        float duration = 0.0f;
        if (models && !clip.file.empty()) {
            if (const std::shared_ptr<const Animation> animation = models->AcquireClip(clip.file, clip.source)) {
                duration = animation->duration;
            }
        }
        return clipDurations.emplace(key, duration).first->second;
    }

    // ---------------------------------------------------------------- 編集

    void AnimatorDocument::Select(AnimatorSelection kind, int index)
    {
        selection = kind;
        selected = kind == AnimatorSelection::None ? -1 : index;
        nameEditId = 0;
    }

    AnimatorLayer& AnimatorDocument::Layer()
    {
        if (data.layers.empty()) {
            data.layers.emplace_back();
        }
        activeLayer = std::clamp(activeLayer, 0, static_cast<int>(data.layers.size()) - 1);
        return data.layers[static_cast<size_t>(activeLayer)];
    }

    const AnimatorLayer& AnimatorDocument::Layer() const
    {
        const int index = std::clamp(activeLayer, 0, static_cast<int>(data.layers.size()) - 1);
        return data.layers[static_cast<size_t>(index)];
    }

    void AnimatorDocument::SetActiveLayer(int layerIndex)
    {
        if (layerIndex < 0 || layerIndex >= static_cast<int>(data.layers.size()) || layerIndex == activeLayer) {
            return;
        }
        activeLayer = layerIndex;
        linkFrom.reset();
        dragging = {};
        frameRequested = true;
        Select(AnimatorSelection::None, -1);
    }

    void AnimatorDocument::AddLayer()
    {
        AnimatorLayer layer;
        layer.name = UniqueLayerName("Layer");
        data.layers.push_back(std::move(layer));
        const int index = static_cast<int>(data.layers.size()) - 1;
        SetActiveLayer(index);
        Select(AnimatorSelection::Layer, index);
        MarkChanged("レイヤーを足す");
    }

    void AnimatorDocument::DeleteLayer(int layerIndex)
    {
        if (layerIndex <= 0 || layerIndex >= static_cast<int>(data.layers.size())) {
            return;
        }
        data.layers.erase(data.layers.begin() + layerIndex);
        if (activeLayer >= layerIndex) {
            activeLayer = (std::max)(0, activeLayer - 1);
        }
        linkFrom.reset();
        dragging = {};
        frameRequested = true;
        Select(AnimatorSelection::None, -1);
        MarkChanged("レイヤーを消す");
    }

    void AnimatorDocument::RenameLayer(int layerIndex, const std::string& newName)
    {
        if (newName.empty()) {
            notice = "レイヤーの名前が空です。";
            return;
        }
        for (const AnimatorLayer& layer : data.layers) {
            if (layer.name == newName) {
                notice = "同じ名前のレイヤーがあります: " + newName;
                return;
            }
        }
        data.layers[static_cast<size_t>(layerIndex)].name = newName;
        MarkChanged("レイヤーの名前を変える");
    }

    void AnimatorDocument::MoveLayer(int layerIndex, int direction)
    {
        const int target = layerIndex + direction;
        const int count = static_cast<int>(data.layers.size());
        if (layerIndex <= 0 || layerIndex >= count || target <= 0 || target >= count) {
            return;
        }
        std::swap(data.layers[static_cast<size_t>(layerIndex)], data.layers[static_cast<size_t>(target)]);
        if (activeLayer == layerIndex) {
            activeLayer = target;
        } else if (activeLayer == target) {
            activeLayer = layerIndex;
        }
        if (selection == AnimatorSelection::Layer) {
            if (selected == layerIndex) {
                selected = target;
            } else if (selected == target) {
                selected = layerIndex;
            }
        }
        MarkChanged("レイヤーの順を変える");
    }

    int AnimatorDocument::AddState(const Vector2& position, const std::string& clip)
    {
        AnimatorLayer& layer = Layer();
        AnimatorState state;
        state.name = UniqueStateName(clip.empty() ? std::string("State") : clip);
        state.clip = clip;
        state.position = Vector2{ std::round(position.x), std::round(position.y) };
        if (layer.defaultState.empty()) {
            layer.defaultState = state.name;
        }
        layer.states.push_back(std::move(state));
        MarkChanged("状態を作る");
        return static_cast<int>(layer.states.size()) - 1;
    }

    void AnimatorDocument::AddTransition(int from, int to)
    {
        AnimatorLayer& layer = Layer();
        if (from == to || to < 0 || to >= static_cast<int>(layer.states.size()) || from >= static_cast<int>(layer.states.size())) {
            return;
        }
        AnimatorTransition transition;
        transition.from = from < 0 ? std::string(AnimatorControllerFormat::kAnyState) : layer.states[static_cast<size_t>(from)].name;
        transition.to = layer.states[static_cast<size_t>(to)].name;
        transition.hasExitTime = from >= 0;
        layer.transitions.push_back(std::move(transition));
        Select(AnimatorSelection::Transition, static_cast<int>(layer.transitions.size()) - 1);
        MarkChanged("遷移を引く");
    }

    void AnimatorDocument::DeleteSelection()
    {
        switch (selection) {
        case AnimatorSelection::State:
            DeleteState(selected);
            break;
        case AnimatorSelection::Transition:
            DeleteTransition(selected);
            break;
        case AnimatorSelection::Layer:
            DeleteLayer(selected);
            break;
        case AnimatorSelection::AnyState:
        case AnimatorSelection::None:
            break;
        }
    }

    void AnimatorDocument::DeleteState(int stateIndex)
    {
        AnimatorLayer& layer = Layer();
        if (stateIndex < 0 || stateIndex >= static_cast<int>(layer.states.size())) {
            return;
        }
        const std::string name = layer.states[static_cast<size_t>(stateIndex)].name;
        std::erase_if(layer.transitions, [&](const AnimatorTransition& transition) {
            return transition.from == name || transition.to == name;
            });
        layer.states.erase(layer.states.begin() + stateIndex);
        if (layer.defaultState == name) {
            layer.defaultState = layer.states.empty() ? std::string() : layer.states.front().name;
        }
        Select(AnimatorSelection::None, -1);
        MarkChanged("状態を消す");
    }

    void AnimatorDocument::DeleteTransition(int transitionIndex)
    {
        AnimatorLayer& layer = Layer();
        if (transitionIndex < 0 || transitionIndex >= static_cast<int>(layer.transitions.size())) {
            return;
        }
        layer.transitions.erase(layer.transitions.begin() + transitionIndex);
        Select(AnimatorSelection::None, -1);
        MarkChanged("遷移を消す");
    }

    void AnimatorDocument::SetDefaultState(int stateIndex)
    {
        AnimatorLayer& layer = Layer();
        if (stateIndex < 0 || stateIndex >= static_cast<int>(layer.states.size())) {
            return;
        }
        layer.defaultState = layer.states[static_cast<size_t>(stateIndex)].name;
        MarkChanged("最初の状態を変える");
    }

    void AnimatorDocument::RenameState(int stateIndex, const std::string& newName)
    {
        AnimatorLayer& layer = Layer();
        if (newName.empty() || newName == AnimatorControllerFormat::kAnyState) {
            notice = "状態の名前に使えません。";
            return;
        }
        if (AnimatorStateIndexOf(layer, newName) >= 0) {
            notice = "同じ名前の状態があります: " + newName;
            return;
        }
        AnimatorState& state = layer.states[static_cast<size_t>(stateIndex)];
        const std::string oldName = state.name;
        state.name = newName;
        for (AnimatorTransition& transition : layer.transitions) {
            if (transition.from == oldName) {
                transition.from = newName;
            }
            if (transition.to == oldName) {
                transition.to = newName;
            }
        }
        if (layer.defaultState == oldName) {
            layer.defaultState = newName;
        }
        MarkChanged("状態の名前を変える");
    }

    void AnimatorDocument::AddParameter(AnimatorParameterType type)
    {
        AnimatorParameter parameter;
        parameter.name = UniqueParameterName(TypeBaseName(type));
        parameter.type = type;
        data.parameters.push_back(std::move(parameter));
        MarkChanged("パラメータを足す");
    }

    void AnimatorDocument::DeleteParameter(int parameterIndex)
    {
        data.parameters.erase(data.parameters.begin() + static_cast<std::ptrdiff_t>(parameterIndex));
        MarkChanged("パラメータを消す");
    }

    void AnimatorDocument::RenameParameter(int parameterIndex, const std::string& newName)
    {
        if (newName.empty()) {
            notice = "パラメータの名前が空です。";
            return;
        }
        if (data.FindParameter(newName)) {
            notice = "同じ名前のパラメータがあります: " + newName;
            return;
        }
        AnimatorParameter& parameter = data.parameters[static_cast<size_t>(parameterIndex)];
        const std::string oldName = parameter.name;
        parameter.name = newName;
        for (AnimatorLayer& layer : data.layers) {
            for (AnimatorTransition& transition : layer.transitions) {
                for (AnimatorCondition& condition : transition.conditions) {
                    if (condition.parameter == oldName) {
                        condition.parameter = newName;
                    }
                }
            }
            for (AnimatorState& state : layer.states) {
                if (state.blend.parameterX == oldName) {
                    state.blend.parameterX = newName;
                }
                if (state.blend.parameterY == oldName) {
                    state.blend.parameterY = newName;
                }
            }
        }
        if (const auto it = previewFloats.find(oldName); it != previewFloats.end()) {
            const float value = it->second;
            previewFloats.erase(it);
            previewFloats[newName] = value;
        }
        MarkChanged("パラメータの名前を変える");
    }

    float AnimatorDocument::PreviewFloat(const std::string& parameterName) const
    {
        if (const auto it = previewFloats.find(parameterName); it != previewFloats.end()) {
            return it->second;
        }
        const AnimatorParameter* const parameter = data.FindParameter(parameterName);
        return parameter && parameter->type == AnimatorParameterType::Float ? parameter->defaultFloat : 0.0f;
    }

    void AnimatorDocument::AddEmptyClip()
    {
        AnimatorClip clip;
        clip.id = UniqueClipId("Clip");
        clip.file = data.previewModel;
        data.clips.push_back(std::move(clip));
        MarkChanged("クリップを足す");
    }

    void AnimatorDocument::DeleteClip(int clipIndex)
    {
        const std::string id = data.clips[static_cast<size_t>(clipIndex)].id;
        for (AnimatorLayer& layer : data.layers) {
            for (AnimatorState& state : layer.states) {
                if (state.clip == id) {
                    state.clip.clear();
                }
                for (AnimatorBlendChild& child : state.blend.children) {
                    if (child.clip == id) {
                        child.clip.clear();
                    }
                }
            }
        }
        data.clips.erase(data.clips.begin() + clipIndex);
        MarkChanged("クリップを消す");
    }

    void AnimatorDocument::RenameClip(int clipIndex, const std::string& newName)
    {
        if (newName.empty()) {
            notice = "クリップの名前が空です。";
            return;
        }
        if (data.FindClip(newName)) {
            notice = "同じ名前のクリップがあります: " + newName;
            return;
        }
        AnimatorClip& clip = data.clips[static_cast<size_t>(clipIndex)];
        const std::string oldName = clip.id;
        clip.id = newName;
        for (AnimatorLayer& layer : data.layers) {
            for (AnimatorState& state : layer.states) {
                if (state.clip == oldName) {
                    state.clip = newName;
                }
                for (AnimatorBlendChild& child : state.blend.children) {
                    if (child.clip == oldName) {
                        child.clip = newName;
                    }
                }
            }
        }
        MarkChanged("クリップの名前を変える");
    }

    void AnimatorDocument::AddClipsFromModel(const std::string& modelAssetPath, const Vector2* position)
    {
        clipNames.erase(modelAssetPath);
        const std::vector<std::string> names = ClipNamesOf(modelAssetPath);
        if (data.previewModel.empty()) {
            data.previewModel = modelAssetPath;
            MarkChanged("見本のモデルを変える");
        }
        if (names.empty()) {
            notice = "「" + AnimatorFileNameOf(modelAssetPath) + "」にアニメーションがありません。";
            return;
        }
        int column = 0;
        int added = 0;
        for (const std::string& name : names) {
            std::string id;
            for (const AnimatorClip& clip : data.clips) {
                if (clip.file == modelAssetPath && clip.source == name) {
                    id = clip.id;
                    break;
                }
            }
            if (id.empty()) {
                AnimatorClip clip;
                clip.id = UniqueClipId(name.empty() ? StemOf(modelAssetPath) : name);
                clip.file = modelAssetPath;
                clip.source = name;
                id = clip.id;
                data.clips.push_back(std::move(clip));
                ++added;
            }
            if (position) {
                const Vector2 at{ position->x + static_cast<float>(column % 3) * 210.0f,
                    position->y + static_cast<float>(column / 3) * 90.0f };
                AddState(at, id);
                ++column;
            }
        }
        MarkChanged("モデルからクリップを足す");
        notice = "「" + AnimatorFileNameOf(modelAssetPath) + "」から " + std::to_string(names.size()) + " 本（新しいクリップ "
            + std::to_string(added) + " 本）";
        if (position) {
            notice += "・状態 " + std::to_string(column) + " 個";
        }
        notice += "を足しました。";
    }

    void AnimatorDocument::AddClipsFromSprite(const SpriteAnimatorComponent& sprite, const Vector2* position)
    {
        const std::vector<SpriteAnimationClip>& sources = sprite.GetClips();
        if (sources.empty()) {
            notice = "スプライトアニメにクリップがありません。";
            return;
        }
        int column = 0;
        int added = 0;
        for (const SpriteAnimationClip& source : sources) {
            std::string id;
            for (const AnimatorClip& clip : data.clips) {
                if (clip.sprite == source.name) {
                    id = clip.id;
                    break;
                }
            }
            if (id.empty()) {
                AnimatorClip clip;
                clip.id = UniqueClipId(source.name.empty() ? std::string("Sprite") : source.name);
                clip.sprite = source.name;
                id = clip.id;
                data.clips.push_back(std::move(clip));
                ++added;
            }
            if (position) {
                const Vector2 at{ position->x + static_cast<float>(column % 3) * 210.0f,
                    position->y + static_cast<float>(column / 3) * 90.0f };
                AddState(at, id);
                ++column;
            }
        }
        MarkChanged("スプライトアニメからクリップを足す");
        notice = "スプライトアニメから " + std::to_string(sources.size()) + " 本（新しいクリップ " + std::to_string(added) + " 本）";
        if (position) {
            notice += "・状態 " + std::to_string(column) + " 個";
        }
        notice += "を足しました。";
    }

    const SpriteAnimatorComponent* AnimatorDocument::SelectedSprite()
    {
        SceneDebugEditor* const sceneEditor = SceneAccess::SceneEditor();
        const GameObject* const object = sceneEditor ? sceneEditor->GetSelectedObject() : nullptr;
        return object ? object->GetComponent<SpriteAnimatorComponent>() : nullptr;
    }

    void AnimatorDocument::MoveTransition(int transitionIndex, int direction)
    {
        AnimatorLayer& layer = Layer();
        const int count = static_cast<int>(layer.transitions.size());
        if (transitionIndex < 0 || transitionIndex >= count) {
            return;
        }
        const std::string& from = layer.transitions[static_cast<size_t>(transitionIndex)].from;
        int neighbor = transitionIndex + direction;
        while (neighbor >= 0 && neighbor < count && layer.transitions[static_cast<size_t>(neighbor)].from != from) {
            neighbor += direction;
        }
        if (neighbor < 0 || neighbor >= count) {
            return;
        }
        std::swap(layer.transitions[static_cast<size_t>(transitionIndex)], layer.transitions[static_cast<size_t>(neighbor)]);
        if (selection == AnimatorSelection::Transition) {
            if (selected == transitionIndex) {
                selected = neighbor;
            } else if (selected == neighbor) {
                selected = transitionIndex;
            }
        }
        MarkChanged("遷移の順を変える");
    }

    std::string AnimatorDocument::UniqueStateName(const std::string& base) const
    {
        const AnimatorLayer& layer = Layer();
        if (AnimatorStateIndexOf(layer, base) < 0 && base != AnimatorControllerFormat::kAnyState) {
            return base;
        }
        for (int n = 2;; ++n) {
            const std::string candidate = base + " " + std::to_string(n);
            if (AnimatorStateIndexOf(layer, candidate) < 0) {
                return candidate;
            }
        }
    }

    std::string AnimatorDocument::UniqueClipId(const std::string& base) const
    {
        if (!data.FindClip(base)) {
            return base;
        }
        for (int n = 2;; ++n) {
            const std::string candidate = base + " " + std::to_string(n);
            if (!data.FindClip(candidate)) {
                return candidate;
            }
        }
    }

    std::string AnimatorDocument::UniqueLayerName(const std::string& base) const
    {
        const auto taken = [this](const std::string& name) {
            return std::any_of(data.layers.begin(), data.layers.end(), [&](const AnimatorLayer& layer) { return layer.name == name; });
            };
        if (!taken(base)) {
            return base;
        }
        for (int n = 2;; ++n) {
            const std::string candidate = base + " " + std::to_string(n);
            if (!taken(candidate)) {
                return candidate;
            }
        }
    }

    std::string AnimatorDocument::UniqueParameterName(const std::string& base) const
    {
        if (!data.FindParameter(base)) {
            return base;
        }
        for (int n = 2;; ++n) {
            const std::string candidate = base + std::to_string(n);
            if (!data.FindParameter(candidate)) {
                return candidate;
            }
        }
    }
}

#endif // CORE_EDITOR
