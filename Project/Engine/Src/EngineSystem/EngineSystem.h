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

// WinApp は CoreEngine 名前空間の型（グローバルスコープで前方宣言すると別物になる）
namespace CoreEngine { class WinApp; }

// ──────────────────────────────────────────────────────────
// このヘッダはサービス型のヘッダを一切 include しない。
//
// GetService<T>() / HasService<T>() は ComponentManager 経由で typeid(T) を使うため
// 「呼び出し元の翻訳単位で T が完全型であること」を要求する。
// かつてはその利便のため主要サービス型 8 本をここでまとめて配っていたが、
// EngineSystem.h は 49 ファイルから include されており、配った型のヘッダを 1 つ
// 触るだけで全ファイルが再コンパイル対象になっていた。
//
// GetService<T>() を呼ぶファイルは、T のヘッダを自分で include すること。
// ──────────────────────────────────────────────────────────


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
    /// @note 型引きは GetService<T>() と同じ ComponentManager 機構を使う
    ///       （型キーのハッシュ引き。旧実装の dynamic_cast 線形探索から置き換えた）。
    ///       キーは登録時の具象型そのものなので、基底型を指定しても引けない。
    template<typename T>
    T* GetSubsystem() {
        return subsystemIndex_.Get<T>();
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

    /// @brief サブシステムを生成して登録する
    /// @tparam T IEngineSubsystem を継承する具象型
    /// @return 生成したサブシステム（所有権は subsystems_ が持つ）
    /// @note 所有（subsystems_）と型引きインデックス（subsystemIndex_）を
    ///       必ず対で更新するための唯一の登録経路。subsystems_ へ直接
    ///       push_back すると GetSubsystem<T>() から引けなくなる。
    template<typename T>
    T* RegisterSubsystem() {
        auto subsystem = std::make_unique<T>();
        T* ptr = subsystem.get();
        subsystems_.push_back(std::move(subsystem));
        subsystemIndex_.Register<T>(ptr);
        return ptr;
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
    // 登録順に BeginFrame / Initialize、逆順に EndFrame / Finalize を回す
    std::vector<std::unique_ptr<IEngineSubsystem>> subsystems_;

    // 型指定アクセス用のインデックス（所有はしない。実体は subsystems_ 側）。
    // サービスの GetService<T>() と同じ ComponentManager を使い、
    // 「型で引く」実装をエンジン内で 1 本に揃えている
    ComponentManager subsystemIndex_;

    };
}
