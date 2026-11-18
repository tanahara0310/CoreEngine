#pragma once

#include "Animation/AnimationEvent.h"
#include "Animation/AnimatorController.h"
#include "Animation/Pose.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Quaternion/Quaternion.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace CoreEngine
{
    struct Animation;
    struct Skeleton;

    /// @brief 1 体ぶんのアニメーターの実行の状態
    /// @details 1 フレームの順番は、レイヤーごとに「時刻を進める → イベントを集める → 遷移を調べる → 姿勢を作る →
    ///          遷移が終わっていれば先の状態へ移る」、そのあと「レイヤーを上へ重ねる → クリップの移動を抜く → 骨の行列」。
    ///          遷移の途中は次の遷移を調べない。スクリプトの Play・CrossFade は遷移の途中でも効き、
    ///          途中の姿勢を写して止めた姿勢から混ぜる。
    ///          クリップの状態は時刻を秒で、ブレンドツリーの状態は進みの割合で持つ。
    ///          2 枚目からのレイヤーは重みとマスクの分だけ下の姿勢を上書きし、流すものの無い状態では下の姿勢のままにする。
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

        /// @brief 1 フレームのクリップの移動（モデルの空間）
        struct RootMotion
        {
            Vector3 position{};  ///< 水平の移動（y は 0）
            float yaw = 0.0f;    ///< 縦の軸まわりの回転（ラジアン）
        };

        /// @brief 設定と骨の並びから作り直す
        /// @param controller 設定
        /// @param clips 設定の clips と同じ並びのクリップ（読めなかったものは nullptr）
        /// @param skeleton 骨の並び（呼び出し側が生かしておく）
        /// @param ownerName 警告に出す持ち主の名前
        /// @note 同じ名前と種類のパラメータは今の値を引き継ぐ。状態は最初の状態へ、レイヤーの重みは設定の値へ戻す。
        void Build(std::shared_ptr<const AnimatorControllerData> controller,
            std::vector<std::shared_ptr<const Animation>> clips, const Skeleton* skeleton, std::string ownerName);

        /// @brief 何も流していない状態へ戻す
        void Reset();

        /// @brief 流せる状態か（設定と骨の並びがそろっているか）
        bool IsReady() const { return controller_ && skeleton_ && !layers_.empty(); }

        /// @brief 時間を進めて姿勢と骨の行列を作り、このフレームのイベントとクリップの移動を集める
        void Update(float deltaTime);

        /// @brief 最後の Update で出たイベント（出た順）
        const std::vector<AnimationEvent>& GetFiredEvents() const { return events_; }

        /// @brief 最初のレイヤーの状態の尺（秒。ブレンドツリーは今のパラメータの重みで子の尺を平均したもの。無い状態は 0）
        float GetStateDuration(const std::string& stateName) const { return GetLayerStateDuration(0, stateName); }

        /// @brief レイヤーの状態の尺（秒）
        float GetLayerStateDuration(int layer, const std::string& stateName) const;

        /// @brief 最初のレイヤーの状態の決まった進みの姿勢と骨の行列を作る（時刻・遷移・イベントは変えない）
        /// @param normalizedTime 0〜1
        /// @return 状態が見つかれば true
        bool EvaluateState(const std::string& stateName, float normalizedTime) { return EvaluateLayerState(0, stateName, normalizedTime); }

        /// @brief レイヤーの状態の決まった進みの姿勢と骨の行列を作る（時刻・遷移・イベントは変えない）
        /// @details 2 枚目からは、最初のレイヤーの今の状態の同じ進みの姿勢へ、重み 1 とマスクで重ねる。
        bool EvaluateLayerState(int layer, const std::string& stateName, float normalizedTime);

        /// @brief 骨ごとのスケルトン空間の行列（最後に作ったもの）
        const std::vector<Matrix4x4>& GetSkeletonSpaceMatrices() const { return matrices_; }

        /// @brief 最後に作った姿勢（レイヤーを重ねてクリップの移動を抜いたもの）
        const Pose& GetPose() const { return pose_; }

        const AnimatorControllerData* GetController() const { return controller_.get(); }
        const Skeleton* GetSkeleton() const { return skeleton_; }

        // ===== レイヤー =====

        int GetLayerCount() const { return static_cast<int>(layers_.size()); }

        /// @brief 名前でレイヤーを引く（無ければ -1）
        int FindLayer(std::string_view layerName) const;

        /// @brief 名前でレイヤーを引く（無ければ警告を 1 回だけ出して -1）
        int ResolveLayer(const std::string& layerName);

        /// @brief レイヤーの名前（無ければ空）
        const std::string& GetLayerName(int layer) const;

        /// @brief レイヤーの重み（0〜1）を変える（最初のレイヤーは 1 のまま）
        void SetLayerWeight(int layer, float weight);

        /// @brief レイヤーの今の重み（無いレイヤーは 0）
        float GetLayerWeight(int layer) const;

        // ===== クリップの移動 =====

        /// @brief 最初のレイヤーのクリップの移動を姿勢から抜いて GetRootMotion へ回すか
        void SetRootMotionEnabled(bool enabled) { rootMotionEnabled_ = enabled; }
        bool IsRootMotionEnabled() const { return rootMotionEnabled_; }

        /// @brief 最後の Update で出たクリップの移動（抜いていなければ 0）
        const RootMotion& GetRootMotion() const { return rootMotion_; }

        /// @brief クリップの移動を取る骨の番号（無ければ -1）
        int GetRootMotionJoint() const { return rootJoint_; }

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

        // ===== 状態（最初のレイヤー） =====

        /// @brief 状態をすぐ切り替える（最初のコマから流す）
        /// @param loop ループするかを状態の設定と変えるとき
        /// @return 状態が見つかれば true
        bool Play(const std::string& stateName, std::optional<bool> loop = std::nullopt) { return PlayInLayer(0, stateName, loop); }

        /// @brief 混ぜながら状態を切り替える（遷移の途中なら途中の姿勢から混ぜる）
        /// @param seconds 混ぜる秒数（0 以下ならすぐ切り替える）
        bool CrossFade(const std::string& stateName, float seconds, std::optional<bool> loop = std::nullopt)
        {
            return CrossFadeInLayer(0, stateName, seconds, loop);
        }

        /// @brief 今の状態の名前（遷移の途中なら移る先。流していなければ空）
        const std::string& GetCurrentStateName() const { return GetLayerStateName(0); }

        /// @brief 今の状態の番号（遷移の途中なら移る先。流していなければ -1）
        int GetCurrentStateIndex() const { return GetLayerStateIndex(0); }

        bool IsInState(const std::string& stateName) const { return IsInLayerState(0, stateName); }

        /// @brief 今の状態の進み（ループの中の 0〜1）
        float GetNormalizedTime() const { return GetLayerNormalizedTime(0); }

        bool IsInTransition() const { return IsLayerInTransition(0); }

        /// @brief 遷移の進み（0〜1。遷移していなければ 0）
        float GetTransitionProgress() const { return GetLayerTransitionProgress(0); }

        /// @brief 遷移の元の状態の番号（遷移していないか、止めた姿勢から混ぜていれば -1）
        int GetTransitionSourceIndex() const { return GetLayerTransitionSourceIndex(0); }

        /// @brief 流れている遷移の、設定の transitions の中の番号（スクリプトの切り替えなら -1）
        int GetActiveTransitionIndex() const { return GetLayerActiveTransitionIndex(0); }

        // ===== 状態（レイヤーを選ぶ） =====

        bool PlayInLayer(int layer, const std::string& stateName, std::optional<bool> loop = std::nullopt);
        bool CrossFadeInLayer(int layer, const std::string& stateName, float seconds, std::optional<bool> loop = std::nullopt);
        const std::string& GetLayerStateName(int layer) const;
        int GetLayerStateIndex(int layer) const;
        bool IsInLayerState(int layer, const std::string& stateName) const;
        float GetLayerNormalizedTime(int layer) const;
        bool IsLayerInTransition(int layer) const;
        float GetLayerTransitionProgress(int layer) const;
        int GetLayerTransitionSourceIndex(int layer) const;
        int GetLayerActiveTransitionIndex(int layer) const;

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
            bool frozenSource = false;  ///< 元は止めた姿勢（レイヤーの frozenPose）
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

        /// @brief レイヤー 1 枚の実行の状態
        struct LayerState
        {
            const AnimatorLayer* layer = nullptr;
            bool base = true;  ///< 最初のレイヤー（流すものの無い状態は骨の初期の姿勢にする）
            std::vector<StateMotion> motions;  ///< 状態ごと
            std::unordered_map<std::string, int> stateIndices;
            std::vector<ResolvedTransition> anyStateTransitions;
            std::vector<std::vector<ResolvedTransition>> stateTransitions;  ///< 状態ごとの出ていく遷移（表の順）
            Playhead current;
            std::optional<Transition> transition;
            float weight = 1.0f;
            std::vector<float> mask;  ///< 骨ごとの効き（空なら全部の骨）
            Pose pose;
            Pose sourcePose;
            Pose targetPose;
            Pose frozenPose;
            float coverage = 1.0f;        ///< 姿勢の効き（流すものの無い状態は 0）
            float frozenCoverage = 1.0f;  ///< frozenPose の効き
        };

        /// @brief 1 区間のクリップの移動（区間の頭の向きから見た移動と回転）
        struct MotionSegment
        {
            Vector3 position{};
            float yaw = 0.0f;
        };

        const LayerState* LayerAt(int layer) const;
        LayerState* LayerAt(int layer);
        void BuildLayer(LayerState& layer, const AnimatorLayer& source, bool base);
        void UpdateLayer(LayerState& layer, float deltaTime);
        /// @brief 2 枚目からのレイヤーの遷移の途中の姿勢と効き（流すものの無い側は下の姿勢として混ぜる）
        void MixCoverage(LayerState& layer, const Pose& source, float sourceCoverage, float targetCoverage, float weight);
        /// @brief 最初のレイヤーの姿勢へ 2 枚目からを重みとマスクの分だけ重ねる
        void ComposeLayers();
        /// @brief 警告に付けるレイヤーの前置き（最初のレイヤーは空）
        std::string Where(const LayerState& layer) const;

        bool IsBlend(const LayerState& layer, int state) const;
        float ClipDuration(int clip) const;
        float ParameterFloat(int parameter) const;
        /// @brief ブレンドツリーの子の重み（クリップの無い子は 0 にして足して 1 へ直す）
        void ComputeWeights(const StateMotion& motion, std::vector<float>& weights) const;
        /// @brief 状態の尺（ブレンドツリーは子の尺を重みで平均したもの）
        float StateDuration(const LayerState& layer, int state) const;
        float NormalizedOf(const LayerState& layer, const Playhead& playhead) const;
        Playhead MakePlayhead(const LayerState& layer, int state, float normalizedOffset, std::optional<bool> loop) const;
        void Advance(const LayerState& layer, Playhead& playhead, float deltaTime) const;
        bool ExitReached(const LayerState& layer, const Playhead& playhead, float exitTime) const;
        /// @brief 進めた流れのイベントを集める（ブレンドツリーはいちばん重い子のクリップだけ）
        /// @details 範囲は (前の時刻, 今の時刻]。入ってから初めて集めるときは前の時刻ちょうども含める。
        ///          速さが負のときと、重み 0 の 2 枚目からのレイヤーは集めない。
        void CollectEvents(const LayerState& layer, Playhead& playhead, float deltaTime);
        /// @brief イベントを読むクリップ（無ければ -1）
        int EventClip(const LayerState& layer, const Playhead& playhead) const;
        /// @brief クリップの [from, to]（includeFrom が false なら (from, to]）のイベントを足す
        void EmitEvents(const LayerState& layer, int clip, int state, float from, float to, bool includeFrom);
        int FindClipIndex(const std::string& clipId) const;
        int ResolveBlendParameter(const LayerState& layer, const AnimatorState& state, const std::string& name);
        bool ConditionsMet(const ResolvedTransition& transition) const;
        void CheckTransitions(LayerState& layer);
        void Fire(LayerState& layer, const ResolvedTransition& transition);
        /// @brief 流れの姿勢を書く
        /// @return 姿勢の効き（流すものがあれば 1。無ければ骨の初期の姿勢を書いて 0）
        float SamplePlayhead(const LayerState& layer, const Playhead& playhead, Pose& out) const;
        int FindStateIndex(const LayerState& layer, const std::string& stateName) const;

        /// @brief クリップの移動を取る骨と、その親までの初期の姿勢の行列を決める
        void ResolveRootMotion();
        /// @brief 最初のレイヤーのこのフレームのクリップの移動を足す
        void AccumulateRootMotion(const LayerState& layer, float deltaTime);
        /// @brief 流れのこのフレームの区間のクリップの移動
        MotionSegment PlayheadMotion(const LayerState& layer, const Playhead& playhead, float deltaTime) const;
        /// @brief クリップの from 秒から to 秒の移動（ループの頭へ戻っていれば尾までと頭からをつなぐ）
        MotionSegment ClipMotion(int clip, float from, float to, bool wrapped, bool forward) const;
        /// @brief クリップの from 秒から to 秒までをまっすぐ取った移動
        MotionSegment ClipSegment(int clip, float from, float to) const;
        /// @brief クリップの時刻の根の骨のモデルの空間の位置と縦の軸まわりの向き
        void SampleRoot(int clip, float time, Vector3& position, float& yaw) const;
        /// @brief 姿勢の根の骨の水平の位置と縦の軸まわりの向きを初期の姿勢に戻す
        void RemoveRootMotion(Pose& pose) const;

        int FindParameter(const std::string& name, AnimatorParameterType type, bool triggerOrBool = false);
        void WarnOnce(const std::string& key, const std::string& message);

        std::shared_ptr<const AnimatorControllerData> controller_;
        const Skeleton* skeleton_ = nullptr;
        std::string ownerName_;

        std::vector<std::shared_ptr<const Animation>> clips_;
        std::vector<ClipBinding> bindings_;              ///< クリップごと
        std::vector<std::vector<int>> eventOrder_;        ///< クリップごとのイベントの番号（時刻の順）
        std::unordered_map<std::string, int> parameterIndices_;
        std::vector<LayerState> layers_;

        std::vector<ParameterValue> parameters_;

        bool rootMotionEnabled_ = false;
        RootMotion rootMotion_;
        int rootJoint_ = -1;
        Matrix4x4 rootParentMatrix_{};    ///< 根の骨の親までの初期の姿勢のモデルの空間の行列
        Matrix4x4 rootParentInverse_{};
        Quaternion rootParentRotation_{};  ///< 根の骨の親までの初期の姿勢のモデルの空間の回転
        Vector3 bindRootPosition_{};       ///< 根の骨の初期の姿勢のモデルの空間の位置
        float bindRootYaw_ = 0.0f;         ///< 根の骨の初期の姿勢の縦の軸まわりの向き

        Pose pose_;
        mutable Pose blendScratch_;
        mutable std::vector<float> weights_;
        std::vector<Matrix4x4> matrices_;
        std::vector<AnimationEvent> events_;
        std::unordered_set<std::string> warned_;
    };
}
