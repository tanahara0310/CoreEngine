#pragma once
#include <d3d12.h>
#include <string>
#include <functional>
#include <vector>

#include "Graphics/Water/WaterSurfaceData.h"
#include "Math/Vector/Vector4.h"
#include "Graphics/Render/Pass/RenderPass.h"

// 前方宣言
namespace CoreEngine {
    class EngineSystem;
    class SceneManager;
    class ICamera;
    class GameObjectManager;
}

/// @brief シーンインターフェース

namespace CoreEngine
{
struct RenderViewResult {
    std::string name;
    std::string outputTargetName;
    D3D12_GPU_DESCRIPTOR_HANDLE viewSrv{};
    D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv{};
    D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrv{};
    bool isValid = false;
};

struct RenderViewRequest {
    bool isEnabled = false;
    std::string name;
    RenderViewSettings viewSettings{};
    std::function<void()> drawCallback;
    std::function<void()> beforeExecute;
    std::function<void()> afterExecute;
    std::function<void(const RenderViewResult&)> completionCallback;
};

class IScene {
public:
    virtual ~IScene() = default;

    virtual void Initialize(CoreEngine::EngineSystem* engine) = 0;
    virtual void Update() = 0;
    virtual void PrepareRender() {}
    virtual void Draw() = 0;
    virtual void DrawRenderView() { Draw(); }
    virtual void Finalize() = 0;

    virtual ICamera* GetGameViewCamera3D() const { return nullptr; }
    virtual ICamera* GetDefaultGameViewCamera3D() const { return GetGameViewCamera3D(); }
    virtual ICamera* GetGameViewCamera2D() const { return nullptr; }
    virtual GameObjectManager* GetGameObjectManager() { return nullptr; }
    virtual const WaterSurfaceData* GetWaterRefractionSurfaceData() const { return nullptr; }

    /// @brief Scene が要求する補助 RenderView 一覧を構築する
    /// @return Engine 側 RenderGraph で実行する補助 View 要求群
    virtual std::vector<RenderViewRequest> BuildRenderViewRequests() { return {}; }

    /// @brief SceneManager への参照を設定（自動呼び出し）
    virtual void SetSceneManager(CoreEngine::SceneManager* sceneManager) {
        sceneManager_ = sceneManager;
    }

protected:
    SceneManager* sceneManager_ = nullptr;
};
}
