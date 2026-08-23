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

// WinApp は CoreEngine 名前空間の型。以前はグローバルスコープで前方宣言しており
// （= 別物の ::WinApp を宣言していた）、GraphicsCore.h が名前空間内で前方宣言して
// いたおかげで偶然コンパイルが通っていた。GraphicsCore から WinApp 依存を外した
// Phase 5 で露見したので、ここで正しい名前空間へ宣言する。
namespace CoreEngine { class WinApp; }

// ──────────────────────────────────────────────────────────
// サービスアクセス利便インクルード
// GetService<T>() の呼び出し元が完全型を必要とするため、
// 主要サービス型のヘッダをここでまとめて提供している。
// 非推奨: 各呼び出し元ファイルで必要な型を直接インクルードすることを推奨。
// ──────────────────────────────────────────────────────────
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Texture/TextureManager.h"
#include "Input/InputManager.h"
#include "Audio/SoundManager.h"
#include "Utility/FrameRate/FrameRateController.h"


/// @file
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

/// @brief エンジンの常駐サービスとサブシステムを所有し、フレームを駆動する中核クラス
class EngineSystem {
public:
    EngineSystem(); // 前方宣言型の unique_ptr メンバがあるため .cpp で定義する
    ~EngineSystem();

    /// @brief エンジンの初期化処理を起動シーケンスへステップとして積む
    /// @param buildPreloadTasks アセット先読み用の差し込み口（ModelManager 生成直後に呼ばれる）
    /// @details ステップに割るのは、呼び出し側が Step() の合間にメッセージ処理と
    ///          ローディング表示を挟めるようにするため。
    /// @warning ステップは全部積んでから回すこと（StartupSequence の warning 参照）
    void BuildStartupTasks(
        StartupSequence& sequence,
        WinApp* winApp,
        const EngineConfig& config,
        const std::function<void(StartupSequence&)>& buildPreloadTasks = {});


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

    /// @brief エンジンサービスを取得（型安全。未登録なら nullptr）
    /// @tparam T サービスの型（GraphicsCore / TextureManager / ModelManager / InputManager など）
    /// @note `GameObject::GetComponent<T>()` とは別物。こちらはエンジン全体で 1 個ずつ存在する
    ///       常駐サービスのロケータ。サブシステムの取得は `GetSubsystem<T>()`。
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

    // フレーム通し番号は FrameSync が単一ソース（EngineSystem 側では数えない）

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
