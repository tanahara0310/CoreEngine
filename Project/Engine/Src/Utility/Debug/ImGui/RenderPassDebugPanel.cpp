#include "pch.h"
#include "RenderPassDebugPanel.h"

#ifdef USE_IMGUI

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"

#include <imgui.h>
#include <imgui_internal.h>

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
        if (!dxCommon_) {
            ImGui::TextDisabled("DirectXCommon が未設定です");
            return;
        }

        auto* gbuf = dxCommon_->GetGBufferManager();

        // ---- バッファエントリーを収集 ----

        // GBuffer ターゲット
        BufferEntry gbufEntries[5] = {};
        if (gbuf) {
            gbufEntries[0] = { "AlbedoAO",        "rgb=アルベド / a=AO",            gbuf->GetSRVHandle(GBufferManager::Target::AlbedoAO),        true };
            gbufEntries[1] = { "NormalRoughness",  "rgb=ワールド法線(encode) / a=Roughness", gbuf->GetSRVHandle(GBufferManager::Target::NormalRoughness), true };
            gbufEntries[2] = { "EmissiveMetallic", "rgb=エミッシブ / a=メタリック", gbuf->GetSRVHandle(GBufferManager::Target::EmissiveMetallic), true };
            gbufEntries[3] = { "WorldPosition",    "rgb=ワールド座標 / a=フラグ",   gbuf->GetSRVHandle(GBufferManager::Target::WorldPosition),    true };
            gbufEntries[4] = { "MotionVector",     "rg=NDCモーションベクター",       gbuf->GetSRVHandle(GBufferManager::Target::MotionVector),     true };
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

        // RenderTargetManager 経由で SSAO バッファを取得
        // RenderTargetManager は EngineSystem から直接取れないため
        // DirectXCommon 経由で RenderTargetManager を取得する方法がない場合は
        // dxCommon_->GetOffScreenSrvHandle() で代替インデックスを試みる
        // → DebugSubsystem 側で SetRenderTargetManager() を呼んでいる場合はそちらを使う
        if (renderTargetManager_) {
            BufferEntry ssaoEntries[2] = {};

            auto FetchOffscreenSrv = [&](const char* name) -> D3D12_GPU_DESCRIPTOR_HANDLE {
                if (auto* rt = renderTargetManager_->GetRenderTarget(name)) {
                    // OffscreenRenderTarget 経由で index を取り、DirectXCommon から直接 SRV を取得
                    if (auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(rt)) {
                        return dxCommon_->GetOffScreenSrvHandle(
                            static_cast<uint32_t>(offscreen->GetIndex()));
                    }
                    return rt->GetSRVHandle();
                }
                return {};
            };

            D3D12_GPU_DESCRIPTOR_HANDLE ssaoSrv     = FetchOffscreenSrv("SSAOBuffer");
            D3D12_GPU_DESCRIPTOR_HANDLE ssaoBlurSrv = FetchOffscreenSrv("SSAOBlurBuffer");

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
            const char* outputTargets[] = { "Offscreen0", "Offscreen1", "SceneView" };
            const char* outputTooltips[] = {
                "DeferredLighting + PostEffect の出力先",
                "PostEffect ピンポンバッファ",
                "SceneView 専用 RT（エディタービュー用）"
            };

            const float availW = ImGui::GetContentRegionAvail().x;
            const int   columns = (availW >= kThumbW * 2 + 16.0f) ? 2 : 1;
            const float thumbW = (availW - ImGui::GetStyle().ItemSpacing.x * (columns - 1)) / columns;
            const float thumbH = thumbW * (9.0f / 16.0f);

            int col = 0;
            for (int i = 0; i < 3; ++i) {
                auto* rt = renderTargetManager_->GetRenderTarget(outputTargets[i]);
                if (!rt) continue;
                D3D12_GPU_DESCRIPTOR_HANDLE srv{};
                if (auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(rt)) {
                    srv = dxCommon_->GetOffScreenSrvHandle(
                        static_cast<uint32_t>(offscreen->GetIndex()));
                } else {
                    srv = rt->GetSRVHandle();
                }
                if (srv.ptr == 0) continue;
                BufferEntry entry{ outputTargets[i], outputTooltips[i], srv, true };
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
