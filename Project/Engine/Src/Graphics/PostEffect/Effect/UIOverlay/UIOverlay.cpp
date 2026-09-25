#include "pch.h"
#include "UIOverlay.h"
#include "Graphics/PostEffect/Graph/PostEffectGraphBuilder.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include "Graphics/Render/RenderManager.h"
#ifdef CORE_EDITOR
#include "Editor/ImGui/ImguiManager.h"
#endif

namespace CoreEngine
{
    const std::wstring& UIOverlay::GetPixelShaderPath() const
    {
        // 入力をそのまま写すだけのシェーダー
        static const std::wstring path = L"FullScreen.PS.hlsl";
        return path;
    }

    void UIOverlay::BuildPasses(PostEffectGraphBuilder& builder)
    {
        RenderManager* const renderManager = builder.Context().renderManager;
        const RenderViewType viewType = builder.Context().viewSettings.viewType;

        builder.AddGraphicsPass(GetEffectName(), { builder.Input() }, builder.ChainOutput(),
            [this, renderManager, viewType](const PostEffectPassContext& passContext) {
                if (passContext.reads.empty()) {
                    return;
                }

                // 入力を写してから、その上へ UI を描く
                Draw(passContext.reads[0]);
                if (renderManager) {
                    renderManager->DrawOverlayQueuePass(passContext.cmdList, viewType);
                }
            });
    }

    void UIOverlay::DrawImGui()
    {
#ifdef CORE_EDITOR
        ImGui::PushID("UIOverlay");
        ImGui::TextWrapped("画面固定の UI（UI 画像・UI テキスト）を、トーンマップと画面演出の後に重ねます。"
            "UI の色は指定した色のまま出ます。フェードとローディング画面は UI の上に掛かります。");
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "このエフェクトは常に有効です。");
        ImGui::PopID();
#endif // CORE_EDITOR
    }
}
