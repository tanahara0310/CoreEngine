#pragma once

#include "IScene.h"
#include "GameObject/GameObjectManager.h"
#include "Collision/CollisionConfig.h"
#include "Scene/Feature/ISceneFeature.h"
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Scene/SceneSaveSystem.h"

namespace CoreEngine {
    class EngineSystem;
    class Camera;
    class CameraManager;
    class GraphicsCore;
    class RenderManager;
    class CollisionWorld;
}

namespace CoreEngine
{
    /// @brief シーン 1 つ分
    /// @details 中身は保存データ（`Application/Assets/Scenes/<名前>/`）が決める。
    ///          動きはオブジェクトに付けたコンポーネント（スクリプトを含む）が受け持つ。
    ///          エンジンの機能はシーンではなく Feature（`ISceneFeature`）として足す。
    class Scene final : public IScene {
    public:
        /// @param sceneName シーン名。保存データのフォルダ名にもなる
        explicit Scene(std::string sceneName);

        ~Scene() override = default;

        /// @brief 初期化をステップ列へ積む（シーン構築の唯一の入口）
        void BuildLoadTasks(StartupSequence& sequence, EngineSystem* engine) override final;

        /// @brief 更新（Feature とゲームオブジェクト）
        virtual void Update(SceneUpdateMode mode) override final;

        /// @brief 描画キューの構築（全 GameObject を RenderManager へ登録する）
        virtual void PrepareRender() override;

        /// @brief 解放（Feature とゲームオブジェクト）
        virtual void Finalize() override final;

        /// @brief Gameビュー用3Dカメラを取得
        Camera* GetGameViewCamera3D() const override;

        /// @brief ゲーム視点の3Dカメラを取得（エディタ視点で覗いていても変わらない）
        Camera* GetGameCamera3D() const override;

        /// @brief Gameビュー用2Dカメラを取得
        Camera* GetGameViewCamera2D() const override;

        /// @brief 現在のゲームオブジェクトマネージャーを取得
        GameObjectManager* GetGameObjectManager() override { return &gameObjectManager_; }

        /// @brief オブジェクトの値を、保存ファイルではなくメモリの控えから戻すようにする
        void SetRestoreSnapshot(std::shared_ptr<const SceneSnapshot> snapshot) override { restoreSnapshot_ = std::move(snapshot); }

        /// @brief 今のシーンの設定（足した Feature・既定の床・衝突マトリクス）をマニフェストへ書く
        /// @note エディタがシーンを保存するときに呼ぶ。
        void SaveSceneSettings();

    private:

        /// @brief エンジン参照・保存システム・既定 Feature の登録
        void SetupSceneCore(EngineSystem* engine);

        /// @brief 全 Feature の Initialize と、既定ライト・カメラの公開
        void InitializeFeatures();

        /// @brief シーン JSON が参照するモデルの並列先読みを開始する
        void BeginModelPreload();

        /// @brief 全 Feature の PostSceneInitialize（シーンのオブジェクトが出そろった後）
        void RunPostSceneInitialize();

        /// @brief マニフェストのシーンの設定を当てる（シーンのコードより後＝保存した値が勝つ）
        void ApplyManifestSettings();

        /// @brief 保存データの衝突マトリクスを当てる（書かれている組み合わせだけを有効にする）
        void ApplyCollisionPairs(const std::vector<std::pair<std::string, std::string>>& pairs);

        /// @brief 今のシーンの設定を集める
        SceneSaveSystem::ManifestSettings CollectManifestSettings() const;

        /// @brief JSON からのシーン復元を開始する（1 体ずつフレームを跨いで進める）
        void BeginSceneDataRestore();

        /// @brief 既定 Feature を登録する（顔ぶれは CreateDefaultSceneFeatures() 側）
        void RegisterDefaultFeatures();

        /// @brief Feature へ渡すコンテキストを最新化（gameViewCamera3D の再解決）
        void RefreshFeatureContext();

        /// @brief 全 Feature の Update を指定フェーズでディスパッチ
        /// @param stopped 進行を止めているか（RunsWhileStopped() の Feature だけを回す）
        void DispatchUpdate(SceneUpdatePhase phase, bool stopped);

    protected:
        // 派生クラスからアクセス可能な共通メンバー
        EngineSystem* engine_ = nullptr;

        // カメラ一式の所有は CameraFeature。ここはホットパス用の非所有キャッシュで、
        // Feature の Initialize 後に解決され、Finalize で無効化される
        CameraManager* cameraManager_ = nullptr;

        // ゲームオブジェクト管理（新システム）
        GameObjectManager gameObjectManager_;

        // === 派生クラス用ヘルパーメソッド ===

        /// @brief シーン横断機能（Feature）を追加登録する（所有権は Scene へ移動）
        /// @details 各フックは priority 昇順・同 priority は登録順で呼ばれる。
        ///          シーン初期化後に追加した場合は即座に Initialize が実行される。
        /// @note エンジン機能のシーン組み込みは Scene を編集せず Feature の追加で行うこと
        ISceneFeature* AddFeature(std::unique_ptr<ISceneFeature> feature, int priority = 0);

    public:
        /// @brief 登録済みの Feature を型で引く（既定 Feature・追加 Feature のどちらも引ける）
        /// @tparam T ISceneFeature の派生型。呼び出し側の .cpp で完全型であればよい
        /// @return 最初に見つかった T。未登録なら nullptr
        /// @details Scene が既定 Feature ごとに専用メンバーと委譲メソッドを抱えると、
        ///          Feature を 1 つ増やすたびに Scene の編集が必要になる。
        ///          この 1 本があれば Scene は Feature の型を知らなくて済む。
        /// @note 同じ型を複数登録した場合は登録順で最初のものが返る。
        template<typename T>
        T* GetFeature() const {
            for (const auto& entry : features_) {
                if (auto* typed = dynamic_cast<T*>(entry.feature.get())) {
                    return typed;
                }
            }
            return nullptr;
        }

        /// @brief 登録済みの Feature の名前（呼ばれる順）
        std::vector<const char*> GetFeatureNames() const;

        /// @brief 登録済みの Feature を名前（`ISceneFeature::GetName()`）で引く
        /// @return 最初に見つかったもの。無ければ nullptr
        /// @note 型が分からない経路（保存データの `features`）が、二重に足していないかを見るために使う。
        ISceneFeature* FindFeature(std::string_view name) const;

    protected:
        /// @brief シーン名を取得
        const std::string& GetSceneName() const { return sceneSaveSystem_->GetSceneName(); }

        /// @brief 既定の床（GroundFeature が作るベース地面）を使うかどうかを設定する
        /// @param enabled false にすると床オブジェクトを生成しない
        /// @note マニフェストの `defaultGround` から呼ばれる。独自の地形や水面を y=0 付近に
        ///       持つシーンで、二重の床になるのを避けるために使う。
        ///       全シーン一律の ON/OFF は CVar "r.Ground.Enabled" 側。
        void SetDefaultGroundEnabled(bool enabled);

    private:
        /// @brief Feature の登録エントリ（priority 昇順・同 priority は登録順でソート済み）
        struct FeatureEntry {
            std::unique_ptr<ISceneFeature> feature;
            int priority = 0;
            uint64_t sequence = 0; ///< 登録順（同 priority の安定ソート用）
        };

        std::vector<FeatureEntry> features_;
        uint64_t featureSequence_ = 0;
        bool featuresInitialized_ = false;
        SceneContext featureContext_{};

        // シーンの名前（保存データのフォルダ名）
        std::string sceneName_;

        // シーン保存/読み込み
        std::unique_ptr<SceneSaveSystem> sceneSaveSystem_;

        // 保存ファイルの代わりに読む控え（空ならファイルから読む）
        std::shared_ptr<const SceneSnapshot> restoreSnapshot_;
    };
}
