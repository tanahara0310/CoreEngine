#pragma once

#include "ISceneFeature.h"
#include "Math/Vector/Vector2.h"
#include <cstdint>

namespace CoreEngine
{
    class GameObject;
    class MeshRendererComponent;
    class MaterialComponent;
    class Collider;
    class Camera;
    class EngineSystem;

    /// @brief どのシーンにも必ず存在する既定の床（ベース地面）を提供する Feature。
    ///
    /// 新規シーンを作っただけの状態では地面が 1 枚も無く、真下が無限の穴になる。
    /// UE の Basic レベルの Floor / Unity の Plane に相当する「最初からある床」を
    /// エンジン側で用意して、当たり判定・影・SSAO の受け皿を常に保証する。
    ///
    /// @note **地平線より遠方の地面は今も大気散乱が描く**（Sky-View LUT の地表反射項）。
    ///       この Feature が受け持つのは近〜中景（カメラのファークリップまで）だけで、
    ///       その外側は従来どおり大気の担当。両者の色を一致させるために既定では
    ///       床のアルベドを `AtmosphereParameters::groundAlbedo` に追従させる
    ///       （`r.Ground.UseAtmosphereAlbedo`）。ここを別々にチューニングすると、
    ///       かつて廃止した無限床と同じ「色合わせの往復」に戻るので注意。
    ///
    /// 生成した床は `SetSerializeEnabled(false)` でシーン JSON の対象外にしてある
    /// （エンジンが毎回作るオブジェクトなので、保存すると全シーンで増殖する）。
    class GroundFeature : public ISceneFeature {
    public:
        const char* GetName() const override { return "Ground"; }

        /// @brief Engine Settings に「Ground」パネルを登録する（デバッグビルドのみ）
        void Initialize(SceneContext& ctx) override;

        /// @brief シーンの OnInitialize() 完了後に床オブジェクトを生成する
        /// @details シーン側が `BaseScene::SetDefaultGroundEnabled(false)` で抑止したかを
        ///          見てから作るので、`PostSceneInitialize` のタイミングでなければならない。
        void PostSceneInitialize(SceneContext& ctx) override;

        /// @brief 床の位置・広さ・マテリアルを毎フレーム更新する（PreObjectUpdate）
        void Update(SceneContext& ctx, SceneUpdatePhase phase) override;

        void Finalize(SceneContext& ctx) override;

        /// @brief シーン側から既定床を抑止する（独自の地形・水面を持つシーン用）
        /// @note シーンの `OnInitialize()` から呼ぶこと（床の生成前でなければ効かない）。
        void SetSuppressed(bool suppressed) { suppressed_ = suppressed; }
        bool IsSuppressed() const { return suppressed_; }

        /// @brief 生成された床オブジェクト（抑止時・生成前は nullptr）
        GameObject* GetGroundObject() const { return ground_; }

#ifdef USE_IMGUI
        /// @brief Engine Settings に「Ground」パネルを登録する（プロセスで一度だけ）
        /// @details ドロワーはファイルスコープの「現在アクティブな床」を読むだけで何も
        ///          キャプチャしない（GameDebugUI に登録解除 API が無いため）。
        static void EnsureSettingsPanelRegistered(EngineSystem* engine);

        /// @brief この床をパネルの編集対象にする（nullptr で解除）
        static void SetActiveForSettingsPanel(GroundFeature* ground);

        /// @brief 設定パネルの中身を描画する
        void DrawSettingsImGui();
#endif

    private:
        /// @brief 床オブジェクト一式（メッシュ・マテリアル・コライダー）を生成する
        void CreateGroundObject(SceneContext& ctx);

        /// @brief カメラのファークリップと CVar から床の半幅 [m] を決める
        static float ComputeHalfSize(const Camera* camera);

        /// @brief 位置（カメラ追従）・スケール・コライダー厚みを反映する
        void UpdateTransform(SceneContext& ctx);

        /// @brief CVar が変わっていればマテリアル（色・粗さ・チェッカー）を作り直す
        void SyncMaterial(SceneContext& ctx);

        /// @brief 大気の地表 Y 座標（床を置く高さ）。大気が無ければ 0
        static float ResolveGroundLevelY(SceneContext& ctx);

        /// 床（所有権は GameObjectManager。Finalize ではポインタを切るだけ）
        GameObject* ground_ = nullptr;
        MeshRendererComponent* mesh_ = nullptr;
        MaterialComponent* material_ = nullptr;
        Collider* collider_ = nullptr;

        /// シーン側の抑止フラグ（CVar r.Ground.Enable とは独立）
        bool suppressed_ = false;

        /// 現在メッシュに焼き込まれている UV タイル数（変化したら作り直す）
        float meshUvTiling_ = 0.0f;

        /// 現在メッシュに焼き込まれている分割数（変化したら作り直す）
        uint32_t meshSubdivisions_ = 0;

        /// 現在テクスチャに反映済みのチェッカー設定（-1 = 未適用。初回は必ず適用する）
        int appliedChecker_ = -1;

        /// 直近に適用した床の半幅 [m]（設定パネルの状態表示用）
        float currentHalfSize_ = 0.0f;

        /// 現在の床中心の XZ。カメラがここから十分離れるまで置き直さない
        /// （毎フレームのスナップは床の瞬間移動＝偽のモーションベクタになるため）
        Vector2 groundCenterXZ_{ 0.0f, 0.0f };
        bool recentered_ = false;
    };
}
