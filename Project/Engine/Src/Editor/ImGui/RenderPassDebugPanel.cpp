#include "pch.h"
#include "RenderPassDebugPanel.h"

#ifdef USE_IMGUI

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/FrameBlackboard.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <vector>

namespace CoreEngine
{
    void RenderPassDebugPanel::Initialize(DirectXCommon* dxCommon)
    {
        dxCommon_ = dxCommon;
    }

    // -------------------------------------------------------------------------
    // Draw
    // -------------------------------------------------------------------------
    void RenderPassDebugPanel::Draw()
    {
        if (!renderTargetManager_ && !renderDomainContext_) {
            ImGui::TextDisabled("RenderTargetManager / RenderDomainContext が未設定です");
            return;
        }

        auto* gbuf = renderDomainContext_ ? renderDomainContext_->GetGBufferManager() : nullptr;

        // ---- バッファエントリーを収集 ----

        // GBuffer ターゲット（WorldPosition は廃止済み。深度から復元する）
        BufferEntry gbufEntries[4] = {};
        if (gbuf) {
            gbufEntries[0] = { "AlbedoAO",        "rgb=アルベド / a=AO",            gbuf->GetSRVHandle(GBufferManager::Target::AlbedoAO),        true };
            gbufEntries[1] = { "NormalRoughness",  "rgb=ワールド法線(encode) / a=符号付きRoughness(符号=IBL有効/無効, 0=アンリット)", gbuf->GetSRVHandle(GBufferManager::Target::NormalRoughness), true };
            gbufEntries[2] = { "EmissiveMetallic", "rgb=エミッシブ / a=メタリック", gbuf->GetSRVHandle(GBufferManager::Target::EmissiveMetallic), true };
            gbufEntries[3] = { "MotionVector",     "rg=NDCモーションベクター",       gbuf->GetSRVHandle(GBufferManager::Target::MotionVector),     true };
        }

        // 説明テキスト
        ImGui::TextDisabled("各パスが書き込んだ中間バッファを可視化します");
        ImGui::TextDisabled("サムネイルをクリックすると拡大表示します");
        ImGui::Spacing();

        // ================================================================
        // GBuffer セクション
        // ================================================================
        SectionHeader("G-Buffer");

        if (!gbuf) {
            ImGui::TextDisabled("  GBufferManager が利用不可です");
        } else {
            // 2列グリッド表示
            const float availW = ImGui::GetContentRegionAvail().x;
            const int   columns = (availW >= kThumbW * 2 + 16.0f) ? 2 : 1;
            const float thumbW = (availW - ImGui::GetStyle().ItemSpacing.x * (columns - 1)) / columns;
            const float thumbH = thumbW * (9.0f / 16.0f);

            int col = 0;
            for (auto& entry : gbufEntries) {
                if (col > 0) ImGui::SameLine();
                DrawThumbnail(entry, thumbW, thumbH);
                col = (col + 1) % columns;
            }
            if (col != 0) { /* 列が途中で終わった場合は改行済み */ }
        }

        ImGui::Spacing();

        // ================================================================
        // SSAO セクション
        // ================================================================
        SectionHeader("SSAO");

        if (renderTargetManager_) {
            BufferEntry ssaoEntries[2] = {};

            auto FetchTargetSrv = [&](const char* name) -> D3D12_GPU_DESCRIPTOR_HANDLE {
                if (auto* rt = renderTargetManager_->GetRenderTarget(name)) {
                    return rt->GetSRVHandle();
                }
                return {};
            };

            D3D12_GPU_DESCRIPTOR_HANDLE ssaoSrv     = FetchTargetSrv(RenderTargetNames::SSAOBuffer);
            D3D12_GPU_DESCRIPTOR_HANDLE ssaoBlurSrv = FetchTargetSrv(RenderTargetNames::SSAOBlurBuffer);

            if (ssaoSrv.ptr != 0) {
                ssaoEntries[0] = { "SSAO (Raw)",  "SSAO 生成結果（ブラー前）",    ssaoSrv,     true };
            }
            if (ssaoBlurSrv.ptr != 0) {
                ssaoEntries[1] = { "SSAO (Blur)", "バイラテラルブラー後の SSAO",  ssaoBlurSrv, true };
            }

            const float availW = ImGui::GetContentRegionAvail().x;
            const int   columns = (availW >= kThumbW * 2 + 16.0f) ? 2 : 1;
            const float thumbW = (availW - ImGui::GetStyle().ItemSpacing.x * (columns - 1)) / columns;
            const float thumbH = thumbW * (9.0f / 16.0f);

            int col = 0;
            for (auto& entry : ssaoEntries) {
                if (!entry.valid) continue;
                if (col > 0) ImGui::SameLine();
                DrawThumbnail(entry, thumbW, thumbH);
                col = (col + 1) % columns;
            }
            if (!ssaoEntries[0].valid && !ssaoEntries[1].valid) {
                ImGui::TextDisabled("  SSAOBuffer が見つかりません");
            }
            // デバッグ情報（SRV ptr が 0 でないか確認用）
            ImGui::TextDisabled("  SSAO srv.ptr=0x%llX  Blur srv.ptr=0x%llX",
                (unsigned long long)ssaoEntries[0].srv.ptr,
                (unsigned long long)ssaoEntries[1].srv.ptr);
        } else {
            ImGui::TextDisabled("  RenderTargetManager が未設定です");
        }

        ImGui::Spacing();

        // ================================================================
        // Deferred Lighting / 最終合成 セクション
        // ================================================================
        SectionHeader("Lighting / 最終合成");

        if (renderTargetManager_) {
            const float availW = ImGui::GetContentRegionAvail().x;
            const int   columns = (availW >= kThumbW * 2 + 16.0f) ? 2 : 1;
            const float thumbW = (availW - ImGui::GetStyle().ItemSpacing.x * (columns - 1)) / columns;
            const float thumbH = thumbW * (9.0f / 16.0f);

            std::vector<std::string> outputLabels;
            std::vector<BufferEntry> outputEntries;
            auto AppendTargetEntry = [&](const std::string& targetName, const char* label, const char* tooltip) {
                auto* rt = renderTargetManager_->GetRenderTarget(targetName);
                if (!rt) {
                    return;
                }

                D3D12_GPU_DESCRIPTOR_HANDLE srv = rt->GetSRVHandle();

                if (srv.ptr == 0) {
                    return;
                }

                outputLabels.push_back(label);
                outputEntries.push_back({ outputLabels.back().c_str(), tooltip, srv, true });
                };

            AppendTargetEntry(RenderTargetNames::PostEffectFinal, "PostEffectFinal", "PostEffect 最終結果");

            for (size_t index = 0;; ++index) {
                const std::string targetName = RenderTargetManager::MakePostEffectIntermediateTargetName(index);
                if (!renderTargetManager_->HasRenderTarget(targetName)) {
                    break;
                }

                const std::string label = FrameBlackboard::MakePostEffectIntermediateName(index);
                AppendTargetEntry(targetName, label.c_str(), "PostEffect 中間ターゲット");
            }

            int col = 0;
            for (const auto& entry : outputEntries) {
                if (col > 0) ImGui::SameLine();
                DrawThumbnail(entry, thumbW, thumbH);
                col = (col + 1) % columns;
            }
        } else {
            ImGui::TextDisabled("  RenderTargetManager が未設定です");
        }

        // ================================================================
        // 拡大ポップアップ
        // ================================================================
        if (!enlargedTitle_.empty()) {
            ImGui::SetNextWindowSize(ImVec2(kThumbLarge + 32.0f, kThumbLarge * (9.0f / 16.0f) + 64.0f),
                ImGuiCond_Always);
            bool open = true;
            if (ImGui::Begin(enlargedTitle_.c_str(), &open,
                ImGuiWindowFlags_NoResize)) {

                if (enlargedSrv_.ptr != 0) {
                    ImGui::Image(
                        (ImTextureID)enlargedSrv_.ptr,
                        ImVec2(kThumbLarge, kThumbLarge * (9.0f / 16.0f)));
                } else {
                    ImGui::TextDisabled("テクスチャが見つかりません");
                }
            }
            ImGui::End();
            if (!open) { enlargedTitle_.clear(); enlargedSrv_ = {}; }
        }
    }

    // -------------------------------------------------------------------------
    // DrawThumbnail
    // -------------------------------------------------------------------------
    void RenderPassDebugPanel::DrawThumbnail(const BufferEntry& entry, float thumbW, float thumbH)
    {
        ImGui::PushID(entry.label);

        // ラベル（上）
        ImGui::TextUnformatted(entry.label);

        if (entry.valid && entry.srv.ptr != 0) {
            ImGui::Image(
                (ImTextureID)entry.srv.ptr,
                ImVec2(thumbW, thumbH));

            // クリックで拡大
            if (ImGui::IsItemClicked()) {
                enlargedTitle_ = entry.label;
                enlargedSrv_   = entry.srv;
            }

            // ホバー時ツールチップ
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(entry.label);
                if (entry.tooltip && entry.tooltip[0] != '\0') {
                    ImGui::Separator();
                    ImGui::TextDisabled("%s", entry.tooltip);
                }
                ImGui::TextDisabled("クリックで拡大");
                ImGui::EndTooltip();
            }
        } else {
            // プレースホルダー（灰色ボックス）
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetCursorScreenPos();
            dl->AddRectFilled(p, ImVec2(p.x + thumbW, p.y + thumbH),
                IM_COL32(60, 60, 60, 255));
            dl->AddRect(p, ImVec2(p.x + thumbW, p.y + thumbH),
                IM_COL32(120, 120, 120, 255));
            ImGui::Dummy(ImVec2(thumbW, thumbH));
            // 中央に "N/A"
            ImVec2 textSize = ImGui::CalcTextSize("N/A");
            ImVec2 textPos{
                p.x + (thumbW - textSize.x) * 0.5f,
                p.y + (thumbH - textSize.y) * 0.5f
            };
            dl->AddText(textPos, IM_COL32(150, 150, 150, 255), "N/A");
        }

        ImGui::PopID();
    }

    // -------------------------------------------------------------------------
    // SectionHeader
    // -------------------------------------------------------------------------
    void RenderPassDebugPanel::SectionHeader(const char* label)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.75f, 0.35f, 1.0f));
        ImGui::SeparatorText(label);
        ImGui::PopStyleColor();
    }

} // namespace CoreEngine

#endif // USE_IMGUI
