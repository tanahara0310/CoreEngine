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

        /// @brief 水面 RT のパス名
        constexpr const char* kWaterPassNames[] = {
            "RTWaterCausticsPass",
            "RTWaterRefractionPass",
            "RTWaterReflectionPass",
        };

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
        auto* lightManager = engine_ ? engine_->GetComponent<LightManager>() : nullptr;

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

        RayTracingShadowSettings settings = shadowMgr->GetSettings();
        bool changed = false;

        ImGui::PushItemWidth(220.0f);

        changed |= ImGui::SliderInt("ソフトシャドウ サンプル数", &settings.softShadowSamples, 1, 16);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "1 ピクセルあたりのシャドウレイ本数。\n"
                "コストはほぼ本数に比例する（1spp = 約1.14ms @1920x1080）。");
        }

        changed |= ImGui::SliderFloat("光源の角半径 [rad]", &settings.lightRadius, 0.0f, 0.2f, "%.4f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "ペナンブラ幅を決める。実際の太陽は約 0.0046 rad。\n"
                "0.15 以上はペナンブラが広すぎてゴーストが出るため非推奨。");
        }

        changed |= ImGui::SliderFloat("シャドウバイアス", &settings.shadowBias, 0.0f, 0.5f, "%.4f");
        changed |= ImGui::SliderFloat("レイ基準距離", &settings.maxRayDistance, 10.0f, 20000.0f, "%.0f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "これより遠い遮蔽物は影を落とさない。\n"
                "実際に使われる距離は上の表の rayDist(実効) を参照。");
        }

        changed |= ImGui::SliderFloat("テンポラル ブレンド係数", &settings.historyAlpha, 0.01f, 1.0f, "%.3f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("小さいほど履歴を重視（0.15 = 履歴 85%）。1.0 で履歴を使わない。");
        }

        // A-Trous パス数（ping-pong の都合で偶数のみ）
        static const char* kAtrousLabels[] = { "0 (デノイズ無効)", "2 (step 1,2)", "4 (step 1,2,4,8)" };
        static const int   kAtrousValues[] = { 0, 2, 4 };
        int atrousIndex = 2;
        for (int i = 0; i < 3; ++i) {
            if (kAtrousValues[i] == shadowMgr->GetEffectiveAtrousPassCount()) { atrousIndex = i; }
        }
        if (ImGui::Combo("A-Trous パス数", &atrousIndex, kAtrousLabels, 3)) {
            settings.atrousPassCount = kAtrousValues[atrousIndex];
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "1 パスあたり約 0.28ms @1920x991。\n"
                "ping-pong の都合で偶数のみ（Stage 3 で解消予定）。\n"
                "1spp のシャドウに step=8（実効31x31）まで広げる必要は無い。");
        }

        changed |= ImGui::SliderFloat("深度エッジ重み (phiDepth)", &settings.denoisePhiDepth, 0.05f, 8.0f, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "大きいほどエッジ検出が厳しくなり影の輪郭が保たれる（ぼけにくい）。\n"
                "小さいほど深度差を跨いで広くぼける。\n"
                "Stage 1 で深度指標をワールド原点距離→線形ビュー深度へ変えたので\n"
                "この値の意味が変わっている。");
        }

        ImGui::PopItemWidth();

        changed |= ImGui::Checkbox("射程を太陽高度でスケールする", &settings.scaleRayDistanceBySunElevation);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "光源が低いほどレイはほぼ水平に走るため、固定距離だと遠くの遮蔽物へ届かない。\n"
                "有効時は 基準距離 / sin(太陽高度) を使う（最大 10 倍で頭打ち）。\n"
                "OFF にすると従来どおり基準距離をそのまま使う。");
        }

        changed |= ImGui::Checkbox("履歴参照を無効化（テンポラル由来の切り分け用）", &settings.disableHistory);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("ON にすると毎フレーム現フレームの空間前処理結果のみを使う。\n残像・ゴーストがテンポラル由来かを判定できる。");
        }

        if (changed) {
            shadowMgr->SetSettings(settings);
        }

        if (ImGui::Button("既定値へ戻す")) {
            shadowMgr->SetSettings(RayTracingShadowSettings{});
        }
        ImGui::SameLine();
        ImGui::TextDisabled("変更は自動保存される（RayTracing.json）");
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

        // 生マスク（denoiseTemp）と履歴はパス終了時 UAV / NON_PIXEL_SHADER_RESOURCE で
        // 残るため、そのまま ImGui::Image に渡すと不正な状態でのピクセルシェーダ読みになる。
        // このフレームだけ PIXEL_SHADER_RESOURCE へ遷移するよう要求する。
        // パネル（またはこのチェックボックス）を閉じれば要求が止まりバリアも消える。
        shadowMgr->RequestDebugViewTransition();

        ImGui::SetNextItemWidth(160.0f);
        ImGui::SliderInt("対象ライト", &selectedLightIndex_, 0,
            static_cast<int>(RayTracingShadowManager::kMaxDirectionalLights) - 1);

        // マスクは R32_FLOAT の 1 チャンネルなので、ImGui では赤成分のみで表示される
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
              "デノイズ前の生マスク。1spp なので 0/1 のバイナリノイズに見えるのが正常",
              shadowMgr->GetRawShadowSRVHandle(viewId, li) },
            { "2. Temporal 後 / A-Trous 後",
              "テンポラル蓄積 → A-Trous を適用した最終マスク（DeferredLighting が読む実体）",
              shadowMgr->GetShadowSRVHandle(viewId, li) },
            { "3. History (前フレーム)",
              "テンポラル蓄積の履歴。CopyResource で毎フレーム全画面コピーしている（Stage 3 で ping-pong 化）",
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
