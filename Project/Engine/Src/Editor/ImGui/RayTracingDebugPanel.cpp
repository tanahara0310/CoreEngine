#include "pch.h"
#include "RayTracingDebugPanel.h"

#ifdef USE_IMGUI

#include "EngineSystem/EngineSystem.h"
#include "Graphics/Common/GpuTimestampProfiler.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Graphics/RayTracing/RayTracingDispatchInfo.h"
#include "Graphics/RayTracing/RayTracingShadowManager.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Water/RayTracing/WaterCausticsRayTracingManager.h"
#include "Graphics/Water/RayTracing/WaterReflectionRayTracingManager.h"
#include "Graphics/Water/RayTracing/WaterRefractionRayTracingManager.h"
#include "Editor/ImGui/CVarPanel.h"

#include <imgui.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>

namespace CoreEngine
{
    namespace
    {
        /// @brief RTシャドウの 3 ステージのパス名（RenderGraph の GetName() と一致させること）
        constexpr const char* kShadowPassNames[] = {
            "RTShadowTrace",
            "RTShadowTemporal",
            "RTShadowDenoise",
        };

        /// @brief RT シャドウ設定の CVar 接頭辞（定義は RayTracingShadowManager.cpp）
        constexpr const char* kShadowCVarPrefix = "r.RTShadow";

        /// @brief 水面 RT のパス名
        constexpr const char* kWaterPassNames[] = {
            "RTWaterCausticsPass",
            "RTWaterRefractionPass",
            "RTWaterReflectionPass",
        };

    /// @brief GPU 時間のセルを、しきい値に応じて色分けして描く
        void DrawGpuMsCell(float gpuMs)
        {
            if (gpuMs < 0.0f) {
                ImGui::TextDisabled("--");
                return;
            }
            // 1ms 超えは注意色、0.5ms 超えは黄色
            ImVec4 color = ImVec4(0.70f, 0.85f, 0.70f, 1.0f);
            if (gpuMs >= 1.0f)      { color = ImVec4(0.95f, 0.55f, 0.45f, 1.0f); }
            else if (gpuMs >= 0.5f) { color = ImVec4(0.90f, 0.85f, 0.45f, 1.0f); }
            ImGui::TextColored(color, "%.3f ms", gpuMs);
        }
    }

    // -------------------------------------------------------------------------
    // Initialize
    // -------------------------------------------------------------------------
    void RayTracingDebugPanel::Initialize(EngineSystem* engine, GpuTimestampProfiler* gpuProfiler)
    {
        engine_ = engine;
        gpuProfiler_ = gpuProfiler;
    }

    // -------------------------------------------------------------------------
    // Draw
    // -------------------------------------------------------------------------
    void RayTracingDebugPanel::Draw()
    {
        if (!engine_) {
            ImGui::TextDisabled("EngineSystem が未設定です");
            return;
        }

        RenderDomainContext* domain = engine_->GetRenderDomainContext();
        if (!domain) {
            ImGui::TextDisabled("RenderDomainContext が未設定です");
            return;
        }

        AccelerationStructureManager* asMgr = domain->GetAccelerationStructureManager();
        RayTracingShadowManager* shadowMgr = domain->GetRayTracingShadowManager();

        DrawAccelerationStructureSection(asMgr);
        ImGui::Spacing();
        DrawShadowSection(shadowMgr);
        ImGui::Spacing();
        DrawShadowSettingsSection(shadowMgr);
        ImGui::Spacing();
        DrawWaterSection();
        ImGui::Spacing();
        DrawIntermediateBufferSection(shadowMgr);

        DrawEnlargedPopup();
    }

    // -------------------------------------------------------------------------
    // 加速構造セクション
    // -------------------------------------------------------------------------
    void RayTracingDebugPanel::DrawAccelerationStructureSection(AccelerationStructureManager* asMgr)
    {
        SectionHeader("加速構造 (BLAS / TLAS)");

        if (!asMgr) {
            ImGui::TextDisabled("  AccelerationStructureManager が利用不可です");
            return;
        }

        if (!asMgr->IsSupported()) {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f),
                "  DXR 非対応 GPU です（レイトレーシングは全て無効）");
            return;
        }

        // D3D12_RAYTRACING_TIER は 10 = Tier 1.0, 11 = Tier 1.1 という生値なので整形する
        const UINT tier = asMgr->GetRaytracingTier();
        ImGui::TextColored(ImVec4(0.60f, 0.90f, 0.60f, 1.0f),
            "  DXR 対応  Raytracing Tier %u.%u", tier / 10, tier % 10);

        if (ImGui::BeginTable("##ASStats", 2,
            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {

            // ラベル + 文字列値の 2 列を 1 行として出す（可変長引数を避けて呼び側で整形する）
            auto row = [](const char* label, const std::string& value) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(label);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(value.c_str());
                };

            row("BLAS 数", std::to_string(asMgr->GetBLASCount()));
            row("TLAS インスタンス数", std::to_string(asMgr->GetTLASInstanceCount()));
            row("BLAS 合計サイズ", FormatBytes(asMgr->GetBLASTotalBytes()));
            row("TLAS サイズ", FormatBytes(asMgr->GetTLASResultBytes()));
            row("スクラッチ合計", FormatBytes(asMgr->GetScratchTotalBytes()));
            row("解放待ちリソース", std::to_string(asMgr->GetRetiredResourceCount()));

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("ASBuild GPU 時間");
            ImGui::TableNextColumn();
            DrawGpuMsCell(FindPassGpuMs("ASBuild"));

            ImGui::EndTable();
        }

        ImGui::TextDisabled("  ※TLAS はフラスタム非依存（画面外の遮蔽物も影を落とす）");
        ImGui::TextDisabled("  ※現状 TLAS に入るのは不透明 ModelGameObject のみ");
        ImGui::TextDisabled("    （スキンメッシュ・半透明・FFT海面は未対応 = Stage 4）");
    }

    // -------------------------------------------------------------------------
    // RTシャドウセクション
    // -------------------------------------------------------------------------
    void RayTracingDebugPanel::DrawShadowSection(RayTracingShadowManager* shadowMgr)
    {
        SectionHeader("RT シャドウ");

        if (!shadowMgr || !shadowMgr->IsInitialized()) {
            ImGui::TextDisabled("  RayTracingShadowManager が未初期化です");
            return;
        }

        // ---- ステージ別 GPU 時間 ----
        float totalMs = 0.0f;
        if (ImGui::BeginTable("##ShadowStages", 2,
            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("ステージ");
            ImGui::TableSetupColumn("GPU 時間");
            ImGui::TableHeadersRow();

            for (const char* passName : kShadowPassNames) {
                const float ms = FindPassGpuMs(passName);
                if (ms > 0.0f) { totalMs += ms; }
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(passName);
                ImGui::TableNextColumn();
                DrawGpuMsCell(ms);
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.85f, 0.75f, 0.35f, 1.0f), "合計");
            ImGui::TableNextColumn();
            DrawGpuMsCell(totalMs > 0.0f ? totalMs : -1.0f);

            ImGui::EndTable();
        }

        const float frameMs = FindPassGpuMs("Frame Total");
        if (frameMs > 0.0f && totalMs > 0.0f) {
            ImGui::Text("  フレーム比: %.1f%%  (%.3f / %.3f ms)",
                totalMs / frameMs * 100.0f, totalMs, frameMs);
        }

        // ---- ライトごとのディスパッチ状況 ----
        ImGui::Spacing();
        auto* lightManager = engine_ ? engine_->GetService<LightManager>() : nullptr;

        if (ImGui::BeginTable("##ShadowDispatch", 6,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("Light");
            ImGui::TableSetupColumn("状態");
            ImGui::TableSetupColumn("解像度");
            ImGui::TableSetupColumn("レイ本数");
            ImGui::TableSetupColumn("BLAS");
            ImGui::TableSetupColumn("補足");
            ImGui::TableHeadersRow();

            uint64_t totalRays = 0;
            for (uint32_t li = 0; li < RayTracingShadowManager::kMaxDirectionalLights; ++li) {
                const RayTracingDispatchInfo& info =
                    shadowMgr->GetDispatchInfo(RayTracingShadowManager::ViewID::GameView, li);

                // 一度もディスパッチされていないスロットは表から省く
                if (info.status == RayTracingDispatchStatus::None) {
                    continue;
                }
                totalRays += info.rayCount;

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const Light* light = lightManager ? lightManager->GetDirectionalLight(li) : nullptr;
                if (light) {
                    ImGui::Text("%u: %s%s", li, light->name.c_str(), light->enabled ? "" : " (無効)");
                } else {
                    ImGui::Text("%u", li);
                }

                ImGui::TableNextColumn();
                if (info.WasDispatched()) {
                    ImGui::TextColored(ImVec4(0.60f, 0.90f, 0.60f, 1.0f), "%s", ToString(info.status));
                } else {
                    ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.45f, 1.0f), "%s", ToString(info.status));
                }

                ImGui::TableNextColumn();
                ImGui::Text("%ux%u", info.width, info.height);

                ImGui::TableNextColumn();
                ImGui::Text("%.2f M", static_cast<double>(info.rayCount) / 1.0e6);

                ImGui::TableNextColumn();
                ImGui::Text("%u", info.blasCount);

                ImGui::TableNextColumn();
                std::string extras;
                for (uint32_t e = 0; e < info.extraCount; ++e) {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%s=%.4g",
                        info.extras[e].label ? info.extras[e].label : "?", info.extras[e].value);
                    if (!extras.empty()) { extras += "  "; }
                    extras += buf;
                }
                ImGui::TextDisabled("%s", extras.c_str());
            }

            ImGui::EndTable();

            ImGui::Text("  総レイ本数: %.2f M rays/frame", static_cast<double>(totalRays) / 1.0e6);
            const float traceMs = FindPassGpuMs("RTShadowTrace");
            if (traceMs > 0.0f && totalRays > 0) {
                ImGui::SameLine();
                ImGui::TextDisabled("(%.2f Grays/s)",
                    static_cast<double>(totalRays) / (traceMs * 1.0e-3) / 1.0e9);
            }
        }
    }

    // -------------------------------------------------------------------------
    // RTシャドウ設定セクション
    // -------------------------------------------------------------------------
    void RayTracingDebugPanel::DrawShadowSettingsSection(RayTracingShadowManager* shadowMgr)
    {
        SectionHeader("RT シャドウ設定");

        if (!shadowMgr || !shadowMgr->IsInitialized()) {
            ImGui::TextDisabled("  RayTracingShadowManager が未初期化です");
            return;
        }

        // パラメータ UI は CVar から自動生成される（説明は各 CVar のツールチップに出る）
        CVarUI::DrawTree(kShadowCVarPrefix);

        if (ImGui::Button("既定値へ戻す")) {
            CVarUI::ResetTree(kShadowCVarPrefix);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("変更は自動保存される（CVars.json）");
    }

    // -------------------------------------------------------------------------
    // 水面 RT セクション
    // -------------------------------------------------------------------------
    void RayTracingDebugPanel::DrawWaterSection()
    {
        SectionHeader("水面 RT (屈折 / 反射 / コースティクス)");

        RenderDomainContext* domain = engine_ ? engine_->GetRenderDomainContext() : nullptr;
        if (!domain) {
            ImGui::TextDisabled("  RenderDomainContext が未設定です");
            return;
        }

        // Stage 2a 以降、水面マネージャは RayTracingPassBase を継承して
        // 共通型を直接保持しているので、変換なしでそのまま読める。
        std::array<RayTracingDispatchInfo, 3> infos{};
        int count = 0;
        if (auto* caustics = domain->GetWaterCausticsRayTracingManager()) {
            infos[count++] = caustics->GetDispatchInfo();
        }
        if (auto* refraction = domain->GetWaterRefractionRayTracingManager()) {
            infos[count++] = refraction->GetDispatchInfo();
        }
        if (auto* reflection = domain->GetWaterReflectionRayTracingManager()) {
            infos[count++] = reflection->GetDispatchInfo();
        }

        if (count == 0) {
            ImGui::TextDisabled("  水面 RT マネージャが利用不可です");
            return;
        }

        if (ImGui::BeginTable("##WaterRT", 6,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("パス");
            ImGui::TableSetupColumn("状態");
            ImGui::TableSetupColumn("解像度");
            ImGui::TableSetupColumn("レイ本数");
            ImGui::TableSetupColumn("GPU 時間");
            ImGui::TableSetupColumn("補足");
            ImGui::TableHeadersRow();

            for (int i = 0; i < count; ++i) {
                DrawDispatchInfoRow(infos[i]);
            }
            ImGui::EndTable();
        }

        ImGui::TextDisabled("  ※反射/屈折はヒット面の色を持ち帰れず、スクリーン空間へ再投影して");
        ImGui::TextDisabled("    SceneColor を参照している（＝画面外のヒットは棄却される。Stage 5 で解消）");
    }

    // -------------------------------------------------------------------------
    // 中間バッファセクション
    // -------------------------------------------------------------------------
    void RayTracingDebugPanel::DrawIntermediateBufferSection(RayTracingShadowManager* shadowMgr)
    {
        SectionHeader("RT シャドウ 中間バッファ");

        ImGui::Checkbox("サムネイルを表示", &showThumbnails_);
        if (!showThumbnails_) {
            return;
        }
        if (!shadowMgr || !shadowMgr->IsInitialized()) {
            ImGui::TextDisabled("  RayTracingShadowManager が未初期化です");
            return;
        }

        // 生マスク（Raw）と履歴はパス終了時 UAV / NON_PIXEL_SHADER_RESOURCE で
        // 残るため、そのまま ImGui::Image に渡すと不正な状態でのピクセルシェーダ読みになる。
        // このフレームだけ PIXEL_SHADER_RESOURCE へ遷移するよう要求する。
        // パネル（またはこのチェックボックス）を閉じれば要求が止まりバリアも消える。
        shadowMgr->RequestDebugViewTransition();

        ImGui::SetNextItemWidth(160.0f);
        ImGui::SliderInt("対象ライト", &selectedLightIndex_, 0,
            static_cast<int>(RayTracingShadowManager::kMaxDirectionalLights) - 1);

        // マスクは R8_UNORM の 1 チャンネルなので、ImGui では赤成分のみで表示される
        // （0=影, 1=光 → 赤いほど光が当たっている）
        ImGui::TextDisabled("  1ch テクスチャのため赤で表示される（赤 = 非遮蔽 / 黒 = 影）");

        const auto viewId = RayTracingShadowManager::ViewID::GameView;
        const uint32_t li = static_cast<uint32_t>(selectedLightIndex_);

        const float availW = ImGui::GetContentRegionAvail().x;
        const int   columns = (availW >= 240.0f * 3 + 32.0f) ? 3 : ((availW >= 240.0f * 2 + 16.0f) ? 2 : 1);
        const float thumbW = (availW - ImGui::GetStyle().ItemSpacing.x * (columns - 1)) / columns;
        const float thumbH = thumbW * (9.0f / 16.0f);

        struct Entry {
            const char* label;
            const char* tooltip;
            D3D12_GPU_DESCRIPTOR_HANDLE srv;
        };
        const Entry entries[] = {
            { "1. Raw (RayGen 出力)",
              "デノイズ前の生マスク（トレース解像度）。1spp なので 0/1 のバイナリノイズに見えるのが正常",
              shadowMgr->GetRawShadowSRVHandle(viewId, li) },
            { "2. 最終マスク (フル解像度)",
              "テンポラル蓄積 → A-Trous → アップサンプルまで通した実体（DeferredLighting が読む）",
              shadowMgr->GetShadowSRVHandle(viewId, li) },
            { "3. History (今フレームの書き込み先)",
              "テンポラル蓄積の履歴（トレース解像度）。Stage 3 で 2 枚 ping-pong になり全画面コピーは廃止した",
              shadowMgr->GetHistorySRVHandle(viewId, li) },
        };

        int col = 0;
        for (const Entry& entry : entries) {
            if (col > 0) { ImGui::SameLine(); }
            DrawThumbnail(entry.label, entry.tooltip, entry.srv, thumbW, thumbH);
            col = (col + 1) % columns;
        }
    }

    // -------------------------------------------------------------------------
    // ヘルパー
    // -------------------------------------------------------------------------
    void RayTracingDebugPanel::DrawDispatchInfoRow(const RayTracingDispatchInfo& info)
    {
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(info.passName ? info.passName : "?");

        ImGui::TableNextColumn();
        if (info.WasDispatched()) {
            ImGui::TextColored(ImVec4(0.60f, 0.90f, 0.60f, 1.0f), "%s", ToString(info.status));
        } else {
            ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.45f, 1.0f), "%s", ToString(info.status));
        }

        ImGui::TableNextColumn();
        ImGui::Text("%ux%u", info.width, info.height);

        ImGui::TableNextColumn();
        ImGui::Text("%.2f M", static_cast<double>(info.rayCount) / 1.0e6);

        ImGui::TableNextColumn();
        // ownerName_ とパス名は綴りが違うので、対応表から引く
        float ms = -1.0f;
        if (info.passName) {
            for (const char* passName : kWaterPassNames) {
                // "WaterCausticsRayTracingManager" ↔ "RTWaterCausticsPass" のような
                // 綴り違いを吸収するため、キーワードで対応付ける
                const bool caustics = std::strstr(info.passName, "Caustics") && std::strstr(passName, "Caustics");
                const bool refraction = std::strstr(info.passName, "Refraction") && std::strstr(passName, "Refraction");
                const bool reflection = std::strstr(info.passName, "Reflection") && std::strstr(passName, "Reflection");
                if (caustics || refraction || reflection) {
                    ms = FindPassGpuMs(passName);
                    break;
                }
            }
        }
        DrawGpuMsCell(ms);

        ImGui::TableNextColumn();
        std::string extras;
        for (uint32_t e = 0; e < info.extraCount; ++e) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s=%.4g",
                info.extras[e].label ? info.extras[e].label : "?", info.extras[e].value);
            if (!extras.empty()) { extras += "  "; }
            extras += buf;
        }
        ImGui::TextDisabled("%s", extras.c_str());
    }

    float RayTracingDebugPanel::FindPassGpuMs(const char* passName) const
    {
        if (!gpuProfiler_ || !passName) {
            return -1.0f;
        }

        const auto& results = gpuProfiler_->GetResults();
        for (uint32_t i = 0; i < results.size(); ++i) {
            const char* name = gpuProfiler_->GetSlotName(i);
            if (name && std::strcmp(name, passName) == 0) {
                return results[i].gpuMs;
            }
        }
        return -1.0f;
    }

    void RayTracingDebugPanel::DrawThumbnail(const char* label, const char* tooltip,
        D3D12_GPU_DESCRIPTOR_HANDLE srv, float thumbW, float thumbH)
    {
        ImGui::PushID(label);
        ImGui::BeginGroup();
        ImGui::TextUnformatted(label);

        if (srv.ptr != 0) {
            ImGui::Image((ImTextureID)srv.ptr, ImVec2(thumbW, thumbH));

            if (ImGui::IsItemClicked()) {
                enlargedTitle_ = label;
                enlargedSrv_ = srv;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(label);
                if (tooltip && tooltip[0] != '\0') {
                    ImGui::Separator();
                    ImGui::TextDisabled("%s", tooltip);
                }
                ImGui::TextDisabled("クリックで拡大");
                ImGui::EndTooltip();
            }
        } else {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetCursorScreenPos();
            dl->AddRectFilled(p, ImVec2(p.x + thumbW, p.y + thumbH), IM_COL32(60, 60, 60, 255));
            dl->AddRect(p, ImVec2(p.x + thumbW, p.y + thumbH), IM_COL32(120, 120, 120, 255));
            ImGui::Dummy(ImVec2(thumbW, thumbH));
            const ImVec2 textSize = ImGui::CalcTextSize("N/A");
            dl->AddText(ImVec2(p.x + (thumbW - textSize.x) * 0.5f, p.y + (thumbH - textSize.y) * 0.5f),
                IM_COL32(150, 150, 150, 255), "N/A");
        }

        ImGui::EndGroup();
        ImGui::PopID();
    }

    void RayTracingDebugPanel::DrawEnlargedPopup()
    {
        if (enlargedTitle_.empty()) {
            return;
        }

        ImGui::SetNextWindowSize(
            ImVec2(kThumbLarge + 32.0f, kThumbLarge * (9.0f / 16.0f) + 64.0f), ImGuiCond_Always);
        bool open = true;
        if (ImGui::Begin(enlargedTitle_.c_str(), &open, ImGuiWindowFlags_NoResize)) {
            if (enlargedSrv_.ptr != 0) {
                ImGui::Image((ImTextureID)enlargedSrv_.ptr,
                    ImVec2(kThumbLarge, kThumbLarge * (9.0f / 16.0f)));
            } else {
                ImGui::TextDisabled("テクスチャが見つかりません");
            }
        }
        ImGui::End();
        if (!open) {
            enlargedTitle_.clear();
            enlargedSrv_ = {};
        }
    }

    void RayTracingDebugPanel::SectionHeader(const char* label)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.75f, 0.35f, 1.0f));
        ImGui::SeparatorText(label);
        ImGui::PopStyleColor();
    }

    std::string RayTracingDebugPanel::FormatBytes(unsigned long long bytes)
    {
        char buf[64];
        if (bytes >= 1024ull * 1024ull) {
            std::snprintf(buf, sizeof(buf), "%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
        } else if (bytes >= 1024ull) {
            std::snprintf(buf, sizeof(buf), "%.1f KB", static_cast<double>(bytes) / 1024.0);
        } else {
            std::snprintf(buf, sizeof(buf), "%llu B", bytes);
        }
        return buf;
    }

} // namespace CoreEngine

#endif // USE_IMGUI
