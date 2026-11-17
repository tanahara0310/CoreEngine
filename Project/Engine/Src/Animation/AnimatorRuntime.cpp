#include "pch.h"
#include "AnimatorRuntime.h"

#include "Animation/PoseEvaluator.h"
#include "Graphics/Model/Animation/Animation.h"
#include "Graphics/Model/Skeleton/Skeleton.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cmath>

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
        for (size_t i = 0; i < clips_.size(); ++i) {
            if (!clips_[i]) {
                continue;
            }
            bindings_[i] = PoseEvaluator::Bind(*skeleton_, *clips_[i]);
            if (bindings_[i].matchedCount == 0) {
                WarnOnce("bind:" + controller_->clips[i].id,
                    "クリップ「" + controller_->clips[i].id + "」に、このモデルの骨の名前と同じトラックがありません（動きません）");
            }
        }

        stateClips_.assign(layer_->states.size(), -1);
        for (size_t i = 0; i < layer_->states.size(); ++i) {
            const AnimatorState& state = layer_->states[i];
            stateIndices_.emplace(state.name, static_cast<int>(i));
            if (state.clip.empty()) {
                continue;
            }
            for (size_t c = 0; c < controller_->clips.size(); ++c) {
                if (controller_->clips[c].id == state.clip) {
                    stateClips_[i] = static_cast<int>(c);
                    break;
                }
            }
            if (stateClips_[i] < 0) {
                WarnOnce("clip:" + state.name, "状態「" + state.name + "」のクリップ「" + state.clip + "」がクリップの表にありません");
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
        stateClips_.clear();
        stateIndices_.clear();
        parameterIndices_.clear();
        anyStateTransitions_.clear();
        stateTransitions_.clear();
        parameters_.clear();
        current_ = Playhead{};
        transition_.reset();
    }

    void AnimatorRuntime::Update(float deltaTime)
    {
        if (!IsReady()) {
            return;
        }

        if (transition_) {
            if (!transition_->frozenSource) {
                Advance(current_, deltaTime);
            }
            Advance(transition_->next, deltaTime);
            transition_->elapsed += deltaTime;
        } else {
            Advance(current_, deltaTime);
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
        const Playhead& playhead = transition_ ? transition_->next : current_;
        const float duration = StateDuration(playhead.state);
        return duration > 0.0f ? playhead.time / duration : 0.0f;
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

    float AnimatorRuntime::StateDuration(int state) const
    {
        if (state < 0 || static_cast<size_t>(state) >= stateClips_.size()) {
            return 0.0f;
        }
        const int clip = stateClips_[static_cast<size_t>(state)];
        return clip >= 0 && clips_[static_cast<size_t>(clip)] ? clips_[static_cast<size_t>(clip)]->duration : 0.0f;
    }

    AnimatorRuntime::Playhead AnimatorRuntime::MakePlayhead(int state, float normalizedOffset, std::optional<bool> loop) const
    {
        Playhead playhead;
        playhead.state = state;
        playhead.loop = loop.value_or(layer_->states[static_cast<size_t>(state)].loop);
        playhead.time = normalizedOffset * StateDuration(state);
        return playhead;
    }

    void AnimatorRuntime::Advance(Playhead& playhead, float deltaTime) const
    {
        playhead.wrapped = false;
        if (playhead.state < 0) {
            return;
        }
        const float duration = StateDuration(playhead.state);
        playhead.time += deltaTime * layer_->states[static_cast<size_t>(playhead.state)].speed;
        if (duration <= 0.0f) {
            playhead.time = 0.0f;
            return;
        }
        if (playhead.loop) {
            if (playhead.time >= duration || playhead.time < 0.0f) {
                playhead.wrapped = true;
            }
            playhead.time = std::fmod(playhead.time, duration);
            if (playhead.time < 0.0f) {
                playhead.time += duration;
            }
        } else {
            playhead.time = std::clamp(playhead.time, 0.0f, duration);
        }
    }

    bool AnimatorRuntime::ExitReached(const Playhead& playhead, float exitTime) const
    {
        const float duration = StateDuration(playhead.state);
        if (duration <= 0.0f) {
            return true;
        }
        const float normalized = playhead.time / duration;
        if (playhead.loop) {
            return exitTime < 1.0f ? (normalized >= exitTime || playhead.wrapped) : playhead.wrapped;
        }
        return normalized >= (std::min)(exitTime, 1.0f);
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
        const int clip = playhead.state >= 0 ? stateClips_[static_cast<size_t>(playhead.state)] : -1;
        if (clip < 0 || !clips_[static_cast<size_t>(clip)]) {
            PoseEvaluator::WriteBindPose(*skeleton_, out);
            return;
        }
        PoseEvaluator::Sample(*skeleton_, bindings_[static_cast<size_t>(clip)], playhead.time, out);
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
