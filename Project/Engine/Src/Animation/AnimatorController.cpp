#include "pch.h"
#include "AnimatorController.h"

#include "Graphics/Asset/AssetRef.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"

#include <algorithm>
#include <cwctype>
#include <utility>

namespace CoreEngine
{
    namespace
    {
        constexpr std::pair<AnimatorParameterType, const char*> kParameterTypeNames[] = {
            { AnimatorParameterType::Float, "Float" },
            { AnimatorParameterType::Int, "Int" },
            { AnimatorParameterType::Bool, "Bool" },
            { AnimatorParameterType::Trigger, "Trigger" },
        };

        constexpr std::pair<AnimatorConditionOp, const char*> kConditionOpNames[] = {
            { AnimatorConditionOp::Greater, ">" },
            { AnimatorConditionOp::Less, "<" },
            { AnimatorConditionOp::Equal, "==" },
            { AnimatorConditionOp::NotEqual, "!=" },
            { AnimatorConditionOp::True, "true" },
            { AnimatorConditionOp::False, "false" },
            { AnimatorConditionOp::Trigger, "trigger" },
        };

        template <class T, size_t N>
        bool ParseName(const std::pair<T, const char*> (&names)[N], const std::string& text, T& out)
        {
            for (const auto& [value, name] : names) {
                if (text == name) {
                    out = value;
                    return true;
                }
            }
            return false;
        }

        json Vector2ToJson(const Vector2& value)
        {
            return json::array({ value.x, value.y });
        }

        Vector2 JsonToVector2(const json& node, const Vector2& fallback)
        {
            if (!node.is_array() || node.size() < 2 || !node[0].is_number() || !node[1].is_number()) {
                return fallback;
            }
            return Vector2{ node[0].get<float>(), node[1].get<float>() };
        }

        std::string ReadString(const json& node, const char* key)
        {
            const auto it = node.find(key);
            return it != node.end() && it->is_string() ? it->get<std::string>() : std::string{};
        }

        float ReadFloat(const json& node, const char* key, float fallback)
        {
            const auto it = node.find(key);
            return it != node.end() && it->is_number() ? it->get<float>() : fallback;
        }

        void Warn(std::vector<std::string>* warnings, std::string message)
        {
            if (warnings) {
                warnings->push_back(std::move(message));
            }
        }

        void ReadParameters(const json& root, AnimatorControllerData& out, std::vector<std::string>* warnings)
        {
            const auto it = root.find("parameters");
            if (it == root.end() || !it->is_array()) {
                return;
            }
            for (const json& node : *it) {
                if (!node.is_object()) {
                    continue;
                }
                AnimatorParameter parameter;
                parameter.name = ReadString(node, "name");
                if (parameter.name.empty()) {
                    continue;
                }
                if (!ParseName(kParameterTypeNames, ReadString(node, "type"), parameter.type)) {
                    Warn(warnings, "パラメータ「" + parameter.name + "」の種類が読めないので Float にしました");
                }
                const auto value = node.find("default");
                if (value != node.end()) {
                    if (value->is_boolean()) {
                        parameter.defaultBool = value->get<bool>();
                    } else if (value->is_number()) {
                        parameter.defaultFloat = value->get<float>();
                        parameter.defaultInt = value->is_number_integer() ? value->get<int>()
                                                                          : static_cast<int>(value->get<float>());
                    }
                }
                out.parameters.push_back(std::move(parameter));
            }
        }

        void ReadClips(const json& root, AnimatorControllerData& out)
        {
            const auto it = root.find("clips");
            if (it == root.end() || !it->is_array()) {
                return;
            }
            for (const json& node : *it) {
                if (!node.is_object()) {
                    continue;
                }
                AnimatorClip clip;
                clip.id = ReadString(node, "id");
                if (clip.id.empty()) {
                    continue;
                }
                if (const auto file = node.find("file"); file != node.end()) {
                    clip.file = JsonToAssetPath(*file, "アニメーター設定のクリップ「" + clip.id + "」");
                }
                clip.source = ReadString(node, "source");
                clip.sprite = ReadString(node, "sprite");
                if (const auto events = node.find("events"); events != node.end() && events->is_array()) {
                    for (const json& eventNode : *events) {
                        if (!eventNode.is_object()) {
                            continue;
                        }
                        AnimatorClipEvent clipEvent;
                        clipEvent.time = (std::max)(0.0f, ReadFloat(eventNode, "time", 0.0f));
                        clipEvent.name = ReadString(eventNode, "name");
                        clipEvent.floatValue = ReadFloat(eventNode, "float", 0.0f);
                        if (const auto intValue = eventNode.find("int"); intValue != eventNode.end() && intValue->is_number()) {
                            clipEvent.intValue = intValue->is_number_integer() ? intValue->get<int>()
                                                                               : static_cast<int>(intValue->get<float>());
                        }
                        clipEvent.stringValue = ReadString(eventNode, "string");
                        clip.events.push_back(std::move(clipEvent));
                    }
                }
                out.clips.push_back(std::move(clip));
            }
        }

        void ReadConditions(const json& node, AnimatorTransition& transition, std::vector<std::string>* warnings)
        {
            const auto it = node.find("conditions");
            if (it == node.end() || !it->is_array()) {
                return;
            }
            for (const json& conditionNode : *it) {
                if (!conditionNode.is_object()) {
                    continue;
                }
                AnimatorCondition condition;
                condition.parameter = ReadString(conditionNode, "param");
                if (!ParseName(kConditionOpNames, ReadString(conditionNode, "op"), condition.op)) {
                    Warn(warnings, "遷移「" + transition.from + " → " + transition.to + "」の条件の比べ方が読めません");
                    continue;
                }
                condition.value = ReadFloat(conditionNode, "value", 0.0f);
                transition.conditions.push_back(std::move(condition));
            }
        }

        void ReadBlendChildren(const json& node, AnimatorBlendTree& tree)
        {
            const auto children = node.find("children");
            if (children == node.end() || !children->is_array()) {
                return;
            }
            for (const json& childNode : *children) {
                if (!childNode.is_object()) {
                    continue;
                }
                AnimatorBlendChild child;
                child.clip = ReadString(childNode, "clip");
                child.threshold = ReadFloat(childNode, "threshold", 0.0f);
                if (const auto pos = childNode.find("pos"); pos != childNode.end()) {
                    child.position = JsonToVector2(*pos, child.position);
                }
                tree.children.push_back(std::move(child));
            }
        }

        void ReadMotion(const json& motion, AnimatorState& state)
        {
            if (const auto blend = motion.find("blend1D"); blend != motion.end() && blend->is_object()) {
                state.motion = AnimatorMotionType::Blend1D;
                state.blend.parameterX = ReadString(*blend, "param");
                ReadBlendChildren(*blend, state.blend);
                return;
            }
            if (const auto blend = motion.find("blend2D"); blend != motion.end() && blend->is_object()) {
                state.motion = AnimatorMotionType::Blend2D;
                state.blend.parameterX = ReadString(*blend, "paramX");
                state.blend.parameterY = ReadString(*blend, "paramY");
                ReadBlendChildren(*blend, state.blend);
                return;
            }
            state.motion = AnimatorMotionType::Clip;
            state.clip = ReadString(motion, "clip");
        }

        json WriteMotion(const AnimatorState& state)
        {
            switch (state.motion) {
            case AnimatorMotionType::Blend1D: {
                json children = json::array();
                for (const AnimatorBlendChild& child : state.blend.children) {
                    children.push_back({ { "clip", child.clip }, { "threshold", child.threshold } });
                }
                return { { "blend1D", { { "param", state.blend.parameterX }, { "children", std::move(children) } } } };
            }
            case AnimatorMotionType::Blend2D: {
                json children = json::array();
                for (const AnimatorBlendChild& child : state.blend.children) {
                    children.push_back({ { "clip", child.clip }, { "pos", Vector2ToJson(child.position) } });
                }
                return { { "blend2D", { { "paramX", state.blend.parameterX }, { "paramY", state.blend.parameterY },
                                        { "children", std::move(children) } } } };
            }
            case AnimatorMotionType::Clip:
                break;
            }
            return { { "clip", state.clip } };
        }

        void ReadLayer(const json& node, AnimatorLayer& layer, std::vector<std::string>* warnings)
        {
            if (std::string name = ReadString(node, "name"); !name.empty()) {
                layer.name = std::move(name);
            }
            layer.defaultState = ReadString(node, "defaultState");
            if (const auto pos = node.find("anyStatePos"); pos != node.end()) {
                layer.anyStatePosition = JsonToVector2(*pos, layer.anyStatePosition);
            }
            layer.weight = std::clamp(ReadFloat(node, "weight", 1.0f), 0.0f, 1.0f);
            if (const auto mask = node.find("mask"); mask != node.end() && mask->is_array()) {
                layer.useMask = true;
                for (const json& bone : *mask) {
                    if (bone.is_string()) {
                        layer.mask.push_back(bone.get<std::string>());
                    }
                }
            }
            if (const auto states = node.find("states"); states != node.end() && states->is_array()) {
                for (const json& stateNode : *states) {
                    if (!stateNode.is_object()) {
                        continue;
                    }
                    AnimatorState state;
                    state.name = ReadString(stateNode, "name");
                    if (state.name.empty() || layer.FindState(state.name)) {
                        Warn(warnings, "名前が空か重なっている状態を飛ばしました: " + state.name);
                        continue;
                    }
                    if (const auto motion = stateNode.find("motion"); motion != stateNode.end() && motion->is_object()) {
                        ReadMotion(*motion, state);
                    }
                    state.speed = ReadFloat(stateNode, "speed", 1.0f);
                    if (const auto loop = stateNode.find("loop"); loop != stateNode.end() && loop->is_boolean()) {
                        state.loop = loop->get<bool>();
                    }
                    if (const auto pos = stateNode.find("pos"); pos != stateNode.end()) {
                        state.position = JsonToVector2(*pos, state.position);
                    }
                    layer.states.push_back(std::move(state));
                }
            }
            if (const auto transitions = node.find("transitions"); transitions != node.end() && transitions->is_array()) {
                for (const json& transitionNode : *transitions) {
                    if (!transitionNode.is_object()) {
                        continue;
                    }
                    AnimatorTransition transition;
                    transition.from = ReadString(transitionNode, "from");
                    transition.to = ReadString(transitionNode, "to");
                    if (transition.from.empty() || transition.to.empty()) {
                        continue;
                    }
                    ReadConditions(transitionNode, transition, warnings);
                    transition.duration = (std::max)(0.0f, ReadFloat(transitionNode, "duration", transition.duration));
                    if (const auto exit = transitionNode.find("exitTime"); exit != transitionNode.end() && exit->is_number()) {
                        transition.hasExitTime = true;
                        transition.exitTime = (std::max)(0.0f, exit->get<float>());
                    }
                    transition.offset = std::clamp(ReadFloat(transitionNode, "offset", 0.0f), 0.0f, 1.0f);
                    layer.transitions.push_back(std::move(transition));
                }
            }
        }
    }

    const AnimatorState* AnimatorLayer::FindState(std::string_view stateName) const
    {
        for (const AnimatorState& state : states) {
            if (state.name == stateName) {
                return &state;
            }
        }
        return nullptr;
    }

    AnimatorState* AnimatorLayer::FindState(std::string_view stateName)
    {
        return const_cast<AnimatorState*>(std::as_const(*this).FindState(stateName));
    }

    const AnimatorParameter* AnimatorControllerData::FindParameter(std::string_view parameterName) const
    {
        for (const AnimatorParameter& parameter : parameters) {
            if (parameter.name == parameterName) {
                return &parameter;
            }
        }
        return nullptr;
    }

    const AnimatorClip* AnimatorControllerData::FindClip(std::string_view clipId) const
    {
        for (const AnimatorClip& clip : clips) {
            if (clip.id == clipId) {
                return &clip;
            }
        }
        return nullptr;
    }

    namespace AnimatorControllerFormat
    {
        bool Read(const json& root, AnimatorControllerData& out, std::vector<std::string>* warnings)
        {
            if (!root.is_object()) {
                return false;
            }
            out = AnimatorControllerData{};
            if (const auto preview = root.find("previewModel"); preview != root.end()) {
                out.previewModel = JsonToAssetPath(*preview, "アニメーター設定の previewModel");
            }
            out.rootMotionBone = ReadString(root, "rootMotionBone");
            ReadParameters(root, out, warnings);
            ReadClips(root, out);
            if (const auto layers = root.find("layers"); layers != root.end() && layers->is_array()) {
                for (const json& node : *layers) {
                    if (!node.is_object()) {
                        continue;
                    }
                    AnimatorLayer layer;
                    ReadLayer(node, layer, warnings);
                    out.layers.push_back(std::move(layer));
                }
            }
            if (out.layers.empty()) {
                out.layers.emplace_back();
            }
            return true;
        }

        json Write(const AnimatorControllerData& data)
        {
            json root = json::object();
            root["version"] = kVersion;
            root["previewModel"] = AssetPathToJson(data.previewModel);
            if (!data.rootMotionBone.empty()) {
                root["rootMotionBone"] = data.rootMotionBone;
            }

            json parameters = json::array();
            for (const AnimatorParameter& parameter : data.parameters) {
                json node = { { "name", parameter.name }, { "type", ToString(parameter.type) } };
                switch (parameter.type) {
                case AnimatorParameterType::Float: node["default"] = parameter.defaultFloat; break;
                case AnimatorParameterType::Int: node["default"] = parameter.defaultInt; break;
                case AnimatorParameterType::Bool: node["default"] = parameter.defaultBool; break;
                case AnimatorParameterType::Trigger: break;
                }
                parameters.push_back(std::move(node));
            }
            root["parameters"] = std::move(parameters);

            json clips = json::array();
            for (const AnimatorClip& clip : data.clips) {
                json node = clip.sprite.empty()
                    ? json{ { "id", clip.id }, { "file", AssetPathToJson(clip.file) }, { "source", clip.source } }
                    : json{ { "id", clip.id }, { "sprite", clip.sprite } };
                if (!clip.events.empty()) {
                    json events = json::array();
                    for (const AnimatorClipEvent& clipEvent : clip.events) {
                        json eventNode = { { "time", clipEvent.time }, { "name", clipEvent.name } };
                        if (clipEvent.floatValue != 0.0f) {
                            eventNode["float"] = clipEvent.floatValue;
                        }
                        if (clipEvent.intValue != 0) {
                            eventNode["int"] = clipEvent.intValue;
                        }
                        if (!clipEvent.stringValue.empty()) {
                            eventNode["string"] = clipEvent.stringValue;
                        }
                        events.push_back(std::move(eventNode));
                    }
                    node["events"] = std::move(events);
                }
                clips.push_back(std::move(node));
            }
            root["clips"] = std::move(clips);

            json layers = json::array();
            for (const AnimatorLayer& layer : data.layers) {
                json node = { { "name", layer.name }, { "defaultState", layer.defaultState },
                              { "anyStatePos", Vector2ToJson(layer.anyStatePosition) } };
                if (&layer != &data.layers.front()) {
                    node["weight"] = layer.weight;
                    if (layer.useMask) {
                        node["mask"] = layer.mask;
                    }
                }
                json states = json::array();
                for (const AnimatorState& state : layer.states) {
                    states.push_back({
                        { "name", state.name },
                        { "motion", WriteMotion(state) },
                        { "speed", state.speed },
                        { "loop", state.loop },
                        { "pos", Vector2ToJson(state.position) },
                    });
                }
                node["states"] = std::move(states);
                json transitions = json::array();
                for (const AnimatorTransition& transition : layer.transitions) {
                    json conditions = json::array();
                    for (const AnimatorCondition& condition : transition.conditions) {
                        json conditionNode = { { "param", condition.parameter }, { "op", ToString(condition.op) } };
                        if (condition.op == AnimatorConditionOp::Greater || condition.op == AnimatorConditionOp::Less ||
                            condition.op == AnimatorConditionOp::Equal || condition.op == AnimatorConditionOp::NotEqual) {
                            conditionNode["value"] = condition.value;
                        }
                        conditions.push_back(std::move(conditionNode));
                    }
                    json transitionNode = {
                        { "from", transition.from },
                        { "to", transition.to },
                        { "conditions", std::move(conditions) },
                        { "duration", transition.duration },
                    };
                    if (transition.hasExitTime) {
                        transitionNode["exitTime"] = transition.exitTime;
                    }
                    if (transition.offset != 0.0f) {
                        transitionNode["offset"] = transition.offset;
                    }
                    transitions.push_back(std::move(transitionNode));
                }
                node["transitions"] = std::move(transitions);
                layers.push_back(std::move(node));
            }
            root["layers"] = std::move(layers);
            return root;
        }

        AnimatorControllerData MakeEmpty()
        {
            AnimatorControllerData data;
            data.layers.emplace_back();
            return data;
        }

        const char* ToString(AnimatorParameterType type)
        {
            for (const auto& [value, name] : kParameterTypeNames) {
                if (value == type) {
                    return name;
                }
            }
            return "Float";
        }

        const char* ToString(AnimatorConditionOp op)
        {
            for (const auto& [value, name] : kConditionOpNames) {
                if (value == op) {
                    return name;
                }
            }
            return ">";
        }

        bool IsValidOp(AnimatorParameterType type, AnimatorConditionOp op)
        {
            switch (type) {
            case AnimatorParameterType::Float:
                return op == AnimatorConditionOp::Greater || op == AnimatorConditionOp::Less;
            case AnimatorParameterType::Int:
                return op == AnimatorConditionOp::Greater || op == AnimatorConditionOp::Less ||
                    op == AnimatorConditionOp::Equal || op == AnimatorConditionOp::NotEqual;
            case AnimatorParameterType::Bool:
                return op == AnimatorConditionOp::True || op == AnimatorConditionOp::False;
            case AnimatorParameterType::Trigger:
                return op == AnimatorConditionOp::Trigger;
            }
            return false;
        }
    }

    AnimatorControllerLibrary& AnimatorControllerLibrary::Get()
    {
        static AnimatorControllerLibrary instance;
        return instance;
    }

    std::wstring AnimatorControllerLibrary::MakeKey(const std::string& assetPath)
    {
        std::wstring key = ProjectPaths::Resolve(assetPath).lexically_normal().wstring();
        std::replace(key.begin(), key.end(), L'/', L'\\');
        std::transform(key.begin(), key.end(), key.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return key;
    }

    std::shared_ptr<const AnimatorControllerLibrary::Entry> AnimatorControllerLibrary::Load(const std::string& assetPath)
    {
        if (assetPath.empty()) {
            return nullptr;
        }
        const std::wstring key = MakeKey(assetPath);
        if (const auto it = entries_.find(key); it != entries_.end()) {
            return it->second;
        }

        auto entry = std::make_shared<Entry>();
        entry->revision = nextRevision_++;
        const json document = JsonManager::GetInstance().LoadJson(assetPath);
        auto data = std::make_shared<AnimatorControllerData>();
        std::vector<std::string> warnings;
        if (AnimatorControllerFormat::Read(document, *data, &warnings)) {
            entry->data = std::move(data);
        } else {
            Logger::GetInstance().Warnf(LogCategory::Resource, "アニメーター設定を読めません: {}", assetPath);
        }
        for (const std::string& warning : warnings) {
            Logger::GetInstance().Warnf(LogCategory::Resource, "{}: {}", assetPath, warning);
        }
        entries_.emplace(key, entry);
        return entry;
    }

    void AnimatorControllerLibrary::Store(const std::string& assetPath, AnimatorControllerData data)
    {
        if (assetPath.empty()) {
            return;
        }
        std::shared_ptr<Entry>& entry = entries_[MakeKey(assetPath)];
        if (!entry) {
            entry = std::make_shared<Entry>();
        }
        entry->data = std::make_shared<const AnimatorControllerData>(std::move(data));
        entry->revision = nextRevision_++;
    }

    void AnimatorControllerLibrary::Clear()
    {
        entries_.clear();
    }
}
