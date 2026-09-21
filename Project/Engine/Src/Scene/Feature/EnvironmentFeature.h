#pragma once

#include "ISceneFeature.h"

#include <chrono>
#include <cstdint>

namespace CoreEngine
{
    class SkyBoxComponent;

    /// @brief 既定の環境（空・大気散乱・雲）を管理する Feature
    /// @details シーンのオブジェクトが出そろった後（PostSceneInitialize）に SkyBox を
    ///          採用（未生成なら自動生成）し、
    ///          PostLogic で大気散乱 → 雲の順に毎フレーム反映する。
    /// @note 地平線より下の地面は大気散乱そのものが描く（Sky-View LUT の地表反射項）。
    class EnvironmentFeature : public ISceneFeature {
    public:
        const char* GetName() const override { return "Environment"; }

        void PostSceneInitialize(SceneContext& ctx) override;
        void Update(SceneContext& ctx, SceneUpdatePhase phase) override;

        /// @brief 停止中も回す（止めると大気・フォグのエディタが効かなくなる）
        bool RunsWhileStopped() const override { return true; }
        void Finalize(SceneContext& ctx) override;

        /// @brief シーンの SkyBox（大気散乱で描く空）を取得
        SkyBoxComponent* GetSkyBox() const { return skyBox_; }

    private:
        /// @brief 既定の空（大気散乱モードの SkyBox）のセットアップ
        void SetupDefaultSky(SceneContext& ctx);

        /// @brief 大気散乱システム（と雲）の毎フレーム更新
        /// @details SkyBox が大気散乱モードの場合のみ AtmosphereManager へ太陽情報と
        ///          カメラ情報を反映する（LUT 生成・Aerial Perspective の有効化トリガ）。
        void UpdateAtmosphere(SceneContext& ctx);

#ifdef CORE_EDITOR
        /// @brief 触った少し後に、シーンが持つ見た目の設定をシーンへ書く
        /// @details CVars.json へ自動保存していた頃と同じ間合い。動かしている間は書かず、
        ///          手を止めてから 1 回だけ書く。
        void AutoSaveEnvironment(SceneContext& ctx);
#endif

        /// @brief フォグの毎フレーム更新
        /// @details フォグは空・大気を必要としないので、SkyBox が無いシーンでも呼ぶ。
        ///          FogManager::Update が「このフレームはフォグを使う」フラグを立てる。
        void UpdateFog(SceneContext& ctx);

        // 既定背景の SkyBox（所有権は GameObjectManager。Finalize でポインタをクリアする）
        SkyBoxComponent* skyBox_ = nullptr;

#ifdef CORE_EDITOR
        // シーンが持つ値の変更を見張るための控え
        uint64_t lastEnvironmentRevision_ = 0;
        bool environmentDirty_ = false;
        std::chrono::steady_clock::time_point lastEnvironmentChange_{};
#endif
    };
}
