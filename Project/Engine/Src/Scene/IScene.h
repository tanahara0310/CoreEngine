#pragma once
#include <d3d12.h>
#include <string>
#include <functional>
#include <vector>

#include "Graphics/Water/WaterSurfaceData.h"
#include "Math/Vector/Vector4.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include "EngineSystem/Startup/StartupSequence.h"

// 前方宣言
namespace CoreEngine {
    class EngineSystem;
    class SceneManager;
    class Camera;
    class GameObjectManager;
    class RenderPipeline;
}

namespace CoreEngine
{
/// @brief 補助ビューの描画結果（出力先の名前と参照用 SRV）
struct RenderViewResult {
    std::string name;
    std::string outputTargetName;
    D3D12_GPU_DESCRIPTOR_HANDLE viewSrv{};
    D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv{};
    D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrv{};
    bool isValid = false;
};

/// @brief 1 フレーム内で追加実行してほしい補助ビュー（平面反射など）の要求
struct RenderViewRequest {
    bool isEnabled = false;
    std::string name;
    RenderViewSettings viewSettings{};
    std::function<void()> beforeExecute;
    std::function<void()> afterExecute;
    std::function<void(const RenderViewResult&)> completionCallback;
};

/// @brief シーンのインターフェース。SceneManager はこの型だけを介してシーンを回す
class IScene {
public:
    virtual ~IScene() = default;

    /// @brief シーン開始時の初期化
    virtual void Initialize(CoreEngine::EngineSystem* engine) = 0;
    /// @brief 毎フレームのロジック更新
    virtual void Update() = 0;
    virtual void PrepareRender() {}
    /// @brief 描画コマンドの発行
    virtual void Draw() = 0;
    /// @brief シーン終了時の後始末
    virtual void Finalize() = 0;

    /// @brief 初期化をステップ列へ積む（ローディング画面はステップの合間に描かれる）
    /// @details 既定は Initialize() 全体を 1 ステップとして積む。
    ///          1 ステップの実行時間がそのままローディング画面の止まる時間になる。
    /// @param sequence 積み先のステップ列
    /// @param engine   エンジンシステム
    virtual void BuildLoadTasks(CoreEngine::StartupSequence& sequence, CoreEngine::EngineSystem* engine) {
        sequence.Add("シーン構築", [this, engine] { Initialize(engine); });
    }

    virtual Camera* GetGameViewCamera3D() const { return nullptr; }
    virtual Camera* GetGameViewCamera2D() const { return nullptr; }
    virtual GameObjectManager* GetGameObjectManager() { return nullptr; }

    /// @brief Scene が要求する補助 RenderView 一覧を構築する
    /// @return Engine 側 RenderGraph で実行する補助 View 要求群
    virtual std::vector<RenderViewRequest> BuildRenderViewRequests() { return {}; }

    /// @brief シーン固有のレンダーパスをパイプラインへ登録する（シーン初期化直後に自動呼び出し）
    /// @details pipeline.AddPass(pass, phase, priority) で任意フェーズへ挿入できる。
    ///          登録したパスはシーン破棄時に SceneManager が自動で除去するため、
    ///          Finalize での手動削除は不要。エンジンコードの編集も不要。
    /// @param pipeline エンジンのレンダーパイプライン
    virtual void RegisterRenderPasses([[maybe_unused]] RenderPipeline& pipeline) {}

    /// @brief SceneManager への参照を設定（自動呼び出し）
    virtual void SetSceneManager(CoreEngine::SceneManager* sceneManager) {
        sceneManager_ = sceneManager;
    }

protected:
    SceneManager* sceneManager_ = nullptr;
};
}
