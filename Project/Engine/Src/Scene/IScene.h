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

/// @brief シーン更新の粒度（SceneManager が毎フレーム指定する）
enum class SceneUpdateMode {
    Full,      ///< 通常。ゲームロジックと全 Feature を回す
    Suspended, ///< ゲームの進行を止め、常時更新の Feature（RunsWhileStopped）だけを回す
};

/// @brief シーンのインターフェース。SceneManager はこの型だけを介してシーンを回す
class IScene {
public:
    virtual ~IScene() = default;

    /// @brief 毎フレームのロジック更新
    /// @param mode Suspended でゲームの進行だけを止める（シーン遷移のフェード中）
    /// @note ここを丸ごと呼ばずに飛ばしてはいけない。大気・雲・フォグは毎フレームの
    ///       更新で「このフレーム有効」フラグを立て直しており、1 フレームでも飛ばすと
    ///       その場で描画から外れる。止めたいときは Suspended を渡すこと。
    virtual void Update(SceneUpdateMode mode) = 0;
    /// @brief 描画キューの構築（実際の描画は RenderGraph の各パスが行う）
    virtual void PrepareRender() {}
    /// @brief シーン終了時の後始末
    virtual void Finalize() = 0;

    /// @brief 初期化をステップ列へ積む（シーン構築の唯一の入口）
    /// @details SceneManager は必ずこの経路でシーンを組み立てる。1 ステップの実行時間が
    ///          そのままローディング画面の止まる時間になるので、重い処理は分割すること。
    ///          フレームを回さない同期読み込みかどうかは SceneManager 側が吸収するため、
    ///          シーンはステップの積み方だけを考えればよい。
    /// @param sequence 積み先のステップ列
    /// @param engine   エンジンシステム
    virtual void BuildLoadTasks(CoreEngine::StartupSequence& sequence, CoreEngine::EngineSystem* engine) = 0;

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
