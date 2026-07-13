#pragma once

#include "IScene.h"
#include "Graphics/Light/LightData.h"
#include "GameObject/GameObjectManager.h"
#include "Audio/SoundManager.h"
#include "Collider/CollisionManager.h"
#include "Collider/CollisionConfig.h"
#include <memory>

#include "Scene/SceneSaveSystem.h"

#ifdef USE_IMGUI
#include "Editor/Scene/SceneDebugEditor.h"
#endif

// 前方宣言
class SkyBoxObject;
class InfiniteGroundObject;

namespace CoreEngine {
    class EngineSystem;
    class CameraManager;
    class DirectXCommon;
    class RenderManager;
    class GridRenderer;
    class ResourceFactory;
    class IParticleSystem;
    enum class ParticleBackend;
}

/// @brief シーンの基底クラス（共通処理を実装）

namespace CoreEngine
{
    class BaseScene : public IScene {
    public:

        virtual ~BaseScene() = default;

        /// @brief 初期化（共通処理 + OnInitialize() + LoadObjectsFromJson() を自動実行）
        /// @note 派生クラスは Initialize() ではなく OnInitialize() をオーバーライドしてください
        void Initialize(CoreEngine::EngineSystem* engine) override final;

        /// @brief 更新（共通処理 + 派生クラスの更新）
        /// @note このメソッドはfinalです。派生クラスはOnUpdate()をオーバーライドしてください
        virtual void Update() override final;

        /// @brief 描画処理（共通処理 + 派生クラスの描画）
        virtual void PrepareRender() override;

        /// @brief 描画処理（共通処理 + 派生クラスの描画）
        virtual void Draw() override;

        /// @brief 解放（共通処理 + 派生クラスの解放）
        virtual void Finalize() override;

        /// @brief Gameビュー用3Dカメラを取得
        ICamera* GetGameViewCamera3D() const override;

        /// @brief 既定の Gameビュー用3Dカメラを取得
        ICamera* GetDefaultGameViewCamera3D() const override;

        /// @brief Gameビュー用2Dカメラを取得
        ICamera* GetGameViewCamera2D() const override;

        /// @brief 現在のゲームオブジェクトマネージャーを取得
        GameObjectManager* GetGameObjectManager() override { return &gameObjectManager_; }

    protected:
        /// @brief 派生クラスでオーバーライドするシーン固有の初期化処理
        /// @note SetSceneName() と全 CreateObject() をここで行う。
        ///       完了後に LoadObjectsFromJson() が自動的に呼ばれる。
        virtual void OnInitialize() {}

        /// @brief 派生クラスでオーバーライドする更新処理（GameObjectの更新前）
        virtual void OnUpdate() {}

        /// @brief 派生クラスでオーバーライドする後処理（GameObjectの更新後、クリーンアップ前）
        virtual void OnLateUpdate() {}

        /// @brief 既定の無限地面（y=0 のグレータイル床）を自動生成するかどうか
        /// @return true で自動生成（既定）。床が不要／邪魔になるシーンだけ false を返す。
        virtual bool WantsDefaultGround() const { return true; }

        /// @brief 既定の GameView カメラ（"Release"）の位置・回転を上書きする
        /// @param translate ワールド座標（無限床より上＝y > 0 にすること）
        /// @param rotate    オイラー角（ラジアン）
        /// @note OnInitialize() から呼ぶ。シーン固有の構図に合わせて使う。
        void SetReleaseCameraTransform(const Vector3& translate, const Vector3& rotate = { 0.0f, 0.0f, 0.0f });

        /// 既定 GameView カメラの高さ（無限床 y=0 より上）
        static constexpr float kDefaultCameraHeight = 3.0f;

        /// @brief 大気散乱シーンでサーフェスの直接光に使う太陽強度
        /// @details 空の輝度スケール（DirectionalLightData::atmosphereIntensity ≒ 20）とは単位系が別。
        ///          この値を空と同じ 20 にすると、明るいアルベド（既定床のタイルなど）が
        ///          ACES の飽和域（HDR >= 7.24）へ入り階調が全て 1.0 に潰れる。
        ///
        ///          値の根拠: 既定シーン（太陽が真下・intensity=1 → NdotL=1）と既定床の見た目を
        ///          揃えるため、大気シーンの太陽高度 35°（NdotL≈0.574）を打ち消す 1/0.574 ≈ 1.75 とする。
        static constexpr float kAtmosphereSurfaceSunIntensity = 1.75f;

    private:

        /// @brief カメラのセットアップ
        void SetupCamera();

        /// @brief 指定カメラでジオメトリ描画
        void DrawWithCamera(const std::string& cameraName);

        /// @brief Gameビューに使用する3Dカメラ名を解決
        std::string ResolveGameViewCameraName() const;

        /// @brief ライトのセットアップ
        void SetupLight();

        /// @brief 既定の空（大気散乱モードの SkyBox）のセットアップ
        /// @details OnInitialize() 完了後に呼ばれる。シーンが SkyBox を生成済みの場合は
        ///          それを採用し、未生成の場合のみ大気散乱モードの SkyBox を自動生成する。
        void SetupDefaultSky();

        /// @brief 既定の無限地面（y=0 のグレータイル床）のセットアップ
        /// @details OnInitialize() 完了後に呼ばれる。シーンが InfiniteGroundObject を
        ///          生成済みならそれを採用し、未生成かつ WantsDefaultGround()==true の
        ///          場合のみ自動生成する。
        void SetupDefaultGround();

        /// @brief 既定の無限地面をカメラ XZ に追従させる（毎フレーム）
        void UpdateGroundPlane();

        /// @brief 大気散乱システムの毎フレーム更新
        /// @details SkyBox が大気散乱モードの場合のみ AtmosphereManager へ太陽情報と
        ///          カメラ情報を反映する（LUT 生成・Aerial Perspective の有効化トリガ）。
        void UpdateAtmosphere();

        /// @brief シャドウマップ用のライトView-Projection行列を更新
        void UpdateLightViewProjection();

#ifdef USE_IMGUI
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

#ifdef USE_IMGUI
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

        /// @brief パーティクルシステムを生成する（CPU/GPUバックエンドの統一入口）
        /// @param backend ParticleBackend::CPU（ParticleSystem）/ GPU（GpuParticleSystem）
        /// @param name オブジェクト名（ImGui表示用、省略可）
        /// @return 共通インターフェース（モジュール編集・再生制御・プリセットは同じAPI）
        /// @note DirectXCommon / ResourceFactory は engine_ から自動取得して Initialize まで行う
        IParticleSystem* CreateParticleSystem(ParticleBackend backend, const std::string& name = "");

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
        /// @note Initialize() から自動的に呼ばれる。手動で再ロードが必要な場合にも使用可能。
        void LoadObjectsFromJson();

        /// @brief シーン名を取得
        const std::string& GetSceneName() const { return sceneSaveSystem_->GetSceneName(); }

        /// @brief シーンのオブジェクトデータを JSON に保存（手動呼び出し）
        void SaveObjectsToJson();

        /// @brief 単一オブジェクトのデータだけを JSON に上書き保存（手動呼び出し）
        /// @param obj 保存対象のオブジェクト
        void SaveSingleObjectToJson(GameObject* obj);

        /// @brief シーンの SkyBox を取得（既定で大気散乱モード。SetTexture() でキューブマップへ切替）
        SkyBoxObject* GetSkyBox() const { return skyBox_; }

        /// @brief シーンBGMを登録し、トランジション時の自動フェードを有効化
        /// @param bgm BGMのSoundResourceポインタ（現在のSetVolume()で設定した音量が使用されます）
        void RegisterSceneBGM(std::unique_ptr<SoundManager::SoundResource>* bgm);

    private:
        // BGM管理用
        std::unique_ptr<SoundManager::SoundResource>* sceneBGM_ = nullptr;
        float baseBGMVolume_ = 1.0f;

        // 既定背景の SkyBox（所有権は gameObjectManager_。Finalize でポインタをクリアする）
        SkyBoxObject* skyBox_ = nullptr;

        // 既定の無限地面（所有権は gameObjectManager_。Finalize でポインタをクリアする）
        InfiniteGroundObject* groundPlane_ = nullptr;

        // シーン保存/読み込み
        std::unique_ptr<SceneSaveSystem> sceneSaveSystem_;

#ifdef USE_IMGUI
        std::unique_ptr<SceneDebugEditor> debugEditor_;  // Undo/Redo・デバッグ編集機能
#endif

        std::string gameViewCameraName_ = "Release";
    };
}
