#pragma once

#include "ISceneFeature.h"

#include "Camera/Rig/CameraRigRuntime.h"

#include <string>
#include <unordered_map>

/// @file
/// @brief カメラリグの評価結果をゲーム視点カメラへ反映する Feature

namespace CoreEngine
{
    class GameObjectManager;

    /// @brief 動作中のリグが決めた構図を、ゲーム視点カメラへ書き込む
    ///
    /// @details
    /// **SceneUpdatePhase::PostLogic + kLateFeaturePriority で、CameraSequenceFeature より
    /// 先に登録すること。** 重ね順がこの Feature の設計そのものになっている。
    ///
    ///  - 追従コンポーネント（OnLateUpdate で構図を決めるもの）より後 …
    ///    PostLogic は OnLateUpdate の後なので、このフェーズに居るだけで満たされる。
    ///    リグはゲーム側の追従を「置き換える」層なので、後から上書きする側でよい。
    ///  - CameraSequenceFeature より先 …
    ///    カットシーンはリグが決めた構図の上に来る。逆順だとカットシーンが
    ///    毎フレームリグに消される。
    ///
    /// つまり「リグ → シーケンス → 揺れ」の順で 1 つのカメラを重ね書きする。
    /// 誰がカメラを握っているかは CameraRig::IsActive() で外から見える。
    ///
    /// 対象は常にゲーム視点カメラ（CameraManager::GetGameCameraName()）で、エディタ視点
    /// カメラは対象外。編集中に画面が奪われると構図の確認ができなくなる。
    ///
    /// リグを動かしていない間は一切カメラに触れないので、ゲーム側の追従がそのまま効く。
    class CameraRigFeature : public ISceneFeature {
    public:
        const char* GetName() const override { return "CameraRig"; }

        /// @brief 静的ファサード（CameraRig::Activate など）の委譲先になる
        void Initialize(SceneContext& ctx) override;

        /// @brief シーンに保存された開始リグを動かす
        /// @details オブジェクトが揃った後に呼ばれるので、対象を名前で引ける。
        ///          保存が無ければ何もしない（ゲーム側の追従がそのまま効く）。
        void PostSceneInitialize(SceneContext& ctx) override;

        /// @brief PostLogic で減衰を進め、ゲーム視点カメラへ構図を書き込む
        void Update(SceneContext& ctx, SceneUpdatePhase phase) override;

        /// @brief 委譲先から外れ、リグを止める
        void PostSceneFinalize(SceneContext& ctx) override;

        /// @brief ランタイムへの直接アクセス（デバッグ UI・シーン固有の細かい制御用）
        CameraRigRuntime& GetRuntime() { return runtime_; }
        const CameraRigRuntime& GetRuntime() const { return runtime_; }

    private:
        /// @brief 動作中のリグが参照する対象の速度を、位置の差分から作り直す
        /// @details GameObject は速度を持たないので、ここでフレーム間の移動量から求める。
        ///          SpeedToFov のためだけに必要。参照されている対象だけを見る。
        void UpdateTargetVelocities(GameObjectManager* objects, float deltaTime);

        /// @brief 名前でシーンのオブジェクトを引く解決口を作る
        /// @details 評価器はシーンを知らないので、ここで橋渡しする。
        CameraRigContext MakeContext(GameObjectManager* objects, float aspectRatio) const;

        CameraRigRuntime runtime_;

        // 対象名 → 前フレームの位置 / 今フレームの速度。参照されている対象のぶんだけ持つ。
        std::unordered_map<std::string, Vector3> lastPositions_;
        std::unordered_map<std::string, Vector3> velocities_;

        // 繋ぎ用。切り替わった瞬間のカメラ姿勢を繋ぎ元として控える。
        CameraSnapshot blendFrom_{};
        bool hasBlendFrom_ = false;
        std::uint32_t observedActivationId_ = 0;
    };
}
