#pragma once

#include "Animation/AnimationEvent.h"
#include "Animation/AnimatorController.h"
#include "Animation/Pose.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector2.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace CoreEngine
{
    struct Animation;
    struct Skeleton;

    /// @brief 1 体ぶんのアニメーターの実行の状態
    /// @details 1 フレームの順番は「時刻を進める → イベントを集める → 遷移を調べる → 姿勢を作る →
    ///          遷移が終わっていれば先の状態へ移る → 骨の行列」。
    ///          遷移の途中は次の遷移を調べない。スクリプトの Play・CrossFade は遷移の途中でも効き、
    ///          途中の姿勢を写して止めた姿勢から混ぜる。
    ///          クリップの状態は時刻を秒で、ブレンドツリーの状態は進みの割合で持つ。
    class AnimatorRuntime
    {
    public:
        /// @brief パラメータの今の値
        struct ParameterValue
        {
            AnimatorParameterType type = AnimatorParameterType::Float;
            float floatValue = 0.0f;
            int intValue = 0;
            bool boolValue = false;
        };

        /// @brief 設定と骨の並びから作り直す
        /// @param controller 設定（最初のレイヤーだけを流す）
        /// @param clips 設定の clips と同じ並びのクリップ（読めなかったものは nullptr）
        /// @param skeleton 骨の並び（呼び出し側が生かしておく）
        /// @param ownerName 警告に出す持ち主の名前
        /// @note 同じ名前と種類のパラメータは今の値を引き継ぐ。状態は最初の状態へ戻す。
        void Build(std::shared_ptr<const AnimatorControllerData> controller,
            std::vector<std::shared_ptr<const Animation>> clips, const Skeleton* skeleton, std::string ownerName);

        /// @brief 何も流していない状態へ戻す
        void Reset();

        /// @brief 流せる状態か（設定と骨の並びがそろっているか）
        bool IsReady() const { return controller_ && skeleton_ && layer_; }

        /// @brief 時間を進めて姿勢と骨の行列を作り、このフレームのイベントを集める
        void Update(float deltaTime);

        /// @brief 最後の Update で出たイベント（出た順）
        const std::vector<AnimationEvent>& GetFiredEvents() const { return events_; }

        /// @brief 状態の尺（秒。ブレンドツリーは今のパラメータの重みで子の尺を平均したもの。無い状態は 0）
        float GetStateDuration(const std::string& stateName) const;

        /// @brief 状態の決まった進みの姿勢と骨の行列を作る（時刻・遷移・イベントは変えない）
        /// @param normalizedTime 0〜1
        /// @return 状態が見つかれば true
        bool EvaluateState(const std::string& stateName, float normalizedTime);

        /// @brief 骨ごとのスケルトン空間の行列（最後に作ったもの）
        const std::vector<Matrix4x4>& GetSkeletonSpaceMatrices() const { return matrices_; }

        /// @brief 最後に作った姿勢
        const Pose& GetPose() const { return pose_; }

        const AnimatorControllerData* GetController() const { return controller_.get(); }
        const Skeleton* GetSkeleton() const { return skeleton_; }

        // ===== パラメータ =====

        void SetFloat(const std::string& name, float value);
        float GetFloat(const std::string& name);
        void SetInt(const std::string& name, int value);
        int GetInt(const std::string& name);
        void SetBool(const std::string& name, bool value);
        bool GetBool(const std::string& name);
        void SetTrigger(const std::string& name);
        void ResetTrigger(const std::string& name);

        /// @brief パラメータの今の値（設定の parameters と同じ並び）
        const std::vector<ParameterValue>& GetParameterValues() const { return parameters_; }

        // ===== 状態 =====

        /// @brief 状態をすぐ切り替える（最初のコマから流す）
        /// @param loop ループするかを状態の設定と変えるとき
        /// @return 状態が見つかれば true
        bool Play(const std::string& stateName, std::optional<bool> loop = std::nullopt);

        /// @brief 混ぜながら状態を切り替える（遷移の途中なら途中の姿勢から混ぜる）
        /// @param seconds 混ぜる秒数（0 以下ならすぐ切り替える）
        bool CrossFade(const std::string& stateName, float seconds, std::optional<bool> loop = std::nullopt);

        /// @brief 今の状態の名前（遷移の途中なら移る先。流していなければ空）
        const std::string& GetCurrentStateName() const;

        /// @brief 今の状態の番号（遷移の途中なら移る先。流していなければ -1）
        int GetCurrentStateIndex() const;

        bool IsInState(const std::string& stateName) const;

        /// @brief 今の状態の進み（ループの中の 0〜1）
        float GetNormalizedTime() const;

        bool IsInTransition() const { return transition_.has_value(); }

        /// @brief 遷移の進み（0〜1。遷移していなければ 0）
        float GetTransitionProgress() const;

        /// @brief 遷移の元の状態の番号（遷移していないか、止めた姿勢から混ぜていれば -1）
        int GetTransitionSourceIndex() const;

        /// @brief 流れている遷移の、設定の transitions の中の番号（スクリプトの切り替えなら -1）
        int GetActiveTransitionIndex() const;

    private:
        /// @brief 1 つの状態の流れ
        struct Playhead
        {
            int state = -1;
            float time = 0.0f;      ///< クリップの状態は秒・ブレンドツリーの状態は進みの割合
            float previous = 0.0f;  ///< 最後に進める前の time
            bool loop = true;
            bool wrapped = false;  ///< 最後に進めたときにループの頭へ戻ったか
            bool fresh = true;     ///< 入ってからまだイベントを集めていない
        };

        /// @brief 状態が流すもの（引き当てた番号）
        struct StateMotion
        {
            AnimatorMotionType type = AnimatorMotionType::Clip;
            int clip = -1;          ///< クリップの状態のクリップ（無ければ -1）
            int parameterX = -1;    ///< ブレンドツリーのパラメータ（無ければ -1。値は 0 とみなす）
            int parameterY = -1;
            std::vector<int> childClips;  ///< 子ごとのクリップ（無ければ -1）
            std::vector<float> thresholds;
            std::vector<Vector2> positions;
        };

        /// @brief 流れている遷移
        struct Transition
        {
            Playhead next;
            float elapsed = 0.0f;
            float duration = 0.0f;
            bool frozenSource = false;  ///< 元は止めた姿勢（frozenPose_）
            int tableIndex = -1;
        };

        /// @brief 引き当てた条件
        struct Condition
        {
            int parameter = -1;  ///< -1 なら名前が無い（常に満たさない）
            AnimatorConditionOp op = AnimatorConditionOp::Greater;
            float value = 0.0f;
        };

        /// @brief 引き当てた遷移
        struct ResolvedTransition
        {
            int to = -1;
            std::vector<Condition> conditions;
            float duration = 0.0f;
            bool hasExitTime = false;
            float exitTime = 0.0f;
            float offset = 0.0f;
            int tableIndex = -1;
        };

        bool IsBlend(int state) const;
        float ClipDuration(int clip) const;
        float ParameterFloat(int parameter) const;
        /// @brief ブレンドツリーの子の重み（クリップの無い子は 0 にして足して 1 へ直す）
        void ComputeWeights(const StateMotion& motion, std::vector<float>& weights) const;
        /// @brief 状態の尺（ブレンドツリーは子の尺を重みで平均したもの）
        float StateDuration(int state) const;
        float NormalizedOf(const Playhead& playhead) const;
        Playhead MakePlayhead(int state, float normalizedOffset, std::optional<bool> loop) const;
        void Advance(Playhead& playhead, float deltaTime) const;
        bool ExitReached(const Playhead& playhead, float exitTime) const;
        /// @brief 進めた流れのイベントを集める（ブレンドツリーはいちばん重い子のクリップだけ）
        /// @details 範囲は (前の時刻, 今の時刻]。入ってから初めて集めるときは前の時刻ちょうども含める。
        ///          速さが負のときは集めない。
        void CollectEvents(Playhead& playhead, float deltaTime);
        /// @brief イベントを読むクリップ（無ければ -1）
        int EventClip(const Playhead& playhead) const;
        /// @brief クリップの [from, to]（includeFrom が false なら (from, to]）のイベントを足す
        void EmitEvents(int clip, int state, float from, float to, bool includeFrom);
        int FindClipIndex(const std::string& clipId) const;
        int ResolveBlendParameter(const AnimatorState& state, const std::string& name);
        bool ConditionsMet(const ResolvedTransition& transition) const;
        void CheckTransitions();
        void Fire(const ResolvedTransition& transition);
        void SamplePlayhead(const Playhead& playhead, Pose& out) const;
        int FindStateIndex(const std::string& stateName) const;
        /// @brief 名前と種類でパラメータを引く（無ければ警告を 1 回だけ出して -1）
        int FindParameter(const std::string& name, AnimatorParameterType type, bool triggerOrBool = false);
        void WarnOnce(const std::string& key, const std::string& message);

        std::shared_ptr<const AnimatorControllerData> controller_;
        const AnimatorLayer* layer_ = nullptr;
        const Skeleton* skeleton_ = nullptr;
        std::string ownerName_;

        std::vector<std::shared_ptr<const Animation>> clips_;
        std::vector<ClipBinding> bindings_;              ///< クリップごと
        std::vector<std::vector<int>> eventOrder_;        ///< クリップごとのイベントの番号（時刻の順）
        std::vector<StateMotion> motions_;                ///< 状態ごと
        std::unordered_map<std::string, int> stateIndices_;
        std::unordered_map<std::string, int> parameterIndices_;
        std::vector<ResolvedTransition> anyStateTransitions_;
        std::vector<std::vector<ResolvedTransition>> stateTransitions_;  ///< 状態ごとの出ていく遷移（表の順）

        std::vector<ParameterValue> parameters_;
        Playhead current_;
        std::optional<Transition> transition_;

        Pose pose_;
        Pose sourcePose_;
        Pose targetPose_;
        Pose frozenPose_;
        mutable Pose blendScratch_;
        mutable std::vector<float> weights_;
        std::vector<Matrix4x4> matrices_;
        std::vector<AnimationEvent> events_;
        std::unordered_set<std::string> warned_;
    };
}
