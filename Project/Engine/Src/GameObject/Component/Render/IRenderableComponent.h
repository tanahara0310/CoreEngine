#pragma once

#include "Graphics/Render/RenderPassType.h"
#include "Graphics/Render/RenderItem.h"
#include "Graphics/Pipeline/PipelineStateManager.h"

namespace CoreEngine
{
struct DrawViewInfo;

/// @brief 「このオブジェクトをどう描くか」をコンポーネント側が答えるためのインターフェース。
/// @details `GameObject` はこれを持つコンポーネントに描画パス・ブレンド・実描画を委譲する。
///          継承で `GetRenderPassType()` を override する必要がなくなり、素の GameObject に
///          描画コンポーネントを載せるだけで絵が出る。
class IRenderableComponent {
public:
    virtual ~IRenderableComponent() = default;

    /// @brief 所属する描画パス
    virtual RenderPassType GetRenderPassType() const = 0;

    /// @brief 使用するブレンドモード
    virtual BlendMode GetBlendMode() const { return BlendMode::kBlendModeNone; }
    virtual void SetBlendMode(BlendMode blendMode) { (void)blendMode; }

    /// @brief 描画キューの振り分け種別（既定はパス種別から自動判定させる）
    virtual RenderItemKind GetRenderItemKind() const { return RenderItemKind::Default; }

    /// @brief 実際の描画（カリング込み）
    virtual void Render(const DrawViewInfo& view) = 0;
};
}
