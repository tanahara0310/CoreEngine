#pragma once

#include "ISceneFeature.h"
#include "GameObject/Component/Core/ObjectRef.h"

#include <chrono>
#include <cstdint>

namespace CoreEngine
{
    class SkyBoxComponent;
    class VolumetricCloudComponent;
    class HeightFogComponent;
    class PostProcessComponent;
    class GameObject;

    /// @brief 環境（空・大気散乱・雲・霧）をシーンのオブジェクトとして置く Feature
    /// @details シーンのオブジェクトが出そろった後（PostSceneInitialize）に、空・雲・霧・
    ///          ポストエフェクトのコンポーネントを採用する
    ///          （足りなければ `Environment` オブジェクトを作って載せる）。
    ///          PostLogic で大気散乱 → 雲 → 霧の順に毎フレーム反映する。
    /// @note パラメータと雲・霧の有効・無効の実体は CVar が持つ（保存はシーンの `_environment.json`）。
    ///       コンポーネントは「シーンに置かれている」ことを表し、雲・霧のチェックは CVar と同じ値になる。
    /// @note 地平線より下の地面は大気散乱そのものが描く（Sky-View LUT の地表反射項）。
    class EnvironmentFeature : public ISceneFeature {
    public:
        const char* GetName() const override { return "Environment"; }

        void PostSceneInitialize(SceneContext& ctx) override;
        void Update(SceneContext& ctx, SceneUpdatePhase phase) override;

        /// @brief 停止中も回す（止めると大気・フォグのエディタが効かなくなる）
        bool RunsWhileStopped() const override { return true; }
        void Finalize(SceneContext& ctx) override;

    private:
        /// @brief 環境のコンポーネントを採用する（足りなければ `Environment` を作って載せる）
        void SetupEnvironmentObject(SceneContext& ctx);

        /// @brief 雲・霧のチェックを CVar に合わせ、チェックの切り替えが CVar へ写るよう結ぶ
        void BindComponentToggles();

        /// @brief 雲・霧の有効の CVar がパネル・コンソール・スクリプトで変わったら、チェックへ写す
        /// @note チェックの切り替えは、コンポーネントの OnEnable / OnDisable が CVar へ写す。
        void FollowToggleCVars();

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

        // 環境のコンポーネント（所有権は GameObjectManager。使うたびに ID から引き直す）
        ObjectRef<SkyBoxComponent> skyBox_;
        ObjectRef<VolumetricCloudComponent> cloud_;
        ObjectRef<HeightFogComponent> fog_;
        ObjectRef<PostProcessComponent> postProcess_;

        // 雲・霧の有効の CVar を最後にチェックへ写したときの通番
        uint32_t cloudEnabledRevision_ = 0;
        uint32_t fogEnabledRevision_ = 0;

#ifdef CORE_EDITOR
        // シーンが持つ値の変更を見張るための控え
        uint64_t lastEnvironmentRevision_ = 0;
        bool environmentDirty_ = false;
        std::chrono::steady_clock::time_point lastEnvironmentChange_{};
#endif
    };
}
