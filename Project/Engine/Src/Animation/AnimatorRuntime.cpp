#include "pch.h"
#include "AnimatorRuntime.h"

#include "Animation/BlendTree.h"
#include "Animation/PoseEvaluator.h"
#include "Graphics/Model/Animation/Animation.h"
#include "Graphics/Model/Skeleton/Skeleton.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace CoreEngine
{
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
        layer_ = &controller_->layers.front();
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

        motions_.assign(layer_->states.size(), {});
        for (size_t i = 0; i < layer_->states.size(); ++i) {
            const AnimatorState& state = layer_->states[i];
            stateIndices_.emplace(state.name, static_cast<int>(i));
            StateMotion& motion = motions_[i];
            motion.type = state.motion;
            if (state.motion == AnimatorMotionType::Clip) {
                if (state.clip.empty()) {
                    continue;
                }
                motion.clip = FindClipIndex(state.clip);
                if (motion.clip < 0) {
                    WarnOnce("clip:" + state.name, "状態「" + state.name + "」のクリップ「" + state.clip + "」がクリップの表にありません");
                }
                continue;
            }

            motion.parameterX = ResolveBlendParameter(state, state.blend.parameterX);
            if (state.motion == AnimatorMotionType::Blend2D) {
                motion.parameterY = ResolveBlendParameter(state, state.blend.parameterY);
            }
            if (state.blend.children.empty()) {
                WarnOnce("children:" + state.name, "状態「" + state.name + "」のブレンドツリーに子がありません");
            }
            for (const AnimatorBlendChild& child : state.blend.children) {
                const int clip = FindClipIndex(child.clip);
                if (clip < 0) {
                    WarnOnce("child:" + state.name + child.clip,
                        "状態「" + state.name + "」のブレンドツリーの子のクリップ「" + child.clip + "」がクリップの表にありません");
                }
                motion.childClips.push_back(clip);
                motion.thresholds.push_back(child.threshold);
                motion.positions.push_back(child.position);
            }
        }

        stateTransitions_.assign(layer_->states.size(), {});
        for (size_t t = 0; t < layer_->transitions.size(); ++t) {
            const AnimatorTransition& source = layer_->transitions[t];
            ResolvedTransition resolved;
            resolved.to = FindStateIndex(source.to);
            resolved.duration = source.duration;
            resolved.hasExitTime = source.hasExitTime;
            resolved.exitTime = source.exitTime;
            resolved.offset = source.offset;
            resolved.tableIndex = static_cast<int>(t);
            const std::string label = source.from + " → " + source.to;
            if (resolved.to < 0) {
                WarnOnce("to:" + label, "遷移「" + label + "」の先の状態がありません");
                continue;
            }
            for (const AnimatorCondition& condition : source.conditions) {
                Condition entry;
                entry.op = condition.op;
                entry.value = condition.value;
                if (const auto it = parameterIndices_.find(condition.parameter); it != parameterIndices_.end()) {
                    if (AnimatorControllerFormat::IsValidOp(parameters_[it->second].type, condition.op)) {
                        entry.parameter = it->second;
                    } else {
                        WarnOnce("op:" + label + condition.parameter,
                            "遷移「" + label + "」の条件「" + condition.parameter + "」の比べ方がパラメータの種類と合いません（移りません）");
                    }
                } else {
                    WarnOnce("cond:" + label + condition.parameter,
                        "遷移「" + label + "」の条件のパラメータ「" + condition.parameter + "」がありません（移りません）");
                }
                resolved.conditions.push_back(entry);
            }
            if (source.from == AnimatorControllerFormat::kAnyState) {
                anyStateTransitions_.push_back(std::move(resolved));
                continue;
            }
            const int from = FindStateIndex(source.from);
            if (from < 0) {
                WarnOnce("from:" + label, "遷移「" + label + "」の元の状態がありません");
                continue;
            }
            stateTransitions_[static_cast<size_t>(from)].push_back(std::move(resolved));
        }

        const size_t jointCount = skeleton_->joints.size();
        pose_.resize(jointCount);
        sourcePose_.resize(jointCount);
        targetPose_.resize(jointCount);
        frozenPose_.resize(jointCount);
        blendScratch_.resize(jointCount);
        matrices_.resize(jointCount);

        int defaultState = FindStateIndex(layer_->defaultState);
        if (defaultState < 0 && !layer_->states.empty()) {
            defaultState = 0;
        }
        if (defaultState >= 0) {
            current_ = MakePlayhead(defaultState, 0.0f, std::nullopt);
        }
        SamplePlayhead(current_, pose_);
        PoseEvaluator::ComputeSkeletonSpace(*skeleton_, pose_, matrices_);
    }

    void AnimatorRuntime::Reset()
    {
        controller_.reset();
        layer_ = nullptr;
        skeleton_ = nullptr;
        clips_.clear();
        bindings_.clear();
        eventOrder_.clear();
        motions_.clear();
        stateIndices_.clear();
        parameterIndices_.clear();
        anyStateTransitions_.clear();
        stateTransitions_.clear();
        parameters_.clear();
        current_ = Playhead{};
        transition_.reset();
        events_.clear();
    }

    void AnimatorRuntime::Update(float deltaTime)
    {
        events_.clear();
        if (!IsReady()) {
            return;
        }

        if (transition_) {
            if (!transition_->frozenSource) {
                Advance(current_, deltaTime);
                CollectEvents(current_, deltaTime);
            }
            Advance(transition_->next, deltaTime);
            CollectEvents(transition_->next, deltaTime);
            transition_->elapsed += deltaTime;
        } else {
            Advance(current_, deltaTime);
            CollectEvents(current_, deltaTime);
            CheckTransitions();
        }

        if (transition_) {
            SamplePlayhead(transition_->next, targetPose_);
            if (transition_->elapsed >= transition_->duration) {
                // 混ぜ終わるフレームは移った先の姿勢をそのまま使う
                std::swap(pose_, targetPose_);
                current_ = transition_->next;
                transition_.reset();
            } else {
                const float weight = transition_->duration > 0.0f
                    ? (std::min)(1.0f, transition_->elapsed / transition_->duration)
                    : 1.0f;
                if (!transition_->frozenSource) {
                    SamplePlayhead(current_, sourcePose_);
                }
                PoseEvaluator::Blend(transition_->frozenSource ? frozenPose_ : sourcePose_, targetPose_, weight, pose_);
            }
        } else {
            SamplePlayhead(current_, pose_);
        }

        PoseEvaluator::ComputeSkeletonSpace(*skeleton_, pose_, matrices_);
    }

    float AnimatorRuntime::GetStateDuration(const std::string& stateName) const
    {
        return IsReady() ? StateDuration(FindStateIndex(stateName)) : 0.0f;
    }

    bool AnimatorRuntime::EvaluateState(const std::string& stateName, float normalizedTime)
    {
        if (!IsReady()) {
            return false;
        }
        const int state = FindStateIndex(stateName);
        if (state < 0) {
            return false;
        }
        const Playhead playhead = MakePlayhead(state, std::clamp(normalizedTime, 0.0f, 1.0f), std::nullopt);
        SamplePlayhead(playhead, pose_);
        PoseEvaluator::ComputeSkeletonSpace(*skeleton_, pose_, matrices_);
        return true;
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

    bool AnimatorRuntime::Play(const std::string& stateName, std::optional<bool> loop)
    {
        if (!IsReady()) {
            return false;
        }
        const int state = FindStateIndex(stateName);
        if (state < 0) {
            WarnOnce("state:" + stateName, "状態「" + stateName + "」がありません");
            return false;
        }
        current_ = MakePlayhead(state, 0.0f, loop);
        transition_.reset();
        return true;
    }

    bool AnimatorRuntime::CrossFade(const std::string& stateName, float seconds, std::optional<bool> loop)
    {
        if (seconds <= 0.0f) {
            return Play(stateName, loop);
        }
        if (!IsReady()) {
            return false;
        }
        const int state = FindStateIndex(stateName);
        if (state < 0) {
            WarnOnce("state:" + stateName, "状態「" + stateName + "」がありません");
            return false;
        }
        Transition next;
        next.next = MakePlayhead(state, 0.0f, loop);
        next.duration = seconds;
        if (transition_) {
            frozenPose_ = pose_;
            next.frozenSource = true;
        }
        transition_ = next;
        return true;
    }

    const std::string& AnimatorRuntime::GetCurrentStateName() const
    {
        static const std::string kEmpty;
        const int index = GetCurrentStateIndex();
        return index >= 0 ? layer_->states[static_cast<size_t>(index)].name : kEmpty;
    }

    int AnimatorRuntime::GetCurrentStateIndex() const
    {
        if (!IsReady()) {
            return -1;
        }
        return transition_ ? transition_->next.state : current_.state;
    }

    bool AnimatorRuntime::IsInState(const std::string& stateName) const
    {
        const int index = GetCurrentStateIndex();
        return index >= 0 && layer_->states[static_cast<size_t>(index)].name == stateName;
    }

    float AnimatorRuntime::GetNormalizedTime() const
    {
        if (!IsReady()) {
            return 0.0f;
        }
        return NormalizedOf(transition_ ? transition_->next : current_);
    }

    float AnimatorRuntime::GetTransitionProgress() const
    {
        if (!transition_) {
            return 0.0f;
        }
        return transition_->duration > 0.0f ? (std::min)(1.0f, transition_->elapsed / transition_->duration) : 1.0f;
    }

    int AnimatorRuntime::GetTransitionSourceIndex() const
    {
        return transition_ && !transition_->frozenSource ? current_.state : -1;
    }

    int AnimatorRuntime::GetActiveTransitionIndex() const
    {
        return transition_ ? transition_->tableIndex : -1;
    }

    // ===== 内部 =====

    bool AnimatorRuntime::IsBlend(int state) const
    {
        return state >= 0 && static_cast<size_t>(state) < motions_.size() &&
            motions_[static_cast<size_t>(state)].type != AnimatorMotionType::Clip;
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

    float AnimatorRuntime::StateDuration(int state) const
    {
        if (state < 0 || static_cast<size_t>(state) >= motions_.size()) {
            return 0.0f;
        }
        const StateMotion& motion = motions_[static_cast<size_t>(state)];
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

    float AnimatorRuntime::NormalizedOf(const Playhead& playhead) const
    {
        if (IsBlend(playhead.state)) {
            return playhead.time;
        }
        const float duration = StateDuration(playhead.state);
        return duration > 0.0f ? playhead.time / duration : 0.0f;
    }

    AnimatorRuntime::Playhead AnimatorRuntime::MakePlayhead(int state, float normalizedOffset, std::optional<bool> loop) const
    {
        Playhead playhead;
        playhead.state = state;
        playhead.loop = loop.value_or(layer_->states[static_cast<size_t>(state)].loop);
        playhead.time = IsBlend(state) ? normalizedOffset : normalizedOffset * StateDuration(state);
        playhead.previous = playhead.time;
        return playhead;
    }

    void AnimatorRuntime::Advance(Playhead& playhead, float deltaTime) const
    {
        playhead.wrapped = false;
        playhead.previous = playhead.time;
        if (playhead.state < 0) {
            return;
        }
        const float duration = StateDuration(playhead.state);
        const float speed = layer_->states[static_cast<size_t>(playhead.state)].speed;
        if (duration <= 0.0f) {
            playhead.time = 0.0f;
            playhead.previous = 0.0f;
            return;
        }
        const bool blend = IsBlend(playhead.state);
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

    bool AnimatorRuntime::ExitReached(const Playhead& playhead, float exitTime) const
    {
        const float duration = StateDuration(playhead.state);
        if (duration <= 0.0f) {
            return true;
        }
        const float normalized = NormalizedOf(playhead);
        if (playhead.loop) {
            return exitTime < 1.0f ? (normalized >= exitTime || playhead.wrapped) : playhead.wrapped;
        }
        return normalized >= (std::min)(exitTime, 1.0f);
    }

    void AnimatorRuntime::CollectEvents(Playhead& playhead, float deltaTime)
    {
        const bool includeFrom = playhead.fresh;
        playhead.fresh = false;
        if (playhead.state < 0 || deltaTime < 0.0f || layer_->states[static_cast<size_t>(playhead.state)].speed < 0.0f) {
            return;
        }
        const int clip = EventClip(playhead);
        if (clip < 0 || eventOrder_[static_cast<size_t>(clip)].empty()) {
            return;
        }
        const float clipDuration = ClipDuration(clip);
        // ブレンドツリーの進みの割合をそのクリップの秒へ直す
        const float scale = IsBlend(playhead.state) ? clipDuration : 1.0f;
        const float from = playhead.previous * scale;
        const float to = playhead.time * scale;
        if (playhead.wrapped) {
            EmitEvents(clip, playhead.state, from, clipDuration, includeFrom);
            EmitEvents(clip, playhead.state, 0.0f, to, true);
        } else {
            EmitEvents(clip, playhead.state, from, to, includeFrom);
        }
    }

    int AnimatorRuntime::EventClip(const Playhead& playhead) const
    {
        if (playhead.state < 0 || static_cast<size_t>(playhead.state) >= motions_.size()) {
            return -1;
        }
        const StateMotion& motion = motions_[static_cast<size_t>(playhead.state)];
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

    void AnimatorRuntime::EmitEvents(int clip, int state, float from, float to, bool includeFrom)
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
            fired.state = layer_->states[static_cast<size_t>(state)].name;
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

    int AnimatorRuntime::ResolveBlendParameter(const AnimatorState& state, const std::string& name)
    {
        if (name.empty()) {
            WarnOnce("blendparam:" + state.name, "状態「" + state.name + "」のブレンドツリーのパラメータが選ばれていません（0 として混ぜます）");
            return -1;
        }
        const auto it = parameterIndices_.find(name);
        if (it == parameterIndices_.end()) {
            WarnOnce("blendparam:" + state.name + name,
                "状態「" + state.name + "」のブレンドツリーのパラメータ「" + name + "」がありません（0 として混ぜます）");
            return -1;
        }
        if (parameters_[static_cast<size_t>(it->second)].type != AnimatorParameterType::Float) {
            WarnOnce("blendparam:" + state.name + name,
                "状態「" + state.name + "」のブレンドツリーのパラメータ「" + name + "」が Float ではありません（0 として混ぜます）");
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

    void AnimatorRuntime::CheckTransitions()
    {
        const int state = current_.state;
        for (const ResolvedTransition& transition : anyStateTransitions_) {
            if (transition.to == state) {
                continue;
            }
            if (transition.hasExitTime && !ExitReached(current_, transition.exitTime)) {
                continue;
            }
            if (ConditionsMet(transition)) {
                Fire(transition);
                return;
            }
        }
        if (state < 0) {
            return;
        }
        for (const ResolvedTransition& transition : stateTransitions_[static_cast<size_t>(state)]) {
            if (transition.hasExitTime && !ExitReached(current_, transition.exitTime)) {
                continue;
            }
            if (ConditionsMet(transition)) {
                Fire(transition);
                return;
            }
        }
    }

    void AnimatorRuntime::Fire(const ResolvedTransition& transition)
    {
        for (const Condition& condition : transition.conditions) {
            if (condition.parameter >= 0 && condition.op == AnimatorConditionOp::Trigger) {
                parameters_[static_cast<size_t>(condition.parameter)].boolValue = false;
            }
        }
        const Playhead next = MakePlayhead(transition.to, transition.offset, std::nullopt);
        if (transition.duration <= 0.0f) {
            current_ = next;
            transition_.reset();
            return;
        }
        Transition started;
        started.next = next;
        started.duration = transition.duration;
        started.tableIndex = transition.tableIndex;
        transition_ = started;
    }

    void AnimatorRuntime::SamplePlayhead(const Playhead& playhead, Pose& out) const
    {
        if (playhead.state < 0 || static_cast<size_t>(playhead.state) >= motions_.size()) {
            PoseEvaluator::WriteBindPose(*skeleton_, out);
            return;
        }
        const StateMotion& motion = motions_[static_cast<size_t>(playhead.state)];
        if (motion.type == AnimatorMotionType::Clip) {
            if (motion.clip < 0 || !clips_[static_cast<size_t>(motion.clip)]) {
                PoseEvaluator::WriteBindPose(*skeleton_, out);
                return;
            }
            PoseEvaluator::Sample(*skeleton_, bindings_[static_cast<size_t>(motion.clip)], playhead.time, out);
            return;
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
        }
    }

    int AnimatorRuntime::FindStateIndex(const std::string& stateName) const
    {
        const auto it = stateIndices_.find(stateName);
        return it != stateIndices_.end() ? it->second : -1;
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
