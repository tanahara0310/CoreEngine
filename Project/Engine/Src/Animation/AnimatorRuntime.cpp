#include "pch.h"
#include "AnimatorRuntime.h"

#include "Animation/BlendTree.h"
#include "Animation/PoseEvaluator.h"
#include "Graphics/Model/Animation/Animation.h"
#include "Graphics/Model/Animation/AnimationUtils.h"
#include "Graphics/Model/Skeleton/Skeleton.h"
#include "Math/MathCore.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <numeric>

namespace CoreEngine
{
    namespace
    {
        constexpr Vector3 kUp{ 0.0f, 1.0f, 0.0f };

        /// @brief 回転の縦の軸まわりのねじれの角度
        float TwistYaw(const Quaternion& rotation)
        {
            return 2.0f * std::atan2(rotation.y, rotation.w);
        }

        Quaternion YawRotation(float yaw)
        {
            return MathCore::QuaternionMath::MakeRotateAxisAngle(kUp, yaw);
        }

        Vector3 RotateYaw(const Vector3& vector, float yaw)
        {
            return MathCore::QuaternionMath::RotateVector(vector, YawRotation(yaw));
        }
    }

    void AnimatorRuntime::Build(std::shared_ptr<const AnimatorControllerData> controller,
        std::vector<std::shared_ptr<const Animation>> clips, const Skeleton* skeleton, std::string ownerName)
    {
        std::unordered_map<std::string, ParameterValue> previous;
        if (controller_) {
            for (size_t i = 0; i < parameters_.size() && i < controller_->parameters.size(); ++i) {
                previous.emplace(controller_->parameters[i].name, parameters_[i]);
            }
        }

        Reset();
        controller_ = std::move(controller);
        clips_ = std::move(clips);
        skeleton_ = skeleton;
        ownerName_ = std::move(ownerName);
        if (!controller_) {
            return;
        }

        parameters_.resize(controller_->parameters.size());
        for (size_t i = 0; i < controller_->parameters.size(); ++i) {
            const AnimatorParameter& parameter = controller_->parameters[i];
            ParameterValue value;
            value.type = parameter.type;
            value.floatValue = parameter.defaultFloat;
            value.intValue = parameter.defaultInt;
            value.boolValue = parameter.type == AnimatorParameterType::Trigger ? false : parameter.defaultBool;
            if (const auto it = previous.find(parameter.name); it != previous.end() && it->second.type == parameter.type) {
                value = it->second;
            }
            parameters_[i] = value;
            parameterIndices_.emplace(parameter.name, static_cast<int>(i));
        }

        if (!skeleton_ || controller_->layers.empty()) {
            return;
        }
        clips_.resize(controller_->clips.size());

        bindings_.resize(clips_.size());
        eventOrder_.resize(clips_.size());
        for (size_t i = 0; i < clips_.size(); ++i) {
            const std::vector<AnimatorClipEvent>& events = controller_->clips[i].events;
            std::vector<int>& order = eventOrder_[i];
            order.resize(events.size());
            std::iota(order.begin(), order.end(), 0);
            std::stable_sort(order.begin(), order.end(), [&events](int a, int b) {
                return events[static_cast<size_t>(a)].time < events[static_cast<size_t>(b)].time;
            });
            if (!clips_[i]) {
                continue;
            }
            bindings_[i] = PoseEvaluator::Bind(*skeleton_, *clips_[i]);
            if (bindings_[i].matchedCount == 0) {
                WarnOnce("bind:" + controller_->clips[i].id,
                    "クリップ「" + controller_->clips[i].id + "」に、このモデルの骨の名前と同じトラックがありません（動きません）");
            }
        }

        const size_t jointCount = skeleton_->joints.size();
        pose_.resize(jointCount);
        blendScratch_.resize(jointCount);
        matrices_.resize(jointCount);

        layers_.resize(controller_->layers.size());
        for (size_t i = 0; i < layers_.size(); ++i) {
            BuildLayer(layers_[i], controller_->layers[i], i == 0);
        }
        ResolveRootMotion();

        for (LayerState& layer : layers_) {
            layer.coverage = SamplePlayhead(layer, layer.current, layer.pose);
        }
        layers_.front().coverage = 1.0f;
        ComposeLayers();
        if (rootMotionEnabled_) {
            RemoveRootMotion(pose_);
        }
        PoseEvaluator::ComputeSkeletonSpace(*skeleton_, pose_, matrices_);
    }

    void AnimatorRuntime::BuildLayer(LayerState& layer, const AnimatorLayer& source, bool base)
    {
        layer.layer = &source;
        layer.base = base;
        layer.weight = base ? 1.0f : std::clamp(source.weight, 0.0f, 1.0f);
        const std::string where = Where(layer);
        const std::string key = source.name + "/";

        layer.motions.assign(source.states.size(), {});
        for (size_t i = 0; i < source.states.size(); ++i) {
            const AnimatorState& state = source.states[i];
            layer.stateIndices.emplace(state.name, static_cast<int>(i));
            StateMotion& motion = layer.motions[i];
            motion.type = state.motion;
            if (state.motion == AnimatorMotionType::Clip) {
                if (state.clip.empty()) {
                    continue;
                }
                motion.clip = FindClipIndex(state.clip);
                if (motion.clip < 0) {
                    WarnOnce("clip:" + key + state.name,
                        where + "状態「" + state.name + "」のクリップ「" + state.clip + "」がクリップの表にありません");
                }
                continue;
            }

            motion.parameterX = ResolveBlendParameter(layer, state, state.blend.parameterX);
            if (state.motion == AnimatorMotionType::Blend2D) {
                motion.parameterY = ResolveBlendParameter(layer, state, state.blend.parameterY);
            }
            if (state.blend.children.empty()) {
                WarnOnce("children:" + key + state.name, where + "状態「" + state.name + "」のブレンドツリーに子がありません");
            }
            for (const AnimatorBlendChild& child : state.blend.children) {
                const int clip = FindClipIndex(child.clip);
                if (clip < 0) {
                    WarnOnce("child:" + key + state.name + child.clip,
                        where + "状態「" + state.name + "」のブレンドツリーの子のクリップ「" + child.clip + "」がクリップの表にありません");
                }
                motion.childClips.push_back(clip);
                motion.thresholds.push_back(child.threshold);
                motion.positions.push_back(child.position);
            }
        }

        layer.stateTransitions.assign(source.states.size(), {});
        for (size_t t = 0; t < source.transitions.size(); ++t) {
            const AnimatorTransition& transition = source.transitions[t];
            ResolvedTransition resolved;
            resolved.to = FindStateIndex(layer, transition.to);
            resolved.duration = transition.duration;
            resolved.hasExitTime = transition.hasExitTime;
            resolved.exitTime = transition.exitTime;
            resolved.offset = transition.offset;
            resolved.tableIndex = static_cast<int>(t);
            const std::string label = transition.from + " → " + transition.to;
            if (resolved.to < 0) {
                WarnOnce("to:" + key + label, where + "遷移「" + label + "」の先の状態がありません");
                continue;
            }
            for (const AnimatorCondition& condition : transition.conditions) {
                Condition entry;
                entry.op = condition.op;
                entry.value = condition.value;
                if (const auto it = parameterIndices_.find(condition.parameter); it != parameterIndices_.end()) {
                    if (AnimatorControllerFormat::IsValidOp(parameters_[it->second].type, condition.op)) {
                        entry.parameter = it->second;
                    } else {
                        WarnOnce("op:" + key + label + condition.parameter,
                            where + "遷移「" + label + "」の条件「" + condition.parameter + "」の比べ方がパラメータの種類と合いません（移りません）");
                    }
                } else {
                    WarnOnce("cond:" + key + label + condition.parameter,
                        where + "遷移「" + label + "」の条件のパラメータ「" + condition.parameter + "」がありません（移りません）");
                }
                resolved.conditions.push_back(entry);
            }
            if (transition.from == AnimatorControllerFormat::kAnyState) {
                layer.anyStateTransitions.push_back(std::move(resolved));
                continue;
            }
            const int from = FindStateIndex(layer, transition.from);
            if (from < 0) {
                WarnOnce("from:" + key + label, where + "遷移「" + label + "」の元の状態がありません");
                continue;
            }
            layer.stateTransitions[static_cast<size_t>(from)].push_back(std::move(resolved));
        }

        const size_t jointCount = skeleton_->joints.size();
        layer.pose.resize(jointCount);
        layer.sourcePose.resize(jointCount);
        layer.targetPose.resize(jointCount);
        layer.frozenPose.resize(jointCount);
        if (!base && source.useMask) {
            layer.mask.assign(jointCount, 0.0f);
            for (const std::string& bone : source.mask) {
                if (const auto it = skeleton_->jointMap.find(bone); it != skeleton_->jointMap.end()) {
                    layer.mask[static_cast<size_t>(it->second)] = 1.0f;
                }
            }
        }

        int defaultState = FindStateIndex(layer, source.defaultState);
        if (defaultState < 0 && !source.states.empty()) {
            defaultState = 0;
        }
        if (defaultState >= 0) {
            layer.current = MakePlayhead(layer, defaultState, 0.0f, std::nullopt);
        }
    }

    void AnimatorRuntime::Reset()
    {
        controller_.reset();
        skeleton_ = nullptr;
        clips_.clear();
        bindings_.clear();
        eventOrder_.clear();
        parameterIndices_.clear();
        layers_.clear();
        parameters_.clear();
        rootMotion_ = RootMotion{};
        rootJoint_ = -1;
        events_.clear();
    }

    void AnimatorRuntime::Update(float deltaTime)
    {
        events_.clear();
        rootMotion_ = RootMotion{};
        if (!IsReady()) {
            return;
        }

        for (LayerState& layer : layers_) {
            UpdateLayer(layer, deltaTime);
        }
        ComposeLayers();
        if (rootMotionEnabled_) {
            RemoveRootMotion(pose_);
        }
        PoseEvaluator::ComputeSkeletonSpace(*skeleton_, pose_, matrices_);
    }

    void AnimatorRuntime::UpdateLayer(LayerState& layer, float deltaTime)
    {
        const bool rootMotion = layer.base && rootMotionEnabled_ && rootJoint_ >= 0;
        if (layer.transition) {
            if (!layer.transition->frozenSource) {
                Advance(layer, layer.current, deltaTime);
                CollectEvents(layer, layer.current, deltaTime);
            }
            Advance(layer, layer.transition->next, deltaTime);
            CollectEvents(layer, layer.transition->next, deltaTime);
            layer.transition->elapsed += deltaTime;
            if (rootMotion) {
                AccumulateRootMotion(layer, deltaTime);
            }
        } else {
            Advance(layer, layer.current, deltaTime);
            CollectEvents(layer, layer.current, deltaTime);
            if (rootMotion) {
                AccumulateRootMotion(layer, deltaTime);
            }
            CheckTransitions(layer);
        }

        // 重ねないレイヤーは姿勢を作らず、遷移の終わりだけ進める
        if (!layer.base && layer.weight <= 0.0f) {
            if (layer.transition && layer.transition->elapsed >= layer.transition->duration) {
                layer.current = layer.transition->next;
                layer.transition.reset();
            }
            layer.coverage = 0.0f;
            return;
        }

        if (layer.transition) {
            const float targetCoverage = SamplePlayhead(layer, layer.transition->next, layer.targetPose);
            if (layer.transition->elapsed >= layer.transition->duration) {
                // 混ぜ終わるフレームは移った先の姿勢をそのまま使う
                std::swap(layer.pose, layer.targetPose);
                layer.coverage = targetCoverage;
                layer.current = layer.transition->next;
                layer.transition.reset();
            } else {
                const float weight = layer.transition->duration > 0.0f
                    ? (std::min)(1.0f, layer.transition->elapsed / layer.transition->duration)
                    : 1.0f;
                float sourceCoverage = layer.frozenCoverage;
                if (!layer.transition->frozenSource) {
                    sourceCoverage = SamplePlayhead(layer, layer.current, layer.sourcePose);
                }
                const Pose& source = layer.transition->frozenSource ? layer.frozenPose : layer.sourcePose;
                if (layer.base) {
                    PoseEvaluator::Blend(source, layer.targetPose, weight, layer.pose);
                } else {
                    MixCoverage(layer, source, sourceCoverage, targetCoverage, weight);
                }
            }
        } else {
            layer.coverage = SamplePlayhead(layer, layer.current, layer.pose);
        }
        if (layer.base) {
            layer.coverage = 1.0f;
        }
    }

    void AnimatorRuntime::MixCoverage(LayerState& layer, const Pose& source, float sourceCoverage, float targetCoverage, float weight)
    {
        const float sourceShare = sourceCoverage * (1.0f - weight);
        const float targetShare = targetCoverage * weight;
        layer.coverage = sourceShare + targetShare;
        if (targetShare <= 0.0f) {
            layer.pose = source;
        } else if (sourceShare <= 0.0f) {
            layer.pose = layer.targetPose;
        } else {
            PoseEvaluator::Blend(source, layer.targetPose, targetShare / layer.coverage, layer.pose);
        }
    }

    void AnimatorRuntime::ComposeLayers()
    {
        pose_ = layers_.front().pose;
        for (size_t k = 1; k < layers_.size(); ++k) {
            const LayerState& layer = layers_[k];
            const float weight = layer.weight * layer.coverage;
            if (weight <= 0.0f) {
                continue;
            }
            const size_t count = (std::min)(pose_.size(), layer.pose.size());
            for (size_t i = 0; i < count; ++i) {
                const float jointWeight = layer.mask.empty() ? weight : weight * layer.mask[i];
                if (jointWeight <= 0.0f) {
                    continue;
                }
                pose_[i] = jointWeight >= 1.0f ? layer.pose[i] : PoseEvaluator::BlendJoint(pose_[i], layer.pose[i], jointWeight);
            }
        }
    }

    std::string AnimatorRuntime::Where(const LayerState& layer) const
    {
        return layer.base || !layer.layer ? std::string{} : "レイヤー「" + layer.layer->name + "」の";
    }

    float AnimatorRuntime::GetLayerStateDuration(int layer, const std::string& stateName) const
    {
        const LayerState* const state = IsReady() ? LayerAt(layer) : nullptr;
        return state ? StateDuration(*state, FindStateIndex(*state, stateName)) : 0.0f;
    }

    bool AnimatorRuntime::EvaluateLayerState(int layer, const std::string& stateName, float normalizedTime)
    {
        LayerState* const target = IsReady() ? LayerAt(layer) : nullptr;
        if (!target) {
            return false;
        }
        const int state = FindStateIndex(*target, stateName);
        if (state < 0) {
            return false;
        }
        const float time = std::clamp(normalizedTime, 0.0f, 1.0f);
        if (target->base) {
            const Playhead playhead = MakePlayhead(*target, state, time, std::nullopt);
            SamplePlayhead(*target, playhead, pose_);
        } else {
            const LayerState& base = layers_.front();
            const int baseState = base.current.state;
            const Playhead basePlayhead = baseState >= 0 ? MakePlayhead(base, baseState, time, std::nullopt) : Playhead{};
            SamplePlayhead(base, basePlayhead, pose_);
            const Playhead playhead = MakePlayhead(*target, state, time, std::nullopt);
            if (SamplePlayhead(*target, playhead, blendScratch_) > 0.0f) {
                for (size_t i = 0; i < pose_.size() && i < blendScratch_.size(); ++i) {
                    if (target->mask.empty() || target->mask[i] > 0.0f) {
                        pose_[i] = blendScratch_[i];
                    }
                }
            }
        }
        if (rootMotionEnabled_) {
            RemoveRootMotion(pose_);
        }
        PoseEvaluator::ComputeSkeletonSpace(*skeleton_, pose_, matrices_);
        return true;
    }

    // ===== レイヤー =====

    const AnimatorRuntime::LayerState* AnimatorRuntime::LayerAt(int layer) const
    {
        return layer >= 0 && static_cast<size_t>(layer) < layers_.size() ? &layers_[static_cast<size_t>(layer)] : nullptr;
    }

    AnimatorRuntime::LayerState* AnimatorRuntime::LayerAt(int layer)
    {
        return layer >= 0 && static_cast<size_t>(layer) < layers_.size() ? &layers_[static_cast<size_t>(layer)] : nullptr;
    }

    int AnimatorRuntime::FindLayer(std::string_view layerName) const
    {
        for (size_t i = 0; i < layers_.size(); ++i) {
            if (layers_[i].layer && layers_[i].layer->name == layerName) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    int AnimatorRuntime::ResolveLayer(const std::string& layerName)
    {
        const int layer = FindLayer(layerName);
        if (layer < 0 && IsReady()) {
            WarnOnce("layer:" + layerName, "レイヤー「" + layerName + "」がありません");
        }
        return layer;
    }

    const std::string& AnimatorRuntime::GetLayerName(int layer) const
    {
        static const std::string kEmpty;
        const LayerState* const state = LayerAt(layer);
        return state && state->layer ? state->layer->name : kEmpty;
    }

    void AnimatorRuntime::SetLayerWeight(int layer, float weight)
    {
        if (LayerState* const state = LayerAt(layer); state && !state->base) {
            state->weight = std::clamp(weight, 0.0f, 1.0f);
        }
    }

    float AnimatorRuntime::GetLayerWeight(int layer) const
    {
        const LayerState* const state = LayerAt(layer);
        return state ? state->weight : 0.0f;
    }

    // ===== パラメータ =====

    void AnimatorRuntime::SetFloat(const std::string& name, float value)
    {
        if (const int index = FindParameter(name, AnimatorParameterType::Float); index >= 0) {
            parameters_[static_cast<size_t>(index)].floatValue = value;
        }
    }

    float AnimatorRuntime::GetFloat(const std::string& name)
    {
        const int index = FindParameter(name, AnimatorParameterType::Float);
        return index >= 0 ? parameters_[static_cast<size_t>(index)].floatValue : 0.0f;
    }

    void AnimatorRuntime::SetInt(const std::string& name, int value)
    {
        if (const int index = FindParameter(name, AnimatorParameterType::Int); index >= 0) {
            parameters_[static_cast<size_t>(index)].intValue = value;
        }
    }

    int AnimatorRuntime::GetInt(const std::string& name)
    {
        const int index = FindParameter(name, AnimatorParameterType::Int);
        return index >= 0 ? parameters_[static_cast<size_t>(index)].intValue : 0;
    }

    void AnimatorRuntime::SetBool(const std::string& name, bool value)
    {
        if (const int index = FindParameter(name, AnimatorParameterType::Bool); index >= 0) {
            parameters_[static_cast<size_t>(index)].boolValue = value;
        }
    }

    bool AnimatorRuntime::GetBool(const std::string& name)
    {
        const int index = FindParameter(name, AnimatorParameterType::Bool, true);
        return index >= 0 ? parameters_[static_cast<size_t>(index)].boolValue : false;
    }

    void AnimatorRuntime::SetTrigger(const std::string& name)
    {
        if (const int index = FindParameter(name, AnimatorParameterType::Trigger); index >= 0) {
            parameters_[static_cast<size_t>(index)].boolValue = true;
        }
    }

    void AnimatorRuntime::ResetTrigger(const std::string& name)
    {
        if (const int index = FindParameter(name, AnimatorParameterType::Trigger); index >= 0) {
            parameters_[static_cast<size_t>(index)].boolValue = false;
        }
    }

    // ===== 状態 =====

    bool AnimatorRuntime::PlayInLayer(int layer, const std::string& stateName, std::optional<bool> loop)
    {
        LayerState* const target = IsReady() ? LayerAt(layer) : nullptr;
        if (!target) {
            return false;
        }
        const int state = FindStateIndex(*target, stateName);
        if (state < 0) {
            WarnOnce("state:" + target->layer->name + "/" + stateName, Where(*target) + "状態「" + stateName + "」がありません");
            return false;
        }
        target->current = MakePlayhead(*target, state, 0.0f, loop);
        target->transition.reset();
        return true;
    }

    bool AnimatorRuntime::CrossFadeInLayer(int layer, const std::string& stateName, float seconds, std::optional<bool> loop)
    {
        if (seconds <= 0.0f) {
            return PlayInLayer(layer, stateName, loop);
        }
        LayerState* const target = IsReady() ? LayerAt(layer) : nullptr;
        if (!target) {
            return false;
        }
        const int state = FindStateIndex(*target, stateName);
        if (state < 0) {
            WarnOnce("state:" + target->layer->name + "/" + stateName, Where(*target) + "状態「" + stateName + "」がありません");
            return false;
        }
        Transition next;
        next.next = MakePlayhead(*target, state, 0.0f, loop);
        next.duration = seconds;
        if (target->transition) {
            target->frozenPose = target->pose;
            target->frozenCoverage = target->coverage;
            next.frozenSource = true;
        }
        target->transition = next;
        return true;
    }

    const std::string& AnimatorRuntime::GetLayerStateName(int layer) const
    {
        static const std::string kEmpty;
        const int index = GetLayerStateIndex(layer);
        return index >= 0 ? layers_[static_cast<size_t>(layer)].layer->states[static_cast<size_t>(index)].name : kEmpty;
    }

    int AnimatorRuntime::GetLayerStateIndex(int layer) const
    {
        const LayerState* const state = IsReady() ? LayerAt(layer) : nullptr;
        if (!state) {
            return -1;
        }
        return state->transition ? state->transition->next.state : state->current.state;
    }

    bool AnimatorRuntime::IsInLayerState(int layer, const std::string& stateName) const
    {
        const int index = GetLayerStateIndex(layer);
        return index >= 0 && layers_[static_cast<size_t>(layer)].layer->states[static_cast<size_t>(index)].name == stateName;
    }

    float AnimatorRuntime::GetLayerNormalizedTime(int layer) const
    {
        const LayerState* const state = IsReady() ? LayerAt(layer) : nullptr;
        if (!state) {
            return 0.0f;
        }
        return NormalizedOf(*state, state->transition ? state->transition->next : state->current);
    }

    bool AnimatorRuntime::IsLayerInTransition(int layer) const
    {
        const LayerState* const state = LayerAt(layer);
        return state && state->transition.has_value();
    }

    float AnimatorRuntime::GetLayerTransitionProgress(int layer) const
    {
        const LayerState* const state = LayerAt(layer);
        if (!state || !state->transition) {
            return 0.0f;
        }
        const Transition& transition = *state->transition;
        return transition.duration > 0.0f ? (std::min)(1.0f, transition.elapsed / transition.duration) : 1.0f;
    }

    int AnimatorRuntime::GetLayerTransitionSourceIndex(int layer) const
    {
        const LayerState* const state = LayerAt(layer);
        return state && state->transition && !state->transition->frozenSource ? state->current.state : -1;
    }

    int AnimatorRuntime::GetLayerActiveTransitionIndex(int layer) const
    {
        const LayerState* const state = LayerAt(layer);
        return state && state->transition ? state->transition->tableIndex : -1;
    }

    // ===== 内部 =====

    bool AnimatorRuntime::IsBlend(const LayerState& layer, int state) const
    {
        return state >= 0 && static_cast<size_t>(state) < layer.motions.size() &&
            layer.motions[static_cast<size_t>(state)].type != AnimatorMotionType::Clip;
    }

    float AnimatorRuntime::ClipDuration(int clip) const
    {
        return clip >= 0 && clips_[static_cast<size_t>(clip)] ? clips_[static_cast<size_t>(clip)]->duration : 0.0f;
    }

    float AnimatorRuntime::ParameterFloat(int parameter) const
    {
        return parameter >= 0 ? parameters_[static_cast<size_t>(parameter)].floatValue : 0.0f;
    }

    void AnimatorRuntime::ComputeWeights(const StateMotion& motion, std::vector<float>& weights) const
    {
        const size_t count = motion.childClips.size();
        weights.assign(count, 0.0f);
        if (count == 0) {
            return;
        }
        if (motion.type == AnimatorMotionType::Blend1D) {
            BlendTree::Compute1D(motion.thresholds, ParameterFloat(motion.parameterX), weights);
        } else {
            BlendTree::Compute2D(motion.positions,
                Vector2{ ParameterFloat(motion.parameterX), ParameterFloat(motion.parameterY) }, weights);
        }

        float total = 0.0f;
        for (size_t i = 0; i < count; ++i) {
            const int clip = motion.childClips[i];
            if (clip < 0 || !clips_[static_cast<size_t>(clip)]) {
                weights[i] = 0.0f;
            }
            total += weights[i];
        }
        if (total > 0.0f && total != 1.0f) {
            for (float& weight : weights) {
                weight /= total;
            }
        }
    }

    float AnimatorRuntime::StateDuration(const LayerState& layer, int state) const
    {
        if (state < 0 || static_cast<size_t>(state) >= layer.motions.size()) {
            return 0.0f;
        }
        const StateMotion& motion = layer.motions[static_cast<size_t>(state)];
        if (motion.type == AnimatorMotionType::Clip) {
            return ClipDuration(motion.clip);
        }
        ComputeWeights(motion, weights_);
        float duration = 0.0f;
        for (size_t i = 0; i < weights_.size(); ++i) {
            if (weights_[i] > 0.0f) {
                duration += weights_[i] * ClipDuration(motion.childClips[i]);
            }
        }
        return duration;
    }

    float AnimatorRuntime::NormalizedOf(const LayerState& layer, const Playhead& playhead) const
    {
        if (IsBlend(layer, playhead.state)) {
            return playhead.time;
        }
        const float duration = StateDuration(layer, playhead.state);
        return duration > 0.0f ? playhead.time / duration : 0.0f;
    }

    AnimatorRuntime::Playhead AnimatorRuntime::MakePlayhead(const LayerState& layer, int state, float normalizedOffset,
        std::optional<bool> loop) const
    {
        Playhead playhead;
        playhead.state = state;
        playhead.loop = loop.value_or(layer.layer->states[static_cast<size_t>(state)].loop);
        playhead.time = IsBlend(layer, state) ? normalizedOffset : normalizedOffset * StateDuration(layer, state);
        playhead.previous = playhead.time;
        return playhead;
    }

    void AnimatorRuntime::Advance(const LayerState& layer, Playhead& playhead, float deltaTime) const
    {
        playhead.wrapped = false;
        playhead.previous = playhead.time;
        if (playhead.state < 0) {
            return;
        }
        const float duration = StateDuration(layer, playhead.state);
        const float speed = layer.layer->states[static_cast<size_t>(playhead.state)].speed;
        if (duration <= 0.0f) {
            playhead.time = 0.0f;
            playhead.previous = 0.0f;
            return;
        }
        const bool blend = IsBlend(layer, playhead.state);
        // ブレンドツリーは木の尺で割って進みの割合のまま進める
        const float length = blend ? 1.0f : duration;
        playhead.time += blend ? deltaTime * speed / duration : deltaTime * speed;
        if (playhead.loop) {
            if (playhead.time >= length || playhead.time < 0.0f) {
                playhead.wrapped = true;
            }
            playhead.time = std::fmod(playhead.time, length);
            if (playhead.time < 0.0f) {
                playhead.time += length;
            }
        } else {
            playhead.time = std::clamp(playhead.time, 0.0f, length);
        }
    }

    bool AnimatorRuntime::ExitReached(const LayerState& layer, const Playhead& playhead, float exitTime) const
    {
        const float duration = StateDuration(layer, playhead.state);
        if (duration <= 0.0f) {
            return true;
        }
        const float normalized = NormalizedOf(layer, playhead);
        if (playhead.loop) {
            return exitTime < 1.0f ? (normalized >= exitTime || playhead.wrapped) : playhead.wrapped;
        }
        return normalized >= (std::min)(exitTime, 1.0f);
    }

    void AnimatorRuntime::CollectEvents(const LayerState& layer, Playhead& playhead, float deltaTime)
    {
        const bool includeFrom = playhead.fresh;
        playhead.fresh = false;
        if (!layer.base && layer.weight <= 0.0f) {
            return;
        }
        if (playhead.state < 0 || deltaTime < 0.0f || layer.layer->states[static_cast<size_t>(playhead.state)].speed < 0.0f) {
            return;
        }
        const int clip = EventClip(layer, playhead);
        if (clip < 0 || eventOrder_[static_cast<size_t>(clip)].empty()) {
            return;
        }
        const float clipDuration = ClipDuration(clip);
        // ブレンドツリーの進みの割合をそのクリップの秒へ直す
        const float scale = IsBlend(layer, playhead.state) ? clipDuration : 1.0f;
        const float from = playhead.previous * scale;
        const float to = playhead.time * scale;
        if (playhead.wrapped) {
            EmitEvents(layer, clip, playhead.state, from, clipDuration, includeFrom);
            EmitEvents(layer, clip, playhead.state, 0.0f, to, true);
        } else {
            EmitEvents(layer, clip, playhead.state, from, to, includeFrom);
        }
    }

    int AnimatorRuntime::EventClip(const LayerState& layer, const Playhead& playhead) const
    {
        if (playhead.state < 0 || static_cast<size_t>(playhead.state) >= layer.motions.size()) {
            return -1;
        }
        const StateMotion& motion = layer.motions[static_cast<size_t>(playhead.state)];
        if (motion.type == AnimatorMotionType::Clip) {
            return motion.clip >= 0 && clips_[static_cast<size_t>(motion.clip)] ? motion.clip : -1;
        }
        ComputeWeights(motion, weights_);
        int heaviest = -1;
        float heaviestWeight = 0.0f;
        for (size_t i = 0; i < weights_.size(); ++i) {
            if (weights_[i] > heaviestWeight) {
                heaviestWeight = weights_[i];
                heaviest = static_cast<int>(i);
            }
        }
        return heaviest >= 0 ? motion.childClips[static_cast<size_t>(heaviest)] : -1;
    }

    void AnimatorRuntime::EmitEvents(const LayerState& layer, int clip, int state, float from, float to, bool includeFrom)
    {
        if (to < from) {
            return;
        }
        const AnimatorClip& source = controller_->clips[static_cast<size_t>(clip)];
        for (const int index : eventOrder_[static_cast<size_t>(clip)]) {
            const AnimatorClipEvent& event = source.events[static_cast<size_t>(index)];
            const bool afterFrom = includeFrom ? event.time >= from : event.time > from;
            if (!afterFrom) {
                continue;
            }
            if (event.time > to) {
                break;
            }
            AnimationEvent& fired = events_.emplace_back();
            fired.name = event.name;
            fired.clip = source.id;
            fired.state = layer.layer->states[static_cast<size_t>(state)].name;
            fired.layer = layer.layer->name;
            fired.floatValue = event.floatValue;
            fired.intValue = event.intValue;
            fired.stringValue = event.stringValue;
        }
    }

    int AnimatorRuntime::FindClipIndex(const std::string& clipId) const
    {
        for (size_t i = 0; i < controller_->clips.size(); ++i) {
            if (controller_->clips[i].id == clipId) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    int AnimatorRuntime::ResolveBlendParameter(const LayerState& layer, const AnimatorState& state, const std::string& name)
    {
        const std::string where = Where(layer);
        const std::string key = layer.layer->name + "/" + state.name;
        if (name.empty()) {
            WarnOnce("blendparam:" + key, where + "状態「" + state.name + "」のブレンドツリーのパラメータが選ばれていません（0 として混ぜます）");
            return -1;
        }
        const auto it = parameterIndices_.find(name);
        if (it == parameterIndices_.end()) {
            WarnOnce("blendparam:" + key + name,
                where + "状態「" + state.name + "」のブレンドツリーのパラメータ「" + name + "」がありません（0 として混ぜます）");
            return -1;
        }
        if (parameters_[static_cast<size_t>(it->second)].type != AnimatorParameterType::Float) {
            WarnOnce("blendparam:" + key + name,
                where + "状態「" + state.name + "」のブレンドツリーのパラメータ「" + name + "」が Float ではありません（0 として混ぜます）");
            return -1;
        }
        return it->second;
    }

    bool AnimatorRuntime::ConditionsMet(const ResolvedTransition& transition) const
    {
        for (const Condition& condition : transition.conditions) {
            if (condition.parameter < 0) {
                return false;
            }
            const ParameterValue& value = parameters_[static_cast<size_t>(condition.parameter)];
            const bool isInt = value.type == AnimatorParameterType::Int;
            const int target = static_cast<int>(std::lround(condition.value));
            bool met = false;
            switch (condition.op) {
            case AnimatorConditionOp::Greater: met = isInt ? value.intValue > target : value.floatValue > condition.value; break;
            case AnimatorConditionOp::Less: met = isInt ? value.intValue < target : value.floatValue < condition.value; break;
            case AnimatorConditionOp::Equal: met = value.intValue == target; break;
            case AnimatorConditionOp::NotEqual: met = value.intValue != target; break;
            case AnimatorConditionOp::True: met = value.boolValue; break;
            case AnimatorConditionOp::False: met = !value.boolValue; break;
            case AnimatorConditionOp::Trigger: met = value.boolValue; break;
            }
            if (!met) {
                return false;
            }
        }
        return true;
    }

    void AnimatorRuntime::CheckTransitions(LayerState& layer)
    {
        const int state = layer.current.state;
        for (const ResolvedTransition& transition : layer.anyStateTransitions) {
            if (transition.to == state) {
                continue;
            }
            if (transition.hasExitTime && !ExitReached(layer, layer.current, transition.exitTime)) {
                continue;
            }
            if (ConditionsMet(transition)) {
                Fire(layer, transition);
                return;
            }
        }
        if (state < 0) {
            return;
        }
        for (const ResolvedTransition& transition : layer.stateTransitions[static_cast<size_t>(state)]) {
            if (transition.hasExitTime && !ExitReached(layer, layer.current, transition.exitTime)) {
                continue;
            }
            if (ConditionsMet(transition)) {
                Fire(layer, transition);
                return;
            }
        }
    }

    void AnimatorRuntime::Fire(LayerState& layer, const ResolvedTransition& transition)
    {
        for (const Condition& condition : transition.conditions) {
            if (condition.parameter >= 0 && condition.op == AnimatorConditionOp::Trigger) {
                parameters_[static_cast<size_t>(condition.parameter)].boolValue = false;
            }
        }
        const Playhead next = MakePlayhead(layer, transition.to, transition.offset, std::nullopt);
        if (transition.duration <= 0.0f) {
            layer.current = next;
            layer.transition.reset();
            return;
        }
        Transition started;
        started.next = next;
        started.duration = transition.duration;
        started.tableIndex = transition.tableIndex;
        layer.transition = started;
    }

    float AnimatorRuntime::SamplePlayhead(const LayerState& layer, const Playhead& playhead, Pose& out) const
    {
        if (playhead.state < 0 || static_cast<size_t>(playhead.state) >= layer.motions.size()) {
            PoseEvaluator::WriteBindPose(*skeleton_, out);
            return 0.0f;
        }
        const StateMotion& motion = layer.motions[static_cast<size_t>(playhead.state)];
        if (motion.type == AnimatorMotionType::Clip) {
            if (motion.clip < 0 || !clips_[static_cast<size_t>(motion.clip)]) {
                PoseEvaluator::WriteBindPose(*skeleton_, out);
                return 0.0f;
            }
            PoseEvaluator::Sample(*skeleton_, bindings_[static_cast<size_t>(motion.clip)], playhead.time, out);
            return 1.0f;
        }

        // 重みのある子を順に混ぜ、混ぜた分の重みの合計で次の子の割合を決める
        ComputeWeights(motion, weights_);
        float accumulated = 0.0f;
        for (size_t i = 0; i < weights_.size(); ++i) {
            const float weight = weights_[i];
            if (weight <= 0.0f) {
                continue;
            }
            const int clip = motion.childClips[i];
            const float clipTime = playhead.time * ClipDuration(clip);
            if (accumulated <= 0.0f) {
                PoseEvaluator::Sample(*skeleton_, bindings_[static_cast<size_t>(clip)], clipTime, out);
                accumulated = weight;
                continue;
            }
            PoseEvaluator::Sample(*skeleton_, bindings_[static_cast<size_t>(clip)], clipTime, blendScratch_);
            accumulated += weight;
            PoseEvaluator::Blend(out, blendScratch_, weight / accumulated, out);
        }
        if (accumulated <= 0.0f) {
            PoseEvaluator::WriteBindPose(*skeleton_, out);
            return 0.0f;
        }
        return 1.0f;
    }

    int AnimatorRuntime::FindStateIndex(const LayerState& layer, const std::string& stateName) const
    {
        const auto it = layer.stateIndices.find(stateName);
        return it != layer.stateIndices.end() ? it->second : -1;
    }

    // ===== クリップの移動 =====

    void AnimatorRuntime::ResolveRootMotion()
    {
        rootJoint_ = -1;
        const std::vector<Joint>& joints = skeleton_->joints;
        if (const std::string& bone = controller_->rootMotionBone; !bone.empty()) {
            if (const auto it = skeleton_->jointMap.find(bone); it != skeleton_->jointMap.end()) {
                rootJoint_ = it->second;
            } else {
                WarnOnce("rootbone:" + bone,
                    "クリップの移動を取る骨「" + bone + "」がこのモデルにありません（木のいちばん上でトラックを持つ骨を使います）");
            }
        }
        if (rootJoint_ < 0) {
            int bestDepth = INT_MAX;
            for (size_t i = 0; i < joints.size(); ++i) {
                const bool tracked = std::any_of(bindings_.begin(), bindings_.end(), [i](const ClipBinding& binding) {
                    return i < binding.tracks.size() && binding.tracks[i];
                });
                if (!tracked) {
                    continue;
                }
                int depth = 0;
                for (std::optional<int32_t> parent = joints[i].parent; parent; parent = joints[static_cast<size_t>(*parent)].parent) {
                    ++depth;
                }
                if (depth < bestDepth) {
                    bestDepth = depth;
                    rootJoint_ = static_cast<int>(i);
                }
            }
        }
        if (rootJoint_ < 0) {
            return;
        }

        Pose bind;
        PoseEvaluator::WriteBindPose(*skeleton_, bind);
        std::vector<Matrix4x4> model;
        PoseEvaluator::ComputeSkeletonSpace(*skeleton_, bind, model);
        const std::optional<int32_t>& parent = joints[static_cast<size_t>(rootJoint_)].parent;
        rootParentMatrix_ = parent ? model[static_cast<size_t>(*parent)] : MathCore::Matrix::Identity();
        rootParentInverse_ = MathCore::Matrix::Inverse(rootParentMatrix_);

        std::vector<int32_t> chain;
        for (std::optional<int32_t> ancestor = parent; ancestor; ancestor = joints[static_cast<size_t>(*ancestor)].parent) {
            chain.push_back(*ancestor);
        }
        Quaternion rotation = MathCore::QuaternionMath::Identity();
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            rotation = rotation * bind[static_cast<size_t>(*it)].rotate;
        }
        rootParentRotation_ = MathCore::QuaternionMath::Normalize(rotation);

        const JointPose& root = bind[static_cast<size_t>(rootJoint_)];
        bindRootPosition_ = MathCore::CoordinateTransform::TransformCoord(root.translate, rootParentMatrix_);
        bindRootYaw_ = TwistYaw(rootParentRotation_ * root.rotate);
    }

    void AnimatorRuntime::AccumulateRootMotion(const LayerState& layer, float deltaTime)
    {
        MotionSegment total;
        if (layer.transition) {
            const Transition& transition = *layer.transition;
            const float weight = transition.duration > 0.0f ? (std::min)(1.0f, transition.elapsed / transition.duration) : 1.0f;
            const MotionSegment target = PlayheadMotion(layer, transition.next, deltaTime);
            total.position = target.position * weight;
            total.yaw = target.yaw * weight;
            if (!transition.frozenSource) {
                const MotionSegment source = PlayheadMotion(layer, layer.current, deltaTime);
                total.position = total.position + source.position * (1.0f - weight);
                total.yaw += source.yaw * (1.0f - weight);
            }
        } else {
            total = PlayheadMotion(layer, layer.current, deltaTime);
        }
        rootMotion_.position = rootMotion_.position + RotateYaw(total.position, bindRootYaw_);
        rootMotion_.yaw += total.yaw;
    }

    AnimatorRuntime::MotionSegment AnimatorRuntime::PlayheadMotion(const LayerState& layer, const Playhead& playhead,
        float deltaTime) const
    {
        if (playhead.state < 0 || static_cast<size_t>(playhead.state) >= layer.motions.size()) {
            return {};
        }
        const bool forward = deltaTime * layer.layer->states[static_cast<size_t>(playhead.state)].speed >= 0.0f;
        const StateMotion& motion = layer.motions[static_cast<size_t>(playhead.state)];
        if (motion.type == AnimatorMotionType::Clip) {
            if (motion.clip < 0 || !clips_[static_cast<size_t>(motion.clip)]) {
                return {};
            }
            return ClipMotion(motion.clip, playhead.previous, playhead.time, playhead.wrapped, forward);
        }

        ComputeWeights(motion, weights_);
        MotionSegment total;
        for (size_t i = 0; i < weights_.size(); ++i) {
            const float weight = weights_[i];
            if (weight <= 0.0f) {
                continue;
            }
            const int clip = motion.childClips[i];
            const float duration = ClipDuration(clip);
            const MotionSegment segment =
                ClipMotion(clip, playhead.previous * duration, playhead.time * duration, playhead.wrapped, forward);
            total.position = total.position + segment.position * weight;
            total.yaw += segment.yaw * weight;
        }
        return total;
    }

    AnimatorRuntime::MotionSegment AnimatorRuntime::ClipMotion(int clip, float from, float to, bool wrapped, bool forward) const
    {
        if (!wrapped) {
            return ClipSegment(clip, from, to);
        }
        const float duration = ClipDuration(clip);
        const MotionSegment first = forward ? ClipSegment(clip, from, duration) : ClipSegment(clip, from, 0.0f);
        const MotionSegment second = forward ? ClipSegment(clip, 0.0f, to) : ClipSegment(clip, duration, to);
        MotionSegment joined;
        joined.position = first.position + RotateYaw(second.position, first.yaw);
        joined.yaw = first.yaw + second.yaw;
        return joined;
    }

    AnimatorRuntime::MotionSegment AnimatorRuntime::ClipSegment(int clip, float from, float to) const
    {
        Vector3 start;
        Vector3 end;
        float startYaw = 0.0f;
        float endYaw = 0.0f;
        SampleRoot(clip, from, start, startYaw);
        SampleRoot(clip, to, end, endYaw);
        const Vector3 delta{ end.x - start.x, 0.0f, end.z - start.z };
        MotionSegment segment;
        segment.position = RotateYaw(delta, -startYaw);
        segment.yaw = MathCore::NormalizeAngle(endYaw - startYaw);
        return segment;
    }

    void AnimatorRuntime::SampleRoot(int clip, float time, Vector3& position, float& yaw) const
    {
        const size_t joint = static_cast<size_t>(rootJoint_);
        const JointPose& bind = skeleton_->joints[joint].transform;
        const std::vector<const NodeAnimation*>& tracks = bindings_[static_cast<size_t>(clip)].tracks;
        const NodeAnimation* const track = joint < tracks.size() ? tracks[joint] : nullptr;
        Vector3 translate = bind.translate;
        Quaternion rotate = bind.rotate;
        if (track) {
            if (!track->translate.keyframes.empty()) {
                translate = AnimationUtils::CalculateVector3(track->translate.keyframes, time);
            }
            if (!track->rotate.keyframes.empty()) {
                rotate = AnimationUtils::CalculateQuaternion(track->rotate.keyframes, time);
            }
        }
        position = MathCore::CoordinateTransform::TransformCoord(translate, rootParentMatrix_);
        yaw = TwistYaw(rootParentRotation_ * rotate);
    }

    void AnimatorRuntime::RemoveRootMotion(Pose& pose) const
    {
        if (rootJoint_ < 0 || static_cast<size_t>(rootJoint_) >= pose.size()) {
            return;
        }
        JointPose& joint = pose[static_cast<size_t>(rootJoint_)];
        Vector3 position = MathCore::CoordinateTransform::TransformCoord(joint.translate, rootParentMatrix_);
        position.x = bindRootPosition_.x;
        position.z = bindRootPosition_.z;
        joint.translate = MathCore::CoordinateTransform::TransformCoord(position, rootParentInverse_);

        const Quaternion model = rootParentRotation_ * joint.rotate;
        const Quaternion facing = YawRotation(bindRootYaw_ - TwistYaw(model)) * model;
        joint.rotate = MathCore::QuaternionMath::Normalize(MathCore::QuaternionMath::Inverse(rootParentRotation_) * facing);
    }

    int AnimatorRuntime::FindParameter(const std::string& name, AnimatorParameterType type, bool triggerOrBool)
    {
        const auto it = parameterIndices_.find(name);
        if (it == parameterIndices_.end()) {
            WarnOnce("param:" + name, "パラメータ「" + name + "」がありません");
            return -1;
        }
        const AnimatorParameterType actual = parameters_[static_cast<size_t>(it->second)].type;
        const bool matches = actual == type ||
            (triggerOrBool && actual == AnimatorParameterType::Trigger && type == AnimatorParameterType::Bool);
        if (!matches) {
            WarnOnce("type:" + name, std::string("パラメータ「") + name + "」は " + AnimatorControllerFormat::ToString(actual) +
                " です（" + AnimatorControllerFormat::ToString(type) + " では読み書きできません）");
            return -1;
        }
        return it->second;
    }

    void AnimatorRuntime::WarnOnce(const std::string& key, const std::string& message)
    {
        if (warned_.insert(key).second) {
            Logger::GetInstance().Warnf(LogCategory::Graphics, "Animator（{}）: {}", ownerName_, message);
        }
    }
}
