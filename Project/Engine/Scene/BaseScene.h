#pragma once

#include "IScene.h"
#include "Engine/Graphics/Light/LightData.h"
#include "ObjectCommon/GameObjectManager.h"
#include "Engine/Audio/SoundManager.h"
#include <memory>

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

        /// @brief シーンのBGMを登録し、トランジション時の自動フェードを有効化
        /// @param bgm BGMのSoundResourceポインタ（現在のSetVolume()で設定した音量が使用されます）
        void RegisterSceneBGM(std::unique_ptr<SoundManager::SoundResource>* bgm);

    private:
        // BGM管理用
        std::unique_ptr<SoundManager::SoundResource>* sceneBGM_ = nullptr;
        float baseBGMVolume_ = 1.0f;
    };
}
