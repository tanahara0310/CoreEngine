#pragma once

#include "IScene.h"
#include "Graphics/Light/Light.h"
#include "GameObject/GameObjectManager.h"
#include "Collision/CollisionConfig.h"
#include "Scene/Feature/ISceneFeature.h"
#include "UI/UIAnchor.h"
#include <memory>
#include <vector>

#include "Scene/SceneSaveSystem.h"

namespace CoreEngine {
    class EngineSystem;
    class Camera;
    class CameraManager;
    class GraphicsCore;
    class RenderManager;
    class CollisionWorld;
    class UIText;
}

namespace CoreEngine
{
    /// @brief シーンの基底クラス（共通処理を実装）
    class BaseScene : public IScene {
    public:

        virtual ~BaseScene() = default;

        /// @brief 初期化をステップ列へ積む（シーン構築の唯一の入口）
        /// @details 派生クラスは OnInitialize() を、生成が重いシーンは
        ///          BuildContentLoadTasks() をオーバーライドすること。
        void BuildLoadTasks(StartupSequence& sequence, EngineSystem* engine) override final;

        /// @brief 更新（共通処理 + 派生クラスの更新）
        /// @note このメソッドはfinalです。派生クラスはOnUpdate()をオーバーライドしてください
        virtual void Update(SceneUpdateMode mode) override final;

        /// @brief 描画キューの構築（全 GameObject を RenderManager へ登録する）
        virtual void PrepareRender() override;

        /// @brief 解放（共通処理 + 派生クラスの解放）
        /// @note このメソッドはfinalです。派生クラスはOnFinalize()をオーバーライドしてください
        virtual void Finalize() override final;

        /// @brief Gameビュー用3Dカメラを取得
        Camera* GetGameViewCamera3D() const override;

        /// @brief Gameビュー用2Dカメラを取得
        Camera* GetGameViewCamera2D() const override;

        /// @brief 現在のゲームオブジェクトマネージャーを取得
        GameObjectManager* GetGameObjectManager() override { return &gameObjectManager_; }

    protected:
        /// @brief 派生クラスでオーバーライドするシーン固有の初期化処理
        /// @note SetSceneName() と全 CreateObject() をここで行う。
        ///       完了後にシーン JSON からの復元が自動的に走る。
        virtual void OnInitialize() {}

        /// @brief OnInitialize() をステップ列へ積む
        /// @details 既定は 1 ステップ。生成に時間がかかるシーンはここを分割する。
        virtual void BuildContentLoadTasks(StartupSequence& sequence);

        /// @brief 派生クラスでオーバーライドする更新処理（GameObjectの更新前）
        virtual void OnUpdate() {}

        /// @brief 派生クラスでオーバーライドする後処理（GameObjectの更新後、クリーンアップ前）
        virtual void OnLateUpdate() {}

        /// @brief 派生クラスでオーバーライドするシーン固有の解放処理
        /// @note Feature の解放・GameObject のクリアより前に呼ばれる
        virtual void OnFinalize() {}

        /// @brief ゲーム視点カメラ（CameraNames::Game）の位置・回転を上書きする
        /// @param translate ワールド座標（地表 y=0 より上＝y > 0 にすること）
        /// @param rotate    オイラー角（ラジアン）
        /// @note OnInitialize() から呼ぶ。シーン固有の構図に合わせて使う。
        void SetReleaseCameraTransform(const Vector3& translate, const Vector3& rotate = { 0.0f, 0.0f, 0.0f });

        /// @brief ゲーム視点カメラ（CameraNames::Game）のレンズを上書きする
        /// @param fovDegrees 垂直画角 [度]（既定 0.45rad ≒ 25.8° は望遠寄りで風景には狭い）
        /// @param farClip    ファークリップ [m]（水平線まで見せるなら水面メッシュの端まで届く値にする）
        /// @param nearClip   ニアクリップ [m]
        /// @note OnInitialize() から呼ぶ。位置・回転と対で構図を決めるための入口。
        void SetReleaseCameraLens(float fovDegrees, float farClip, float nearClip = 0.1f);

        /// @brief 大気散乱シーンでサーフェスの直接光に使う太陽照度 [lx]
        /// @details 快晴の太陽直下照度（LightUnits::kSunIlluminanceLux）。空の輝度スケール
        ///          （Light::atmosphereIntensity ≒ 20）とは単位系が別で、シェーダー単位では
        ///          較正定数により旧 kAtmosphereSurfaceSunIntensity=1.75 と同値になる
        ///          （明るいアルベドが ACES の飽和域へ入らない既存チューニングを維持）。
        static constexpr float kAtmosphereSunIlluminanceLux = LightUnits::kSunIlluminanceLux;

    private:

        /// @brief エンジン参照・保存システム・既定 Feature の登録
        void SetupSceneCore(EngineSystem* engine);

        /// @brief 全 Feature の Initialize と、既定ライト・カメラの公開
        void InitializeFeatures();

        /// @brief シーン JSON が参照するモデルの並列先読みを開始する
        void BeginModelPreload();

        /// @brief 全 Feature の PostSceneInitialize
        void RunPostSceneInitialize();

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

        /// @brief シーン横断機能（Feature）を追加登録する（所有権は BaseScene へ移動）
        /// @details 各フックは priority 昇順・同 priority は登録順で呼ばれる。
        ///          シーン初期化後に追加した場合は即座に Initialize が実行される。
        /// @note エンジン機能のシーン組み込みは BaseScene を編集せず Feature の追加で行うこと
        ISceneFeature* AddFeature(std::unique_ptr<ISceneFeature> feature, int priority = 0);

        /// @brief 登録済みの Feature を型で引く（既定 Feature・追加 Feature のどちらも引ける）
        /// @tparam T ISceneFeature の派生型。呼び出し側の .cpp で完全型であればよい
        /// @return 最初に見つかった T。未登録なら nullptr
        /// @details BaseScene が既定 Feature ごとに専用メンバーと委譲メソッドを抱えると、
        ///          Feature を 1 つ増やすたびに BaseScene の編集が必要になる。
        ///          この 1 本があれば BaseScene は Feature の型を知らなくて済む。
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

        /// @brief 空の GameObject を生成して登録する（コンポーネント化の標準的な入口）
        /// @param name オブジェクト名（Hierarchy 表示・シーン保存のキー）
        /// @note 機能はここから `AddComponent<T>()` で載せる。専用クラスは要らない。
        GameObject* CreateObject(const std::string& name);

        /// @brief 特定の派生クラスを生成して登録する（レガシー経路）
        /// @tparam T GameObjectの派生クラス
        /// @note 新しいコードは `CreateObject(name)` + `AddComponent<T>()` を使うこと。
        ///       これは水面など、まだコンポーネント化していないクラス専用。
        template<typename T, typename... Args>
        T* CreateObject(Args&&... args) {
            auto obj = std::make_unique<T>(std::forward<Args>(args)...);
            return gameObjectManager_.AddObject(std::move(obj));
        }

        /// @brief HUD 用のテキストを生成して登録する
        /// @param textUtf8 表示文字列（UTF-8。改行コードに対応）
        /// @param fontSize フォントサイズ（px）
        /// @param anchor 画面のどこを基準に置くか
        /// @param anchoredPos アンカーからのオフセット（px。Y は下方向が正）
        /// @param color 文字色
        /// @param name オブジェクト名（Hierarchy 表示用。省略時は自動採番）
        /// @return 生成された UIText。以降の見た目調整は戻り値から行う
        /// @details フォント指定は不要。FontManager の既定フォント（和文＋ASCII、
        ///          未収録文字は実行時ベイク）が自動で使われる。
        ///          別のフォントにしたい場合は戻り値へ `SetFontByName()` を呼ぶこと。
        UIText* CreateText(const std::string& textUtf8,
            float fontSize = 32.0f,
            UIAnchor anchor = UIAnchor::TopLeft,
            const Vector2& anchoredPos = { 0.0f, 0.0f },
            const Vector4& color = { 1.0f, 1.0f, 1.0f, 1.0f },
            const std::string& name = "");

        /// @brief レイヤー間の衝突判定を有効/無効に設定
        /// @param a レイヤーA
        /// @param b レイヤーB
        /// @param enable true:衝突判定有効 / false:衝突判定無効
        void SetCollisionEnabled(CollisionLayer a, CollisionLayer b, bool enable = true);

        /// @brief 既定ディレクショナルライト（太陽）を取得する
        /// @return Feature 未登録・ライト削除済みなら nullptr
        /// @details LightManager は世代付きハンドルでライトを管理しており、削除されると
        ///          世代が進んで参照が無効化される。呼ぶたびに引き直すこと。
        ///          戻り値を保持すると、その無効化をすり抜けて
        ///          「削除済みスロット」や「再利用された別のライト」を掴んだままになる。
        Light* GetDirectionalLight() const;

        /// @brief 衝突ワールドを取得する（レイキャスト・オーバーラップの問い合わせ用）
        /// @return Feature 未登録なら nullptr
        /// @note 問い合わせは直近の判定フェーズ（PostObjectUpdate）時点の登録内容を見る。
        ///       OnUpdate から呼ぶと 1 フレーム前の状態になる。
        CollisionWorld* GetCollisionWorld();

        /// @brief シーン名を設定（JSON ファイルパスに使用）
        /// @note OnInitialize() の先頭で呼ぶこと（シーン JSON の復元先を決めるため）
        void SetSceneName(const std::string& name) { sceneSaveSystem_->SetSceneName(name); }

        /// @brief シーン名を取得
        const std::string& GetSceneName() const { return sceneSaveSystem_->GetSceneName(); }

        /// @brief 既定の床（GroundFeature が作るベース地面）を使うかどうかを設定する
        /// @param enabled false にすると床オブジェクトを生成しない
        /// @note **OnInitialize() から呼ぶこと**（床の生成は OnInitialize 完了直後のため）。
        ///       独自の地形や水面を y=0 付近に持つシーンで、二重の床になるのを避けるために使う。
        ///       全シーン一律の ON/OFF は CVar "r.Ground.Enable" 側。
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

        // シーン保存/読み込み
        std::unique_ptr<SceneSaveSystem> sceneSaveSystem_;
    };
}
