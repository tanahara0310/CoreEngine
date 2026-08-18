#pragma once
#include <functional>
#include <memory>
#include <vector>
#include <typeindex>

#include "ComponentManager.h"
#include "EngineConfig.h"
#include "Subsystem/IEngineSubsystem.h"

#ifdef USE_IMGUI
#include "Subsystem/DebugSubsystem.h"
#endif

class WinApp;

// ──────────────────────────────────────────────────────────
// サービスアクセス利便インクルード
// GetService<T>() の呼び出し元が完全型を必要とするため、
// 主要サービス型のヘッダをここでまとめて提供している。
// 非推奨: 各呼び出し元ファイルで必要な型を直接インクルードすることを推奨。
// ──────────────────────────────────────────────────────────
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Texture/TextureManager.h"
#include "Input/InputManager.h"
#include "Audio/SoundManager.h"
#include "Utility/FrameRate/FrameRateController.h"


/// @brief エンジンシステム中核システム管理クラス

namespace CoreEngine
{
class SceneManager;
class GraphicsComponentFactory;
class CoreComponentFactory;
class RayTracingSubsystem;
class RenderPipeline;
class RenderingTechniqueManager;
class RenderDomainContext;
class HiZOcclusionSystem;
class StartupSequence;

class EngineSystem {
public:
    EngineSystem(); // 前方宣言型の unique_ptr メンバがあるため .cpp で定義する
    ~EngineSystem();

    /// @brief エンジンの初期化処理を起動シーケンスへステップとして積む
    /// @param sequence          積み先の起動シーケンス
    /// @param winApp            ウィンドウアプリケーション
    /// @param config            エンジン設定
    /// @param buildPreloadTasks アセット先読み用の差し込み口（省略可）
    /// @details 初期化を一息に実行すると、その間ウィンドウメッセージが 1 度も
    ///          処理されず「応答なし」になる。ステップに割っておくと、
    ///          呼び出し側が Step() の合間にメッセージ処理とローディング表示を挟める。
    /// @details buildPreloadTasks は ModelManager 生成直後（シェーダコンパイル群より前）に
    ///          呼ばれる。ゲーム側がここで非同期ロードを仕掛けると、
    ///          コンパイル時間の裏にアセットロードを隠せる。
    /// @warning ステップは全部積んでから回すこと（StartupSequence の warning 参照）。
    void BuildStartupTasks(
        StartupSequence& sequence,
        WinApp* winApp,
        const EngineConfig& config,
        const std::function<void(StartupSequence&)>& buildPreloadTasks = {});

    /// @brief エンジンシステムの初期化（一括実行）
    /// @param winApp ウィンドウアプリケーション
    /// @param config エンジン設定
    /// @note BuildStartupTasks を組み立てて最後まで回すだけの薄いラッパー。
    ///       Framework::Run はローディング表示のため自前でシーケンスを回すので通らない。
    void Initialize(WinApp* winApp, const EngineConfig& config);

    /// @brief エンジンシステムの終了処理
    void Finalize();

    /// @brief フレーム開始処理
    void BeginFrame();

    /// @brief フレーム終了処理
    void EndFrame();

    /// @brief 共通描画パイプライン - オフスクリーンレンダリングとポストエフェクトを自動処理
    void ExecuteRenderPipeline();

    /// @brief SceneManagerを設定
    void SetSceneManager(SceneManager* sceneManager);

    /// @brief SceneManagerを取得
    SceneManager* GetSceneManager() const;

    /// @brief WinAppを取得
    WinApp* GetWinApp() const { return winApp_; }

    /// @brief RenderDomainContextを取得
    RenderDomainContext* GetRenderDomainContext() { return renderDomainContext_.get(); }

    /// @brief Hi-Zオクルージョンカリングシステムを取得
    HiZOcclusionSystem* GetHiZOcclusionSystem() { return hiZOcclusionSystem_.get(); }

    // ──────────────────────────────────────────────────────────
    // コンポーネントアクセッサ
    // ──────────────────────────────────────────────────────────

    /// @brief エンジンサービスを取得（型安全）
    /// @tparam T サービスの型
    ///
    /// 代表的なサービス例:
    ///
    /// - DirectXCommon: DirectX12の基本機能
    ///
    /// - TextureManager: テクスチャ管理
    ///
    /// - ModelManager: 3Dモデル管理
    ///
    /// - InputManager: 入力管理（InputQuery経由でアクセス）
    ///
    /// - Audio / Light / FrameRate など
    ///
    /// @return サービスへのポインタ（登録されていない場合nullptr）
    /// @note **`GameObject::GetComponent<T>()` とは別物**。こちらはエンジン全体で
    ///       1 個ずつ存在する常駐サービスのロケータで、ゲームオブジェクトに
    ///       アタッチするコンポーネントとは無関係。混同を避けるため
    ///       `GetComponent` から `GetService` へ改名した（2026-08-07）。
    ///       サブシステム（`IEngineSubsystem` 派生）の取得は `GetSubsystem<T>()`。
    template<typename T>
    T* GetService() {
        return componentManager_.Get<T>();
    }

    /// @brief エンジンサービスが登録されているか確認
    /// @tparam T サービスの型（GetServiceと同じ型を指定可能）
    /// @return 登録されている場合true
    template<typename T>
    bool HasService() const {
        return componentManager_.Has<T>();
    }

#ifdef USE_IMGUI
    // ──────────────────────────────────────────────────────────
    // デバッグ機能アクセッサ（デバッグビルドのみ）
    // ──────────────────────────────────────────────────────────

    /// @brief デバッグサブシステムへのアクセッサ
    DebugSubsystem* GetDebugSubsystem() { return GetSubsystem<DebugSubsystem>(); }
#endif

    /// @brief レンダーパイプラインを取得（シーンのユーザーパス登録などに使用）
    RenderPipeline* GetRenderPipeline() const { return renderPipeline_.get(); }

    /// @brief 型指定でサブシステムを取得する
    /// @tparam T IEngineSubsystemを継承するサブシステムの型
    /// @return 該当サブシステムへのポインタ（未登録の場合nullptr）
    template<typename T>
    T* GetSubsystem() {
        for (auto& sys : subsystems_) {
            if (auto* p = dynamic_cast<T*>(sys.get())) {
                return p;
            }
        }
        return nullptr;
    }

private:
    friend class GraphicsComponentFactory; // RegisterComponent への限定アクセス許可
    friend class CoreComponentFactory;     // RegisterComponent への限定アクセス許可

    // ──────────────────────────────────────────────────────────
    // コンポーネント登録ヘルパー
    // ──────────────────────────────────────────────────────────

    /// @brief 型消去コンポーネント所有構造の基底クラス
    struct IComponentHolder {
        virtual ~IComponentHolder() = default;
    };
    /// @brief 型安全なコンポーネント所有構造
    template<typename T>
    struct ComponentHolder final : IComponentHolder {
        std::unique_ptr<T> ptr;
        explicit ComponentHolder(std::unique_ptr<T> p) : ptr(std::move(p)) {}
    };

    /// @brief コンポーネントを登録
    /// @tparam T コンポーネントの型
    /// @param component コンポーネントのunique_ptr
    template<typename T>
    void RegisterComponent(std::unique_ptr<T> component) {
        T* ptr = component.get();
        componentOwners_.push_back(std::make_unique<ComponentHolder<T>>(std::move(component)));
        componentManager_.Register(ptr);
    }

    // ──────────────────────────────────────────────────────────
    // コンポーネント作成ヘルパーメソッド
    // ──────────────────────────────────────────────────────────
    void CreateInputComponents();
    void CreateAudioComponents();
    void CreateLightComponents();
    void CreateFrameRateController();

    /// @brief デフォルトのレンダーパイプラインを構築
    void BuildDefaultRenderPipeline();

    // ──────────────────────────────────────────────────────────
    // コアメンバ変数
    // ──────────────────────────────────────────────────────────

    WinApp* winApp_ = nullptr;

    // コンポーネント管理
    ComponentManager componentManager_;

    // コンポーネントの所有権管理（型安全なホルダーコンテナ）
    std::vector<std::unique_ptr<IComponentHolder>> componentOwners_;

    // レンダーパイプライン
    std::unique_ptr<RenderPipeline> renderPipeline_;

    // レンダリングフレーム通し番号（RenderContext::frameNumber の供給元）
    uint64_t renderFrameNumber_ = 0;

    // ドメイン管理コンテキスト（GBuffer / シャドウ / レイトレーシング）
    std::unique_ptr<RenderDomainContext> renderDomainContext_;

    // Hi-Z オクルージョンカリングシステム（GraphicsComponentFactory が生成）。
    // GPU リソースは Finalize 内の Shutdown() で解放するが、インスタンス自体は
    // 全 Model（~ModelVisibility が UnregisterTarget を呼ぶ）より長く生存させる
    // 必要があるため、EngineSystem のデストラクタまで保持する
    std::unique_ptr<HiZOcclusionSystem> hiZOcclusionSystem_;

    // ──────────────────────────────────────────────────────────
    // サブシステム管理
    // ──────────────────────────────────────────────────────────
    // 全サブシステム（ライフサイクルを一括ループするためのコンテナ）
    // 型指定アクセスはGetSubsystem<T>()を使用する
    std::vector<std::unique_ptr<IEngineSubsystem>> subsystems_;

    };
}
