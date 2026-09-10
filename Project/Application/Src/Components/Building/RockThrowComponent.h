#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Math/Vector/Vector3.h"

#include <functional>

namespace CoreEngine
{
    class MeshRendererComponent;
    class TransformComponent;
}

namespace GameComponents
{
    /// @brief 列車から岩へ投げる石の放物線アニメーション。
    class RockThrowComponent final : public CoreEngine::IComponent
    {
    public:
        const char* GetTypeName() const override { return "RockThrow"; }

        json OnSerialize() const override;
        void OnDeserialize(const json& j) override;

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "岩破壊の投石"; }
        bool DrawInspector() override;
#endif

        void Start() override;
        void Update() override;

        bool Play(
            const CoreEngine::Vector3& start,
            const CoreEngine::Vector3& target,
            std::function<void()> onImpact);
        bool IsPlaying() const { return isPlaying_; }

    private:
        CoreEngine::TransformComponent* transform_ = nullptr;
        CoreEngine::MeshRendererComponent* renderer_ = nullptr;
        CoreEngine::Vector3 startPosition_{};
        CoreEngine::Vector3 targetPosition_{};
        std::function<void()> onImpact_;
        float elapsed_ = 0.0f;
        float duration_ = 0.6f;
        float arcHeight_ = 2.0f;
        float stoneScale_ = 0.2f;
        float rotationSpeed_ = 12.0f;
        bool isPlaying_ = false;
    };
}
