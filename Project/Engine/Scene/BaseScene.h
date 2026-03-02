#pragma once

#include "IScene.h"
#include "Engine/Graphics/Light/LightData.h"
#include "ObjectCommon/GameObjectManager.h"
#include "Engine/Audio/SoundManager.h"
#include "Engine/Collider/CollisionManager.h"
#include "Engine/Collider/CollisionConfig.h"
#include <memory>

#include "Engine/Scene/SceneSaveSystem.h"

#ifdef _DEBUG
#include "Engine/Scene/SceneDebugEditor.h"
#endif

// 前方宣言
namespace CoreEngine {
    class EngineSystem;
    class CameraManager;
    class DirectXCommon;
    class RenderManager;
    class GridRenderer;
}

/// @brief シーンの基底クラス（共通処理を実装）

namespace CoreEngine
{
    class BaseScene : public IScene {
    public:

        virtual ~BaseScene() = default;

        /// @brief 初期化（共通処理 + 派生クラスの初期化）
        virtual void Initialize(CoreEngine::EngineSystem* engine) override;

        /// @brief 更新（共通処理 + 派生クラスの更新）
        /// @note このメソッドはfinalです。派生クラスはOnUpdate()をオーバーライドしてください
        virtual void Update() override final;

        /// @brief 描画処理（共通処理 + 派生クラスの描画）
        virtual void Draw() override;

        /// @brief 解放（共通処理 + 派生クラスの解放）
        virtual void Finalize() override;

    protected:
        /// @brief 派生クラスでオーバーライドする更新処理（GameObjectの更新前）
        virtual void OnUpdate() {}

        /// @brief 派生クラスでオーバーライドする後処理（GameObjectの更新後、クリーンアップ前）
        virtual void OnLateUpdate() {}

    private:

        /// @brief カメラのセットアップ
        void SetupCamera();

        /// @brief ライトのセットアップ
        void SetupLight();

        /// @brief シャドウマップ用のライトView-Projection行列を更新
        void UpdateLightViewProjection();

#ifdef _DEBUG
        /// @brief グリッドのセットアップ（デバッグビルドのみ）
        void SetupGrid();
#endif

    protected:
        // 派生クラスからアクセス可能な共通メンバー
        EngineSystem* engine_ = nullptr;
        std::unique_ptr<CameraManager> cameraManager_;
        DirectionalLightData* directionalLight_ = nullptr;

        // シャドウマップ設定（派生クラスで調整可能）
        static constexpr float kShadowLightDistance = 50.0f;  // ライトの距離
        static constexpr float kShadowOrthoSize = 50.0f;      // 正射影範囲
        static constexpr float kShadowNearPlane = 0.1f;       // 近平面
        static constexpr float kShadowFarPlane = 100.0f;      // 遠平面

        // ゲームオブジェクト管理（新システム）
        GameObjectManager gameObjectManager_;

        // コリジョン管理
        CollisionConfig collisionConfig_;
        CollisionManager collisionManager_{ &collisionConfig_ };

#ifdef _DEBUG
        // グリッドレンダラー（デバッグビルドのみ）
        GridRenderer* gridRenderer_ = nullptr;
#endif

        // === 派生クラス用ヘルパーメソッド ===

        /// @brief GameObjectを生成して登録
        /// @tparam T GameObjectの派生クラス
        /// @tparam Args コンストラクタ引数の型
        /// @param args コンストラクタ引数
        /// @return 生成されたオブジェクトへのポインタ
        template<typename T, typename... Args>
        T* CreateObject(Args&&... args) {
            auto obj = std::make_unique<T>(std::forward<Args>(args)...);
            return gameObjectManager_.AddObject(std::move(obj));
        }

        /// @brief レイヤー間の衝突判定を有効/無効に設定
        /// @param a レイヤーA
        /// @param b レイヤーB
        /// @param enable true:衝突判定有効 / false:衝突判定無効
        void SetCollisionEnabled(CollisionLayer a, CollisionLayer b, bool enable = true) {
            collisionConfig_.SetCollisionEnabled(a, b, enable);
        }

        /// @brief シーン名を設定（JSON ファイルパスに使用）
        /// @note 派生クラスの Initialize()内、BaseScene::Initialize() の後に呼ぶ
        void SetSceneName(const std::string& name) { sceneSaveSystem_->SetSceneName(name); }

        /// @brief シーンのオブジェクトデータを JSON から読み込んで登録済みオブジェクトに適用
        /// @note 派生クラスの Initialize()内、全 CreateObject() の後に呼ぶ
        void LoadObjectsFromJson();

        /// @brief シーン名を取得
        const std::string& GetSceneName() const { return sceneSaveSystem_->GetSceneName(); }

        /// @brief シーンのオブジェクトデータを JSON に保存（手動呼び出し）
        void SaveObjectsToJson();

        /// @brief 単一オブジェクトのデータだけを JSON に上書き保存（手動呼び出し）
        /// @param obj 保存対象のオブジェクト
        void SaveSingleObjectToJson(GameObject* obj);

        /// @brief シーンBGMを登録し、トランジション時の自動フェードを有効化
        /// @param bgm BGMのSoundResourceポインタ（現在のSetVolume()で設定した音量が使用されます）
        void RegisterSceneBGM(std::unique_ptr<SoundManager::SoundResource>* bgm);

    private:
        // BGM管理用
        std::unique_ptr<SoundManager::SoundResource>* sceneBGM_ = nullptr;
        float baseBGMVolume_ = 1.0f;

        // シーン保存/読み込み
        std::unique_ptr<SceneSaveSystem> sceneSaveSystem_;

#ifdef _DEBUG
        std::unique_ptr<SceneDebugEditor> debugEditor_;  // Undo/Redo・デバッグ編集機能
#endif
    };
}
