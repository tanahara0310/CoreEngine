#pragma once

#include "ISceneFeature.h"
#include "Editor/Environment/AtmosphereLightsSettingsSection.h"
#include <memory>

// 前方宣言
class SkyBoxObject;
class InfiniteGroundObject;

namespace CoreEngine
{
    /// @brief 既定の環境（空・無限床・大気散乱・雲）を管理する Feature
    /// @details PostSceneInitialize でシーン生成済みの SkyBox / 無限床を採用
    ///          （未生成なら自動生成）し、PreObjectUpdate で床のカメラ追従、
    ///          PostLogic で大気散乱→雲の順に毎フレーム反映する。
    class EnvironmentFeature : public ISceneFeature {
    public:
        const char* GetName() const override { return "Environment"; }

        void PostSceneInitialize(SceneContext& ctx) override;
        void Update(SceneContext& ctx, SceneUpdatePhase phase) override;
        void Finalize(SceneContext& ctx) override;

        /// @brief 既定の無限地面（y=0 のグレータイル床）を自動生成するかどうか
        /// @note PostSceneInitialize より前に設定すること（BaseScene が
        ///       WantsDefaultGround() の値を反映する）
        void SetWantsDefaultGround(bool wants) { wantsDefaultGround_ = wants; }

        /// @brief シーンの SkyBox を取得（既定で大気散乱モード）
        SkyBoxObject* GetSkyBox() const { return skyBox_; }

    private:
        /// @brief 既定の空（大気散乱モードの SkyBox）のセットアップ
        void SetupDefaultSky(SceneContext& ctx);

        /// @brief 既定の無限地面（y=0 のグレータイル床）のセットアップ
        void SetupDefaultGround(SceneContext& ctx);

        /// @brief 既定の無限地面をカメラ XZ に追従させる（毎フレーム）
        void UpdateGroundPlane(SceneContext& ctx);

        /// @brief 大気散乱システム（と雲）の毎フレーム更新
        /// @details SkyBox が大気散乱モードの場合のみ AtmosphereManager へ太陽情報と
        ///          カメラ情報を反映する（LUT 生成・Aerial Perspective の有効化トリガ）。
        void UpdateAtmosphere(SceneContext& ctx);

        // 既定背景の SkyBox（所有権は GameObjectManager。Finalize でポインタをクリアする）
        SkyBoxObject* skyBox_ = nullptr;

        // 既定の無限地面（所有権は GameObjectManager。Finalize でポインタをクリアする）
        InfiniteGroundObject* groundPlane_ = nullptr;

        bool wantsDefaultGround_ = true;

        // 太陽・月ライトのエディタ設定自動保存セクション（ライトはシーン寿命のため、
        // ライト生成後の PostSceneInitialize で登録し Finalize で解除する。
        // EditorSettingsSubsystem が無いビルド（USE_IMGUI 無効）では未登録のまま）
        std::unique_ptr<AtmosphereLightsSettingsSection> atmosphereLightsSection_;
    };
}
