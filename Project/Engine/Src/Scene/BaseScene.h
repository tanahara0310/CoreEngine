#pragma once

#include "IScene.h"
#include "Graphics/Light/Light.h"
#include "GameObject/GameObjectManager.h"
#include "Audio/SoundManager.h"
#include "Collider/CollisionConfig.h"
#include "Scene/Feature/ISceneFeature.h"
#include <memory>
#include <vector>

#include "Scene/SceneSaveSystem.h"

#ifdef USE_IMGUI
#include "Camera/Debug/DebugCameraSettingsSection.h"
#endif

// 前方宣言
class SkyBoxObject;

namespace CoreEngine {
    class EngineSystem;
    class CameraManager;
    class DirectXCommon;
    class RenderManager;
    class ResourceFactory;
    class IParticleSystem;
    class LightingFeature;
    class EnvironmentFeature;
    class CollisionFeature;
    class SceneBGMFeature;
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
        /// @note このメソッドはfinalです。派生クラスはOnFinalize()をオーバーライドしてください
        virtual void Finalize() override final;

        /// @brief Gameビュー用3Dカメラを取得
        Camera* GetGameViewCamera3D() const override;

        /// @brief 既定の Gameビュー用3Dカメラを取得
        Camera* GetDefaultGameViewCamera3D() const override;

        /// @brief Gameビュー用2Dカメラを取得
        Camera* GetGameViewCamera2D() const override;

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

        /// @brief 派生クラスでオーバーライドするシーン固有の解放処理
        /// @note Feature の解放・GameObject のクリアより前に呼ばれる
        virtual void OnFinalize() {}

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

        /// @brief 大気散乱シーンでサーフェスの直接光に使う太陽照度 [lx]
        /// @details 快晴の太陽直下照度（LightUnits::kSunIlluminanceLux）。空の輝度スケール
        ///          （Light::atmosphereIntensity ≒ 20）とは単位系が別で、シェーダー単位では
        ///          較正定数により旧 kAtmosphereSurfaceSunIntensity=1.75 と同値になる
        ///          （明るいアルベドが ACES の飽和域へ入らない既存チューニングを維持）。
        static constexpr float kAtmosphereSunIlluminanceLux = LightUnits::kSunIlluminanceLux;

    private:

        /// @brief カメラのセットアップ
        void SetupCamera();

        /// @brief Gameビューに使用する3Dカメラ名を解決
        std::string ResolveGameViewCameraName() const;

        /// @brief 既定 Feature（ライト・グリッド・デバッグエディタ・コリジョン・環境・BGM）を登録
        void RegisterDefaultFeatures();

        /// @brief Feature へ渡すコンテキストを最新化（gameViewCamera3D の再解決）
        void RefreshFeatureContext();

        /// @brief 全 Feature の Update を指定フェーズでディスパッチ
        void DispatchUpdate(SceneUpdatePhase phase);

    protected:
        // 派生クラスからアクセス可能な共通メンバー
        EngineSystem* engine_ = nullptr;
        std::unique_ptr<CameraManager> cameraManager_;
        Light* directionalLight_ = nullptr;

        // ゲームオブジェクト管理（新システム）
        GameObjectManager gameObjectManager_;

        // === 派生クラス用ヘルパーメソッド ===

        /// @brief シーン横断機能（Feature）を追加登録する
        /// @details 各フックは priority 昇順（小さいほど先）・同 priority は登録順で呼ばれる。
        ///          シーン初期化後（OnInitialize() 内など）に追加した場合は即座に
        ///          Initialize が実行される。エンジン機能のシーン組み込みは
        ///          BaseScene を編集せず Feature の追加で行うこと。
        /// @param feature 登録する Feature（所有権は BaseScene へ移動）
        /// @param priority フェーズ内優先度
        /// @return 登録した Feature へのポインタ
        ISceneFeature* AddFeature(std::unique_ptr<ISceneFeature> feature, int priority = 0);

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
        void SetCollisionEnabled(CollisionLayer a, CollisionLayer b, bool enable = true);

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

        /// @brief シーンの SkyBox（大気散乱で描く空）を取得
        SkyBoxObject* GetSkyBox() const;

        /// @brief シーンBGMを登録し、トランジション時の自動フェードを有効化
        /// @param bgm BGMのSoundResourceポインタ（現在のSetVolume()で設定した音量が使用されます）
        void RegisterSceneBGM(std::unique_ptr<SoundManager::SoundResource>* bgm);

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

        // 既定 Feature への型付き参照（所有権は features_。派生シーン互換 API の委譲先）
        LightingFeature* lightingFeature_ = nullptr;
        EnvironmentFeature* environmentFeature_ = nullptr;
        CollisionFeature* collisionFeature_ = nullptr;
        SceneBGMFeature* bgmFeature_ = nullptr;

        // シーン保存/読み込み
        std::unique_ptr<SceneSaveSystem> sceneSaveSystem_;

#ifdef USE_IMGUI
        // デバッグカメラのエディタ設定自動保存セクション
        // （登録は SetupCamera、解除は Finalize。対象カメラは cameraManager_ が所有）
        std::unique_ptr<DebugCameraSettingsSection> debugCameraSettingsSection_;
#endif

        std::string gameViewCameraName_ = "Release";
    };
}
