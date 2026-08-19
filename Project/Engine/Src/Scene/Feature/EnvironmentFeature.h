#pragma once

#include "ISceneFeature.h"

// 前方宣言
class SkyBoxObject;

namespace CoreEngine
{
    /// @brief 既定の環境（空・大気散乱・雲）を管理する Feature
    /// @details PostSceneInitialize で SkyBox を採用（未生成なら自動生成）し、
    ///          PostLogic で大気散乱 → 雲の順に毎フレーム反映する。
    /// @note 地平線より下の地面は大気散乱そのものが描く（Sky-View LUT の地表反射項）。
    class EnvironmentFeature : public ISceneFeature {
    public:
        const char* GetName() const override { return "Environment"; }

        void PostSceneInitialize(SceneContext& ctx) override;
        void Update(SceneContext& ctx, SceneUpdatePhase phase) override;
        void Finalize(SceneContext& ctx) override;

        /// @brief シーンの SkyBox（大気散乱で描く空）を取得
        SkyBoxObject* GetSkyBox() const { return skyBox_; }

    private:
        /// @brief 既定の空（大気散乱モードの SkyBox）のセットアップ
        void SetupDefaultSky(SceneContext& ctx);

        /// @brief 保存済みの太陽・月ライト設定（CVar）をシーンのライトへ復元する
        /// @details ライトはシーン寿命・CVar はエンジン寿命のため、ライト生成後の
        ///          PostSceneInitialize でこの向きに一度だけ流し込む。
        void RestoreAtmosphereLightsFromCVars(SceneContext& ctx);

        /// @brief 現在の太陽・月ライトの状態を CVar へ写す（毎フレーム）
        /// @details ライトが実体で CVar は鏡。エディタ・ギズモ・シーンコードの
        ///          どこから書き換えられても、ここを通ることで保存対象に載る。
        void MirrorAtmosphereLightsToCVars(SceneContext& ctx);

        /// @brief 大気散乱システム（と雲）の毎フレーム更新
        /// @details SkyBox が大気散乱モードの場合のみ AtmosphereManager へ太陽情報と
        ///          カメラ情報を反映する（LUT 生成・Aerial Perspective の有効化トリガ）。
        void UpdateAtmosphere(SceneContext& ctx);

        // 既定背景の SkyBox（所有権は GameObjectManager。Finalize でポインタをクリアする）
        SkyBoxObject* skyBox_ = nullptr;
    };
}
