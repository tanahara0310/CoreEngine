#include "pch.h"
#include "UICanvas.h"
#include "Graphics/Render/UI/UIRenderer.h"
#include "Graphics/Render/RenderManager.h"
#include "EngineSystem/EngineSystem.h"

namespace CoreEngine
{
    void UICanvas::Initialize(EngineSystem* engine, const Vector2& referenceResolution)
    {
        engine_ = engine;
        referenceResolution_ = referenceResolution;

        auto* renderManager = engine_->GetComponent<RenderManager>();
        if (renderManager) {
            renderer_ = dynamic_cast<UIRenderer*>(renderManager->GetRenderer(RenderPassType::UI));
        }

        if (renderer_) {
            renderer_->SetReferenceResolution(referenceResolution_.x, referenceResolution_.y);
        }
    }

    void UICanvas::SetReferenceResolution(const Vector2& size)
    {
        referenceResolution_ = size;
        if (renderer_) {
            renderer_->SetReferenceResolution(size.x, size.y);
        }
    }

} // namespace CoreEngine
