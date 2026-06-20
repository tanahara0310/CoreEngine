#pragma once
#include <d3d12.h>
#include <string>
#include <functional>

#include "Math/Vector/Vector4.h"

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
struct ReflectionViewRequest {
    bool isEnabled = false;
    float planeHeight = 0.0f;
};

struct ReflectionViewResult {
    D3D12_GPU_DESCRIPTOR_HANDLE reflectionSrv{};
    D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv{};
    D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrv{};
    Vector4 clipPlane{};
    bool isValid = false;
};

class IScene {
public:
    virtual ~IScene() = default;

    virtual void Initialize(CoreEngine::EngineSystem* engine) = 0;
    virtual void Update() = 0;
    virtual void PrepareRender() {}
    virtual void Draw() = 0;
    virtual void DrawReflectionView() { Draw(); }
    virtual void SetupReflectionView(ICamera* mainCamera, float planeHeight) { (void)mainCamera; (void)planeHeight; }
    virtual void RestoreReflectionView(ICamera* mainCamera) { (void)mainCamera; }
    virtual void Finalize() = 0;

    virtual ICamera* GetGameViewCamera3D() const { return nullptr; }
    virtual ICamera* GetDefaultGameViewCamera3D() const { return GetGameViewCamera3D(); }
    virtual ICamera* GetGameViewCamera2D() const { return nullptr; }
    virtual GameObjectManager* GetGameObjectManager() { return nullptr; }

    /// @brief ReflectionView の実行要求を取得する
    /// @return ReflectionView を必要とする場合の設定
    virtual ReflectionViewRequest GetReflectionViewRequest() const { return {}; }

    /// @brief Engine 側で生成した ReflectionView の結果をシーンへ適用する
    /// @param result ReflectionColor / SceneDepth / SceneColor と clip plane をまとめた結果
    virtual void ApplyReflectionViewResult(const ReflectionViewResult& result) { (void)result; }

    /// @brief SceneManager への参照を設定（自動呼び出し）
    virtual void SetSceneManager(CoreEngine::SceneManager* sceneManager) {
        sceneManager_ = sceneManager;
    }

protected:
    SceneManager* sceneManager_ = nullptr;
};
}
