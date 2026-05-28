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
// コンポーネントアクセス利便インクルード
// GetComponent<T>()呼び出し元のこトを考慮し、
// 主要コンポーネント型の完全型を提供するためのインクルードです。
// 废止: 各呼び出し元ファイルで必要な型を直接インクルードすることを推奨。
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

class EngineSystem {
public:
    ~EngineSystem();
    /// @brief エンジンシステムの初期化
    /// @param winApp ウィンドウアプリケーション
    /// @param config エンジン設定
    void Initialize(WinApp* winApp, const EngineConfig& config);

    /// @brief エンジンシステムの終了処理
    void Finalize();

    /// @brief フレーム開始処理
    void BeginFrame();

    /// @brief フレーム終了処理
    void EndFrame();

    /// @brief 共通描画パイプライン - オフスクリーンレンダリングとポストエフェクトを自動処理
    /// @param renderCallback シーン固有の描画処理を記述するコールバック
    void ExecuteRenderPipeline(std::function<void()> renderCallback);

    /// @brief SceneManagerを設定
    void SetSceneManager(SceneManager* sceneManager);

    /// @brief SceneManagerを取得
    SceneManager* GetSceneManager() const;

    /// @brief WinAppを取得
    WinApp* GetWinApp() const { return winApp_; }

    /// @brief RenderDomainContextを取得
    RenderDomainContext* GetRenderDomainContext() { return renderDomainContext_.get(); }

    // ──────────────────────────────────────────────────────────
    // コンポーネントアクセッサ
    // ──────────────────────────────────────────────────────────

    /// @brief コンポーネントを取得（型安全）
    /// @tparam T コンポーネントの型
    /// 
    /// 代表的なコンポーネント例:
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
    /// @return コンポーネントへのポインタ（登録されていない場合nullptr）
    template<typename T>
    T* GetComponent() {
        return componentManager_.Get<T>();
    }

    /// @brief コンポーネントが登録されているか確認
    /// @tparam T コンポーネントの型（GetComponentと同じ型を指定可能）
    /// @return 登録されている場合true
    template<typename T>
    bool HasComponent() const {
        return componentManager_.Has<T>();
    }

#ifdef USE_IMGUI
    // ──────────────────────────────────────────────────────────
    // デバッグ機能アクセッサ（デバッグビルドのみ）
    // ──────────────────────────────────────────────────────────

    /// @brief デバッグサブシステムへのアクセッサ
    DebugSubsystem* GetDebugSubsystem() { return GetSubsystem<DebugSubsystem>(); }
#endif

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
    void CreateGraphicsComponents(const EngineConfig& config);
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

    // ドメイン管理コンテキスト（GBuffer / シャドウ / レイトレーシング）
    std::unique_ptr<RenderDomainContext> renderDomainContext_;

    // ──────────────────────────────────────────────────────────
    // サブシステム管理
    // ──────────────────────────────────────────────────────────
    // 全サブシステム（ライフサイクルを一括ループするためのコンテナ）
    // 型指定アクセスはGetSubsystem<T>()を使用する
    std::vector<std::unique_ptr<IEngineSubsystem>> subsystems_;

    };
}
