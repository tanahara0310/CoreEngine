#include "pch.h"
#include "RayTracingDebugPanel.h"

#ifdef CORE_EDITOR

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#include <imgui.h>

#include "Editor/ImGui/CVarPanel.h"
#include "Editor/ImGui/GameViewportOverlay.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Line/Line.h"
#include "Graphics/Model/ModelResource.h"
#include "Graphics/RHI/Debug/GpuTimestampProfiler.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Graphics/RayTracing/RTInstanceMask.h"
#include "Graphics/RayTracing/RTScene.h"
#include "Graphics/RayTracing/RayTracingDispatchInfo.h"
#include "Graphics/Render/Line/LineRendererPipeline.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Render/RenderManager.h"
#include "Scene/SceneManager.h"

namespace CoreEngine
{
    namespace
    {
        constexpr ImVec4 kColorOk = ImVec4(0.60f, 0.90f, 0.60f, 1.0f);
        constexpr ImVec4 kColorInfo = ImVec4(0.70f, 0.75f, 0.85f, 1.0f);
        constexpr ImVec4 kColorWarning = ImVec4(0.95f, 0.80f, 0.35f, 1.0f);
        constexpr ImVec4 kColorError = ImVec4(0.95f, 0.45f, 0.40f, 1.0f);
        constexpr ImVec4 kColorStopped = ImVec4(0.95f, 0.65f, 0.45f, 1.0f);
        constexpr ImVec4 kColorHeading = ImVec4(0.85f, 0.75f, 0.35f, 1.0f);

        constexpr const char* kComponentLetters[4] = { "R", "G", "B", "A" };

        /// @brief 見え方のタブが使う中間結果（RTDebugSystem.cpp の申告と同じ名前）
        constexpr const char* kSceneChannelEffect = "デバッグ";
        constexpr const char* kSceneChannelName = "カメラのレイ";

        /// @brief 結果がこのフレーム数より古ければ「今は作っていない」とみなす
        constexpr uint64_t kFreshFrames = 4;

        /// @brief タブの番号（共通の帯から切り替える）
        enum TabIndex : int { kTabOverview = 0, kTabChannels, kTabScene, kTabTrace, kTabAcceleration, kTabChecks };

        struct MaskBit {
            uint32_t bit;
            const char* name;
            const char* letter;
        };
        constexpr MaskBit kMaskBits[] = {
            { RTInstanceMask::kShadowCaster, "影を落とす", "影" },
            { RTInstanceMask::kReflected, "映り込む", "映" },
            { RTInstanceMask::kStaticSolid, "止まっている形", "止" },
            { RTInstanceMask::kMovingSolid, "動く形", "動" },
        };

        void SectionHeader(const char* label)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, kColorHeading);
            ImGui::SeparatorText(label);
            ImGui::PopStyleColor();
        }

        /// @brief GPU 時間を、しきい値に応じて色分けして書く（負なら "--"）
        void DrawGpuMs(float gpuMs)
        {
            if (gpuMs < 0.0f) {
                ImGui::TextDisabled("--");
                return;
            }
            ImVec4 color = kColorOk;
            if (gpuMs >= 1.0f) {
                color = kColorError;
            } else if (gpuMs >= 0.5f) {
                color = kColorWarning;
            }
            ImGui::TextColored(color, "%.3f ms", gpuMs);
        }

        std::string FormatBytes(uint64_t bytes)
        {
            if (bytes >= 1024ull * 1024ull) {
                return std::format("{:.2f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
            }
            if (bytes >= 1024ull) {
                return std::format("{:.1f} KB", static_cast<double>(bytes) / 1024.0);
            }
            return std::format("{} B", bytes);
        }

        ImVec4 SeverityColor(RTDebugSeverity severity)
        {
            switch (severity) {
            case RTDebugSeverity::Ok: return kColorOk;
            case RTDebugSeverity::Info: return kColorInfo;
            case RTDebugSeverity::Warning: return kColorWarning;
            default: return kColorError;
            }
        }

        const char* SeverityLabel(RTDebugSeverity severity)
        {
            switch (severity) {
            case RTDebugSeverity::Ok: return "良し";
            case RTDebugSeverity::Info: return "情報";
            case RTDebugSeverity::Warning: return "警告";
            default: return "誤り";
            }
        }

        /// @brief インスタンスの印を 1 文字ずつ並べる
        std::string MaskLetters(uint32_t mask)
        {
            std::string text;
            for (const MaskBit& bit : kMaskBits) {
                if ((mask & bit.bit) != 0) {
                    text += bit.letter;
                }
            }
            return text.empty() ? std::string("なし") : text;
        }

        /// @brief パスの名前から GPU 時間を引く（見つからなければ -1）
        float FindPassGpuMs(const GpuTimestampProfiler* profiler, const char* passName)
        {
            if (!profiler || !passName) {
                return -1.0f;
            }
            const auto& results = profiler->GetResults();
            for (uint32_t i = 0; i < results.size(); ++i) {
                const char* name = profiler->GetSlotName(i);
                if (name && std::strcmp(name, passName) == 0) {
                    return results[i].gpuMs;
                }
            }
            return -1.0f;
        }

        /// @brief 効果のパスの GPU 時間の合計（どれも見つからなければ -1）
        float EffectGpuMs(const GpuTimestampProfiler* profiler, const RTDebugEffectDesc& desc)
        {
            float total = 0.0f;
            bool found = false;
            for (const char* passName : desc.passNames) {
                const float ms = FindPassGpuMs(profiler, passName);
                if (ms >= 0.0f) {
                    total += ms;
                    found = true;
                }
            }
            return found ? total : -1.0f;
        }

        /// @brief ファイルの道筋から名前だけを取る
        std::string FileNameOf(const std::string& path)
        {
            const size_t slash = path.find_last_of("/\\");
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }

        std::string ObjectNameOf(const GameObjectManager* objects, ObjectId id)
        {
            if (!objects || !id.IsValid()) {
                return "（物なし）";
            }
            const GameObject* object = objects->FindObject(id);
            return object ? object->GetName() : "（消えた物）";
        }

        // D3D12_RAY_FLAG_* のうち表に出すもの
        constexpr uint32_t kRayFlagForceOpaque = 0x1;
        constexpr uint32_t kRayFlagAcceptFirstHit = 0x4;
        constexpr uint32_t kRayFlagCullBack = 0x10;
        constexpr uint32_t kRayFlagCullFront = 0x20;

        /// @brief レイの撃ち方（フラグから）
        std::string RayFlagsText(uint32_t rayFlags)
        {
            std::string text = (rayFlags & kRayFlagAcceptFirstHit) != 0 ? "遮られるか" : "いちばん近い所";
            if ((rayFlags & kRayFlagForceOpaque) != 0) {
                text += "・不透明として";
            }
            if ((rayFlags & kRayFlagCullBack) != 0) {
                text += "・裏を飛ばす";
            }
            if ((rayFlags & kRayFlagCullFront) != 0) {
                text += "・表を飛ばす";
            }
            return text;
        }

        /// @brief 記録したディスパッチの名前（範囲外 = 名前なし）
        const char* DispatchLabel(const RTDebugRayRecordResult& record, uint32_t dispatch)
        {
            return dispatch < record.dispatches.size() ? record.dispatches[dispatch].label.c_str() : "（名前なし）";
        }

        const char* DispatchShader(const RTDebugRayRecordResult& record, uint32_t dispatch)
        {
            return dispatch < record.dispatches.size() ? record.dispatches[dispatch].shaderName.c_str() : "?";
        }

        /// @brief 記録したディスパッチごとの色（表の印と 3D の線で同じ色）
        Vector3 DispatchColor(const RTDebugRayRecordResult& record, uint32_t dispatch)
        {
            constexpr float kPalette[][3] = {
                { 1.00f, 0.85f, 0.20f }, { 0.30f, 0.90f, 1.00f }, { 1.00f, 0.45f, 0.30f },
                { 0.50f, 1.00f, 0.40f }, { 0.85f, 0.50f, 1.00f }, { 1.00f, 0.60f, 0.80f },
            };
            if (dispatch >= record.dispatches.size()) {
                return Vector3{ 0.6f, 0.6f, 0.6f };
            }
            const float* c = kPalette[dispatch % std::size(kPalette)];
            return Vector3{ c[0], c[1], c[2] };
        }

        float FloatFromBits(uint32_t bits)
        {
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        /// @brief RT_DEBUG_PRINT の値を種類に合わせて書く
        std::string FormatPrintedValue(const RTDebugPrintedValue& value)
        {
            std::string text;
            for (uint32_t c = 0; c < value.components; ++c) {
                if (c > 0) {
                    text += ", ";
                }
                const uint32_t bits = value.bits[c];
                switch (value.type) {
                case RTDebugPrintType::Float: text += std::format("{:.6g}", FloatFromBits(bits)); break;
                case RTDebugPrintType::Uint:  text += std::format("{}", bits); break;
                case RTDebugPrintType::Int:   text += std::format("{}", static_cast<int32_t>(bits)); break;
                case RTDebugPrintType::Bool:  text += bits != 0 ? "true" : "false"; break;
                }
            }
            return value.components > 1 ? "(" + text + ")" : text;
        }

        /// @brief 線 1 本と、当たった所の十字
        void AddRayLines(std::vector<Line>& lines, const Vector3& origin, const Vector3& direction,
            float tMin, float tMax, bool hit, float hitT, const Vector3& color)
        {
            constexpr float kMissLength = 300.0f;
            const float end = hit ? hitT : (std::min)(tMax, kMissLength);
            const float start = (std::min)((std::max)(tMin, 0.0f), end);
            auto at = [&](float t) {
                return Vector3{ origin.x + direction.x * t, origin.y + direction.y * t, origin.z + direction.z * t };
            };
            const Vector3 endPoint = at(end);
            lines.emplace_back(at(start), endPoint, color, 1.0f, false);
            if (hit) {
                const float s = (std::max)(end * 0.01f, 0.02f);
                lines.emplace_back(Vector3{ endPoint.x - s, endPoint.y, endPoint.z }, Vector3{ endPoint.x + s, endPoint.y, endPoint.z }, color, 1.0f, false);
                lines.emplace_back(Vector3{ endPoint.x, endPoint.y - s, endPoint.z }, Vector3{ endPoint.x, endPoint.y + s, endPoint.z }, color, 1.0f, false);
                lines.emplace_back(Vector3{ endPoint.x, endPoint.y, endPoint.z - s }, Vector3{ endPoint.x, endPoint.y, endPoint.z + s }, color, 1.0f, false);
            }
        }

        /// @brief 当たった物の名前（行の持ち主）を表のセルへ書く
        void DrawHitObjectCell(const RTDebugQuery& query, const RTScene* scene, bool hit, uint32_t row)
        {
            if (hit && scene && row < scene->GetRows().size()) {
                const RTScene::RowRecord& record = scene->GetRows()[row];
                ImGui::Text("%s", ObjectNameOf(query.objects, record.owner).c_str());
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("行 %u・%s", row,
                        record.resource ? FileNameOf(record.resource->GetFilePath()).c_str() : "?");
                }
            } else if (hit) {
                ImGui::Text("行 %u", row);
            } else {
                ImGui::TextDisabled("--");
            }
        }

        /// @brief 結果が今のフレームに近いか
        bool IsFresh(uint64_t resultFrame, uint64_t currentFrame)
        {
            return resultFrame != 0 && resultFrame + kFreshFrames >= currentFrame;
        }

        /// @brief 行 1 つの三角形の数（材質の表のサブメッシュの合計）
        uint64_t RowTriangleCount(const RTScene& scene, size_t row)
        {
            const auto& instances = scene.GetHitInstances();
            const auto& subMeshes = scene.GetHitSubMeshes();
            if (row >= instances.size()) {
                return 0;
            }
            uint64_t total = 0;
            const RTHitInstance& instance = instances[row];
            for (uint32_t s = 0; s < instance.subMeshCount; ++s) {
                const size_t index = static_cast<size_t>(instance.firstSubMesh) + s;
                if (index < subMeshes.size()) {
                    total += subMeshes[index].triangleCount;
                }
            }
            return total;
        }

        /// @brief 3×4 の行列で点を移す（DXR の行優先）
        Vector3 TransformPoint(const float (&m)[3][4], float x, float y, float z)
        {
            return Vector3{
                m[0][0] * x + m[0][1] * y + m[0][2] * z + m[0][3],
                m[1][0] * x + m[1][1] * y + m[1][2] * z + m[1][3],
                m[2][0] * x + m[2][1] * y + m[2][2] * z + m[2][3] };
        }
    }

    // -------------------------------------------------------------------------
    // 初期化と後始末
    // -------------------------------------------------------------------------
    void RayTracingDebugPanel::Initialize(EngineSystem* engine, GpuTimestampProfiler* gpuProfiler)
    {
        engine_ = engine;
        gpuProfiler_ = gpuProfiler;
        overlayId_ = Editor::GameViewportOverlay::Add(
            [this](const Editor::GameViewportOverlayArgs& args) { return DrawGameViewportOverlay(args); });
    }

    void RayTracingDebugPanel::Finalize()
    {
        if (overlayId_ != 0) {
            Editor::GameViewportOverlay::Remove(overlayId_);
            overlayId_ = 0;
        }
        if (linePipeline_) {
            linePipeline_->UnregisterLineSource(this);
            linePipeline_ = nullptr;
        }
        ResetSwitches();
    }

    void RayTracingDebugPanel::BeginFrame()
    {
        if (drawnThisFrame_) {
            drawnThisFrame_ = false;
            return;
        }
        if (windowOpen_) {
            windowOpen_ = false;
            ResetSwitches();
        }
    }

    void RayTracingDebugPanel::ResetSwitches()
    {
        RTDebug::SetSceneFrozen(false);
        RTDebug::SetNoiseFixed(false);
        RTDebug::SetStopped(false);
        RTDebugRegistry& registry = RTDebugRegistry::Get();
        for (uint32_t i = 0; i < registry.Effects().size(); ++i) {
            registry.SetEffectEnabled(i, true);
        }
        tracePicking_ = false;
        traceActive_ = false;
        viewSource_ = ViewSource::None;
        outputTarget_ = OutputTarget::Window;
    }

    void RayTracingDebugPanel::EnsureLineSource()
    {
        if (linePipeline_ || !engine_) {
            return;
        }
        auto* renderManager = engine_->GetService<RenderManager>();
        if (!renderManager) {
            return;
        }
        linePipeline_ = static_cast<LineRendererPipeline*>(renderManager->GetRenderer(RenderPassType::Line));
        if (linePipeline_) {
            linePipeline_->RegisterLineSource(this);
        }
    }

    RTDebugQuery RayTracingDebugPanel::BuildQuery() const
    {
        RTDebugQuery query;
        if (!engine_) {
            return query;
        }
        query.domain = engine_->GetRenderDomainContext();
        query.graphics = engine_->GetService<GraphicsCore>();
        query.lights = engine_->GetService<LightManager>();
        SceneManager* sceneManager = engine_->GetSceneManager();
        query.objects = sceneManager ? sceneManager->GetCurrentGameObjectManager() : nullptr;
        query.debug = query.domain ? query.domain->GetRTDebugSystem() : nullptr;
        query.frame = query.graphics ? query.graphics->Frame().FrameNumber() : 0;
        return query;
    }

    RTDebugSystem* RayTracingDebugPanel::GetDebugSystem() const
    {
        RenderDomainContext* domain = engine_ ? engine_->GetRenderDomainContext() : nullptr;
        return domain ? domain->GetRTDebugSystem() : nullptr;
    }

    void RayTracingDebugPanel::RunChecks(const RTDebugQuery& query)
    {
        checkLines_.clear();
        warningCount_ = 0;
        errorCount_ = 0;
        std::vector<RTDebugCheckResult> results;
        for (const RTDebugCheckDesc& check : RTDebugRegistry::Get().Checks()) {
            results.clear();
            check.run(query, results);
            for (RTDebugCheckResult& result : results) {
                if (result.severity == RTDebugSeverity::Warning) {
                    ++warningCount_;
                } else if (result.severity == RTDebugSeverity::Error) {
                    ++errorCount_;
                }
                checkLines_.push_back(CheckLine{ check.name, result.severity, std::move(result.message) });
            }
        }
    }

    // -------------------------------------------------------------------------
    // Draw
    // -------------------------------------------------------------------------
    void RayTracingDebugPanel::Draw()
    {
        drawnThisFrame_ = true;
        windowOpen_ = true;
        if (!engine_) {
            ImGui::TextDisabled("EngineSystem が未設定です");
            return;
        }
        EnsureLineSource();

        const RTDebugQuery query = BuildQuery();
        RTDebugSystem* debug = GetDebugSystem();
        if (debug) {
            debug->CollectResults();
        }
        RunChecks(query);
        DrawCommonBar(query);

        bool channelsVisible = false;
        bool sceneVisible = false;
        bool traceVisible = false;
        if (ImGui::BeginTabBar("##RTDebugTabs")) {
            auto tabFlags = [this](int tab) {
                return (selectTab_ == tab) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            };
            if (ImGui::BeginTabItem("概要", nullptr, tabFlags(kTabOverview))) {
                DrawOverviewTab(query);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("中間結果", nullptr, tabFlags(kTabChannels))) {
                channelsVisible = true;
                if (debug) {
                    DrawChannelsTab(*debug);
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("見え方", nullptr, tabFlags(kTabScene))) {
                sceneVisible = true;
                if (debug) {
                    DrawSceneTab(query, *debug);
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("レイの追跡", nullptr, tabFlags(kTabTrace))) {
                traceVisible = true;
                if (debug) {
                    DrawTraceTab(query, *debug);
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("加速構造", nullptr, tabFlags(kTabAcceleration))) {
                DrawAccelerationTab(query);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("チェック", nullptr, tabFlags(kTabChecks))) {
                if (debug) {
                    DrawChecksTab(*debug);
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        selectTab_ = -1;

        // 追跡の線と印は、追跡のタブを見ている間と、止めて残している間だけ出す
        if (!traceVisible) {
            tracePicking_ = false;
            traceActive_ = traceActive_ && traceHold_;
        }

        if (!debug) {
            return;
        }

        // 見ているタブの画像を頼む（ゲーム画面へ重ねている間は、ほかのタブでも頼み続ける）
        if (channelsVisible) {
            viewSource_ = ViewSource::Channel;
        } else if (sceneVisible) {
            viewSource_ = ViewSource::Scene;
        } else if (outputTarget_ == OutputTarget::Window) {
            viewSource_ = ViewSource::None;
        }

        const RTDebugRegistry& registry = RTDebugRegistry::Get();
        const int sceneChannel = registry.FindChannel(kSceneChannelEffect, kSceneChannelName);
        if (viewSource_ == ViewSource::Scene && sceneChannel >= 0) {
            debug->RequestScene(sceneRequest_);
            debug->RequestView(MakeViewRequest(sceneChannel, 0, sceneView_));
        } else if (viewSource_ == ViewSource::Channel && selectedChannel_ >= 0
            && static_cast<size_t>(selectedChannel_) < registry.Channels().size()) {
            if (selectedChannel_ == sceneChannel) {
                debug->RequestScene(sceneRequest_);
            }
            debug->RequestView(MakeViewRequest(selectedChannel_, selectedVariant_, channelView_));
        }
        if (scanChannels_) {
            debug->RequestScan();
        }
        probeEnabled_ = false;
    }

    // -------------------------------------------------------------------------
    // 全部のタブに出る帯
    // -------------------------------------------------------------------------
    void RayTracingDebugPanel::DrawCommonBar(const RTDebugQuery& query)
    {
        AccelerationStructureManager* asMgr = query.domain ? query.domain->GetAccelerationStructureManager() : nullptr;
        RTScene* scene = query.domain ? query.domain->GetRTScene() : nullptr;
        if (!asMgr || !asMgr->IsSupported()) {
            ImGui::TextColored(kColorError, "DXR の Tier 1.1 に対応していない GPU です（レイトレーシングは全部使わない）");
            ImGui::Separator();
            return;
        }

        const UINT tier = asMgr->GetRaytracingTier();
        ImGui::Text("DXR Tier %u.%u", tier / 10, tier % 10);
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::Text("TLAS %u 個", asMgr->GetTLASInstanceCount());
        ImGui::SameLine();
        if (RTDebug::IsSceneFrozen()) {
            ImGui::TextColored(kColorStopped, "（凍結中）");
        } else if (scene && query.frame > scene->GetLastBuildFrame() + 1) {
            ImGui::TextColored(kColorWarning, "（%llu フレーム前に作った）",
                static_cast<unsigned long long>(query.frame - scene->GetLastBuildFrame()));
        } else {
            ImGui::TextDisabled("（毎フレーム作り直し）");
        }

        float totalMs = (std::max)(FindPassGpuMs(gpuProfiler_, "ASBuild"), 0.0f);
        for (const RTDebugRegistry::EffectEntry& entry : RTDebugRegistry::Get().Effects()) {
            totalMs += (std::max)(EffectGpuMs(gpuProfiler_, entry.desc), 0.0f);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::Text("RT の GPU");
        ImGui::SameLine();
        DrawGpuMs(totalMs);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("加速構造を作る時間（ASBuild）と、申告された効果のパスの時間の合計");
        }

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        const ImVec4 checkColor = errorCount_ > 0 ? kColorError : (warningCount_ > 0 ? kColorWarning : kColorOk);
        ImGui::PushStyleColor(ImGuiCol_Text, checkColor);
        const std::string checkLabel = std::format("誤り {}・警告 {}##RTDebugCheckJump", errorCount_, warningCount_);
        if (ImGui::SmallButton(checkLabel.c_str())) {
            selectTab_ = kTabChecks;
        }
        ImGui::PopStyleColor();

        bool frozen = RTDebug::IsSceneFrozen();
        if (ImGui::Checkbox("凍結", &frozen)) {
            RTDebug::SetSceneFrozen(frozen);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("TLAS と材質の表を作り直さず、止めた時点の物のまま撃つ。\n"
                "物を動かしても影と映り込みは変わらない（カメラは動かせる）。\n物の数が変わったときやシーンを替えたときは作り直す");
        }
        ImGui::SameLine();
        bool fixedNoise = RTDebug::IsNoiseFixed();
        if (ImGui::Checkbox("乱数を固定", &fixedNoise)) {
            RTDebug::SetNoiseFixed(fixedNoise);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("乱数の番号を毎フレーム同じにする（影のざらつきが止まる）");
        }
        ImGui::SameLine();
        bool stopped = RTDebug::IsStopped();
        if (ImGui::Checkbox("全 RT を止める", &stopped)) {
            RTDebug::SetStopped(stopped);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("申告された効果を全部撃たない（影なし・水面の映り込みと屈折なし）。\nRT 無しの見た目と比べるのに使う");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("（窓を閉じると元に戻る）");
        ImGui::Separator();
    }

    // -------------------------------------------------------------------------
    // 概要
    // -------------------------------------------------------------------------
    void RayTracingDebugPanel::DrawOverviewTab(const RTDebugQuery& query)
    {
        RTDebugRegistry& registry = RTDebugRegistry::Get();
        const auto& effects = registry.Effects();
        if (effects.empty()) {
            ImGui::TextDisabled("申告された効果がありません");
            return;
        }

        const float em = ImGui::GetFontSize();
        const float statusX = em * 13.0f;
        const float sizeX = em * 19.0f;
        const float raysX = em * 25.0f;
        const float gpuX = em * 30.0f;
        ImGui::TextDisabled("ON  効果");
        ImGui::SameLine(statusX);
        ImGui::TextDisabled("状態");
        ImGui::SameLine(sizeX);
        ImGui::TextDisabled("解像度");
        ImGui::SameLine(raysX);
        ImGui::TextDisabled("レイ");
        ImGui::SameLine(gpuX);
        ImGui::TextDisabled("GPU");

        std::vector<RTDebugEffectRow> rows;
        for (uint32_t i = 0; i < effects.size(); ++i) {
            const RTDebugEffectDesc& desc = effects[i].desc;
            rows.clear();
            if (desc.collect) {
                desc.collect(query, rows);
            }

            ImGui::PushID(static_cast<int>(i));
            bool enabled = effects[i].enabled;
            if (ImGui::Checkbox("##on", &enabled)) {
                registry.SetEffectEnabled(i, enabled);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("切ると、この効果だけ撃たない（窓を閉じると元に戻る）");
            }
            ImGui::SameLine();
            const bool open = ImGui::TreeNodeEx("##effect", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", desc.label);

            // 状態・解像度・レイ・GPU
            uint32_t dispatched = 0;
            uint64_t rays = 0;
            const RayTracingDispatchInfo* first = nullptr;
            for (const RTDebugEffectRow& row : rows) {
                if (row.info.WasDispatched()) {
                    ++dispatched;
                    if (!first) {
                        first = &row.info;
                    }
                }
                rays += row.info.rayCount;
            }
            ImGui::SameLine(statusX);
            if (!effects[i].enabled || RTDebug::IsStopped()) {
                ImGui::TextColored(kColorStopped, "止めている");
            } else if (rows.empty()) {
                ImGui::TextDisabled("まだ撃っていない");
            } else if (rows.size() == 1) {
                ImGui::TextColored(rows[0].info.WasDispatched() ? kColorOk : kColorStopped, "%s", ToString(rows[0].info.status));
            } else {
                ImGui::TextColored(dispatched > 0 ? kColorOk : kColorStopped, "撃った %u / %zu", dispatched, rows.size());
            }
            ImGui::SameLine(sizeX);
            if (first) {
                ImGui::Text("%u×%u", first->width, first->height);
            } else {
                ImGui::TextDisabled("--");
            }
            ImGui::SameLine(raysX);
            ImGui::Text("%.2f M", static_cast<double>(rays) / 1.0e6);
            ImGui::SameLine(gpuX);
            DrawGpuMs(EffectGpuMs(gpuProfiler_, desc));

            if (open) {
                if (!rows.empty() && ImGui::BeginTable("##rows", 5,
                        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
                    ImGui::TableSetupColumn("行");
                    ImGui::TableSetupColumn("状態");
                    ImGui::TableSetupColumn("解像度");
                    ImGui::TableSetupColumn("レイ");
                    ImGui::TableSetupColumn("補足");
                    ImGui::TableHeadersRow();
                    for (const RTDebugEffectRow& row : rows) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(row.label.c_str());
                        ImGui::TableNextColumn();
                        ImGui::TextColored(row.info.WasDispatched() ? kColorOk : kColorStopped, "%s", ToString(row.info.status));
                        ImGui::TableNextColumn();
                        ImGui::Text("%u×%u", row.info.width, row.info.height);
                        ImGui::TableNextColumn();
                        ImGui::Text("%.2f M", static_cast<double>(row.info.rayCount) / 1.0e6);
                        ImGui::TableNextColumn();
                        std::string extras;
                        for (uint32_t e = 0; e < row.info.extraCount; ++e) {
                            extras += std::format("{}{}={:.4g}", extras.empty() ? "" : "  ",
                                row.info.extras[e].label ? row.info.extras[e].label : "?", row.info.extras[e].value);
                        }
                        ImGui::TextDisabled("%s", extras.c_str());
                    }
                    ImGui::EndTable();
                }

                if (desc.passNames[0] && ImGui::BeginTable("##stages", 2,
                        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
                    for (uint32_t s = 0; s < 4; ++s) {
                        if (!desc.passNames[s]) {
                            break;
                        }
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::Text("段：%s", desc.stageLabels[s] ? desc.stageLabels[s] : desc.passNames[s]);
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s", desc.passNames[s]);
                        }
                        ImGui::TableNextColumn();
                        DrawGpuMs(FindPassGpuMs(gpuProfiler_, desc.passNames[s]));
                    }
                    ImGui::EndTable();
                }

                if (desc.cvarPrefix) {
                    if (ImGui::TreeNode("##settings", "設定（%s）", desc.cvarPrefix)) {
                        CVarUI::DrawTree(desc.cvarPrefix);
                        if (ImGui::Button("既定値へ戻す")) {
                            CVarUI::ResetTree(desc.cvarPrefix);
                        }
                        ImGui::TreePop();
                    }
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        ImGui::Spacing();
        const float asBuildMs = FindPassGpuMs(gpuProfiler_, "ASBuild");
        ImGui::Text("加速構造を作る（ASBuild）");
        ImGui::SameLine(gpuX);
        DrawGpuMs(asBuildMs);
        const float frameMs = FindPassGpuMs(gpuProfiler_, "Frame Total");
        if (frameMs > 0.0f) {
            float totalMs = (std::max)(asBuildMs, 0.0f);
            for (const RTDebugRegistry::EffectEntry& entry : effects) {
                totalMs += (std::max)(EffectGpuMs(gpuProfiler_, entry.desc), 0.0f);
            }
            ImGui::TextDisabled("RT の合計はフレームの %.1f%%（%.3f / %.3f ms）", totalMs / frameMs * 100.0f, totalMs, frameMs);
        }
    }

    // -------------------------------------------------------------------------
    // 中間結果
    // -------------------------------------------------------------------------
    void RayTracingDebugPanel::ApplyChannelDefaults(const RTDebugChannelDesc& channel, ViewSettings& settings) const
    {
        settings.mode = channel.viewMode;
        settings.component = (std::min)(channel.component, 3u);
        settings.rangeMin = channel.rangeMin;
        settings.rangeMax = channel.rangeMax;
        settings.alphaMarksMiss = channel.alphaMarksMiss;
        uint32_t mask = 0;
        for (uint32_t c = 0; c < 3; ++c) {
            if (channel.components[c]) {
                mask |= 1u << c;
            }
        }
        settings.componentMask = mask != 0 ? mask : 0x7u;
    }

    RTDebugViewRequest RayTracingDebugPanel::MakeViewRequest(int channel, uint32_t variant, const ViewSettings& settings) const
    {
        RTDebugViewRequest request;
        request.channel = channel;
        request.variant = variant;
        request.mode = settings.mode;
        request.component = settings.component;
        request.componentMask = settings.componentMask;
        request.rangeMin = settings.rangeMin;
        request.rangeMax = settings.rangeMax;
        request.alphaMarksMiss = settings.alphaMarksMiss;
        request.probeEnabled = probeEnabled_;
        request.probeX = probeX_;
        request.probeY = probeY_;
        return request;
    }

    void RayTracingDebugPanel::DrawOutputTarget(bool screenSpace)
    {
        if (!screenSpace) {
            ImGui::TextDisabled("出す先：この窓（画面の並びではないので、ゲーム画面へは重ねない）");
            if (outputTarget_ != OutputTarget::Window) {
                outputTarget_ = OutputTarget::Window;
            }
            return;
        }
        int target = static_cast<int>(outputTarget_);
        ImGui::TextUnformatted("出す先：");
        ImGui::SameLine();
        ImGui::RadioButton("この窓", &target, static_cast<int>(OutputTarget::Window));
        ImGui::SameLine();
        ImGui::RadioButton("ゲーム画面いっぱい", &target, static_cast<int>(OutputTarget::Fullscreen));
        ImGui::SameLine();
        ImGui::RadioButton("左右に分ける", &target, static_cast<int>(OutputTarget::Split));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("ゲーム画面の左半分に中間結果、右半分に仕上がりを出す");
        }
        outputTarget_ = static_cast<OutputTarget>(target);
    }

    void RayTracingDebugPanel::DrawViewSettings(const RTDebugChannelDesc& channel, ViewSettings& settings,
        const RTDebugImageStats& stats)
    {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
        if (ImGui::BeginCombo("色の付け方", ToDisplayName(settings.mode))) {
            for (uint32_t m = 0; m < static_cast<uint32_t>(RTDebugViewMode::Count); ++m) {
                const RTDebugViewMode mode = static_cast<RTDebugViewMode>(m);
                if (ImGui::Selectable(ToDisplayName(mode), mode == settings.mode)) {
                    settings.mode = mode;
                }
            }
            ImGui::EndCombo();
        }

        bool anyDeclared = false;
        for (const char* component : channel.components) {
            anyDeclared = anyDeclared || component != nullptr;
        }
        auto componentLabel = [&](uint32_t c) {
            return channel.components[c] ? std::format("{}：{}", kComponentLetters[c], channel.components[c])
                                         : std::string(kComponentLetters[c]);
        };
        if (UsesSingleComponent(settings.mode)) {
            int component = static_cast<int>(settings.component);
            for (uint32_t c = 0; c < 4; ++c) {
                if (anyDeclared && !channel.components[c]) {
                    continue;
                }
                if (c > 0) {
                    ImGui::SameLine();
                }
                ImGui::RadioButton(componentLabel(c).c_str(), &component, static_cast<int>(c));
            }
            settings.component = static_cast<uint32_t>(component);
        } else if (settings.mode == RTDebugViewMode::Color) {
            for (uint32_t c = 0; c < 4; ++c) {
                if (anyDeclared && !channel.components[c]) {
                    continue;
                }
                if (c > 0) {
                    ImGui::SameLine();
                }
                ImGui::CheckboxFlags(componentLabel(c).c_str(), &settings.componentMask, 1u << c);
            }
        }

        const float span = std::abs(settings.rangeMax - settings.rangeMin);
        const float speed = (std::max)(span * 0.005f, 0.0001f);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
        ImGui::DragFloatRange2("範囲", &settings.rangeMin, &settings.rangeMax, speed, 0.0f, 0.0f, "%.4g", "%.4g");
        if (settings.mode == RTDebugViewMode::Signed && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("正負：絶対値が下限以下は黒・上限で色が飽和する");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!stats.valid || !stats.hasRange);
        if (ImGui::Button("見えている値に合わせる")) {
            settings.rangeMin = stats.minValue;
            settings.rangeMax = (stats.maxValue > stats.minValue) ? stats.maxValue : stats.minValue + 1.0e-3f;
            if (settings.mode == RTDebugViewMode::Signed) {
                settings.rangeMin = 0.0f;
                settings.rangeMax = (std::max)(std::abs(stats.minValue), std::abs(stats.maxValue));
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("既定")) {
            ApplyChannelDefaults(channel, settings);
        }
        ImGui::Checkbox("A = 0 の所を当たっていない所として模様で出す", &settings.alphaMarksMiss);
    }

    bool RayTracingDebugPanel::DrawPreview(RTDebugSystem& debug, int channel, uint32_t& outX, uint32_t& outY)
    {
        const uint64_t frame = engine_ && engine_->GetService<GraphicsCore>()
            ? engine_->GetService<GraphicsCore>()->Frame().FrameNumber() : 0;
        const D3D12_GPU_DESCRIPTOR_HANDLE srv = debug.GetViewSRV();
        const uint32_t width = debug.GetViewWidth();
        const uint32_t height = debug.GetViewHeight();
        if (debug.GetViewChannel() != channel || !IsFresh(debug.GetViewFrame(), frame) || srv.ptr == 0
            || width == 0 || height == 0) {
            ImGui::TextDisabled("今は作っていない（効果がこのフレームに撃っていないか、結果がまだ届いていない）");
            return false;
        }
        const float availW = ImGui::GetContentRegionAvail().x;
        const float availH = (std::max)(ImGui::GetContentRegionAvail().y - ImGui::GetFontSize() * 6.0f, 64.0f);
        float drawW = availW;
        float drawH = drawW * static_cast<float>(height) / static_cast<float>(width);
        if (drawH > availH) {
            drawH = availH;
            drawW = drawH * static_cast<float>(width) / static_cast<float>(height);
        }
        ImGui::Image((ImTextureID)srv.ptr, ImVec2(drawW, drawH));
        if (!ImGui::IsItemHovered()) {
            return false;
        }
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const float u = std::clamp((mouse.x - min.x) / drawW, 0.0f, 1.0f);
        const float v = std::clamp((mouse.y - min.y) / drawH, 0.0f, 1.0f);
        outX = (std::min)(static_cast<uint32_t>(u * static_cast<float>(width)), width - 1);
        outY = (std::min)(static_cast<uint32_t>(v * static_cast<float>(height)), height - 1);
        return true;
    }

    void RayTracingDebugPanel::DrawProbe(const RTDebugChannelDesc& channel, const RTDebugImageStats& stats)
    {
        if (!stats.valid || !stats.probeValid) {
            ImGui::TextDisabled("画像の上にマウスを置くと、その画素の生の値が出る");
            return;
        }
        bool anyDeclared = false;
        for (const char* component : channel.components) {
            anyDeclared = anyDeclared || component != nullptr;
        }
        std::string text = std::format("画素 ({}, {})", stats.probeX, stats.probeY);
        for (uint32_t c = 0; c < 4; ++c) {
            if (anyDeclared && !channel.components[c]) {
                continue;
            }
            text += std::format("   {}={:.6g}", kComponentLetters[c], stats.probe[c]);
        }
        ImGui::TextUnformatted(text.c_str());
    }

    void RayTracingDebugPanel::DrawChannelsTab(RTDebugSystem& debug)
    {
        const RTDebugRegistry& registry = RTDebugRegistry::Get();
        const auto& channels = registry.Channels();
        if (channels.empty()) {
            ImGui::TextDisabled("申告された中間結果がありません");
            return;
        }

        // 左：効果 → 段 → 中身の木
        const float treeWidth = ImGui::GetFontSize() * 13.0f;
        ImGui::BeginChild("##ChannelTree", ImVec2(treeWidth, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        std::vector<const char*> effectsSeen;
        for (const RTDebugChannelDesc& effectChannel : channels) {
            if (std::find_if(effectsSeen.begin(), effectsSeen.end(),
                    [&](const char* e) { return std::strcmp(e, effectChannel.effect) == 0; }) != effectsSeen.end()) {
                continue;
            }
            effectsSeen.push_back(effectChannel.effect);
            if (!ImGui::TreeNodeEx(effectChannel.effect, ImGuiTreeNodeFlags_DefaultOpen)) {
                continue;
            }
            std::vector<const char*> stagesSeen;
            for (const RTDebugChannelDesc& stageChannel : channels) {
                if (std::strcmp(stageChannel.effect, effectChannel.effect) != 0) {
                    continue;
                }
                if (std::find_if(stagesSeen.begin(), stagesSeen.end(),
                        [&](const char* s) { return std::strcmp(s, stageChannel.stage) == 0; }) != stagesSeen.end()) {
                    continue;
                }
                stagesSeen.push_back(stageChannel.stage);
                if (!ImGui::TreeNodeEx(stageChannel.stage, ImGuiTreeNodeFlags_DefaultOpen)) {
                    continue;
                }
                for (size_t i = 0; i < channels.size(); ++i) {
                    const RTDebugChannelDesc& channel = channels[i];
                    if (std::strcmp(channel.effect, effectChannel.effect) != 0
                        || std::strcmp(channel.stage, stageChannel.stage) != 0) {
                        continue;
                    }
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::Selectable(channel.name, selectedChannel_ == static_cast<int>(i))) {
                        selectedChannel_ = static_cast<int>(i);
                        selectedVariant_ = 0;
                        ApplyChannelDefaults(channel, channelView_);
                    }
                    if (channel.description[0] != '\0' && ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", channel.description);
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("##ChannelView", ImVec2(0.0f, 0.0f));
        if (selectedChannel_ < 0 || static_cast<size_t>(selectedChannel_) >= channels.size()) {
            ImGui::TextDisabled("左の木から中間結果を選ぶ");
            ImGui::EndChild();
            return;
        }
        const RTDebugChannelDesc& channel = channels[static_cast<size_t>(selectedChannel_)];
        ImGui::Text("%s / %s / %s", channel.effect, channel.stage, channel.name);
        if (channel.description[0] != '\0') {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", channel.description);
            ImGui::PopTextWrapPos();
        }
        if (channel.variantCount > 1) {
            int variant = static_cast<int>(selectedVariant_);
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
            ImGui::SliderInt(channel.variantLabel[0] != '\0' ? channel.variantLabel : "番号", &variant, 0,
                static_cast<int>(channel.variantCount) - 1);
            selectedVariant_ = static_cast<uint32_t>(std::clamp(variant, 0, static_cast<int>(channel.variantCount) - 1));
        }
        DrawOutputTarget(channel.screenSpace);

        const RTDebugImageStats& stats = debug.GetViewStats();
        const bool statsMatch = stats.valid && stats.channel == selectedChannel_ && stats.variant == selectedVariant_;
        DrawViewSettings(channel, channelView_, statsMatch ? stats : RTDebugImageStats{});

        if (statsMatch) {
            if (stats.hasRange) {
                ImGui::Text("最小 %.5g ・ 最大 %.5g", stats.minValue, stats.maxValue);
            } else {
                ImGui::TextDisabled("有限の値がない");
            }
            ImGui::SameLine();
            if (stats.nanCount > 0 || stats.infCount > 0) {
                ImGui::TextColored(kColorError, "NaN %u 画素・Inf %u 画素", stats.nanCount, stats.infCount);
            } else {
                ImGui::TextColored(kColorOk, "NaN と Inf なし");
            }
            ImGui::SameLine();
            ImGui::TextDisabled("（%u×%u）", stats.width, stats.height);
        }

        uint32_t x = 0;
        uint32_t y = 0;
        if (DrawPreview(debug, selectedChannel_, x, y)) {
            probeEnabled_ = true;
            probeX_ = x;
            probeY_ = y;
        }
        if (statsMatch) {
            DrawProbe(channel, stats);
        }
        ImGui::TextDisabled("紫 = NaN・白 = +Inf・水色 = -Inf");
        ImGui::EndChild();
    }

    // -------------------------------------------------------------------------
    // 見え方
    // -------------------------------------------------------------------------
    void RayTracingDebugPanel::DrawSceneTab(const RTDebugQuery& query, RTDebugSystem& debug)
    {
        const RTDebugRegistry& registry = RTDebugRegistry::Get();
        const int sceneChannel = registry.FindChannel(kSceneChannelEffect, kSceneChannelName);
        if (sceneChannel < 0) {
            ImGui::TextDisabled("見え方の中間結果が申告されていません");
            return;
        }
        const RTDebugChannelDesc& channel = registry.Channels()[static_cast<size_t>(sceneChannel)];

        auto applyMode = [this](RTDebugSceneMode mode) {
            sceneRequest_.mode = mode;
            const RTDebugSceneModeInfo info = GetSceneModeInfo(mode);
            sceneView_.mode = info.viewMode;
            sceneView_.rangeMin = info.rangeMin;
            sceneView_.rangeMax = info.rangeMax;
            sceneView_.component = 0;
            sceneView_.componentMask = 0x7;
            sceneView_.alphaMarksMiss = true;
        };
        if (!sceneViewInitialized_) {
            applyMode(sceneRequest_.mode);
            sceneViewInitialized_ = true;
        }

        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
        if (ImGui::BeginCombo("見る値", ToDisplayName(sceneRequest_.mode))) {
            for (uint32_t m = 0; m < static_cast<uint32_t>(RTDebugSceneMode::Count); ++m) {
                const RTDebugSceneMode mode = static_cast<RTDebugSceneMode>(m);
                if (ImGui::Selectable(ToDisplayName(mode), mode == sceneRequest_.mode)) {
                    applyMode(mode);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", GetSceneModeInfo(mode).description);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", GetSceneModeInfo(sceneRequest_.mode).description);
        ImGui::PopTextWrapPos();

        ImGui::TextUnformatted("当てる物：");
        for (const MaskBit& bit : kMaskBits) {
            ImGui::SameLine();
            ImGui::CheckboxFlags(bit.name, &sceneRequest_.instanceMask, bit.bit);
        }
        ImGui::SameLine();
        ImGui::Checkbox("アルファで抜ける所を通す", &sceneRequest_.alphaTest);

        DrawOutputTarget(true);
        const RTDebugImageStats& stats = debug.GetViewStats();
        const bool statsMatch = stats.valid && stats.channel == sceneChannel;
        DrawViewSettings(channel, sceneView_, statsMatch ? stats : RTDebugImageStats{});

        const RTDebugSceneStats& sceneStats = debug.GetSceneStats();
        if (sceneStats.valid && sceneStats.pixels > 0) {
            const double pixels = static_cast<double>(sceneStats.pixels);
            ImGui::Text("当たった画素 %.1f%%", static_cast<double>(sceneStats.hitPixels) / pixels * 100.0);
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::Text("アルファを調べた数 平均 %.2f / 画素", static_cast<double>(sceneStats.alphaTests) / pixels);
            if (sceneStats.mode == RTDebugSceneMode::Candidates) {
                ImGui::SameLine();
                ImGui::TextDisabled("|");
                ImGui::SameLine();
                ImGui::Text("三角形の候補 平均 %.2f / 画素", static_cast<double>(sceneStats.candidates) / pixels);
            }
        }

        uint32_t x = 0;
        uint32_t y = 0;
        if (DrawPreview(debug, sceneChannel, x, y)) {
            probeEnabled_ = true;
            probeX_ = x;
            probeY_ = y;
        }

        // 画素の値を見え方に合わせて読む
        if (!statsMatch || !stats.probeValid) {
            ImGui::TextDisabled("画像の上にマウスを置くと、その画素で当たった物が出る");
            return;
        }
        const float* p = stats.probe;
        std::string text = std::format("画素 ({}, {})  ", stats.probeX, stats.probeY);
        RTScene* scene = query.domain ? query.domain->GetRTScene() : nullptr;
        auto rowText = [&](float value) {
            const size_t row = static_cast<size_t>((std::max)(value, 0.0f) + 0.5f);
            if (!scene || row >= scene->GetRows().size() || row >= scene->GetInstances().size()) {
                return std::format("行 {}", row);
            }
            const RTScene::RowRecord& record = scene->GetRows()[row];
            return std::format("行 {}：{}（{}・印 {}）", row, ObjectNameOf(query.objects, record.owner),
                record.resource ? FileNameOf(record.resource->GetFilePath()) : std::string("?"),
                MaskLetters(scene->GetInstances()[row].instanceMask));
        };
        if (p[3] == 0.0f && sceneRequest_.mode != RTDebugSceneMode::Candidates
            && sceneRequest_.mode != RTDebugSceneMode::AlphaTests) {
            text += "当たっていない";
        } else {
            switch (sceneRequest_.mode) {
            case RTDebugSceneMode::Instance:
                text += rowText(p[0]);
                break;
            case RTDebugSceneMode::SubMesh:
                text += std::format("サブメッシュ {}  {}", static_cast<int>(p[0] + 0.5f), rowText(p[1]));
                break;
            case RTDebugSceneMode::Triangle:
                text += std::format("三角形 {}・サブメッシュ {}  {}", static_cast<int>(p[0] + 0.5f),
                    static_cast<int>(p[1] + 0.5f), rowText(p[2]));
                break;
            case RTDebugSceneMode::Distance:
                text += std::format("距離 {:.3f} m", p[0]);
                break;
            case RTDebugSceneMode::Normal:
                text += std::format("法線 ({:.3f}, {:.3f}, {:.3f})", p[0], p[1], p[2]);
                break;
            case RTDebugSceneMode::BaseColor:
                text += std::format("ベースカラー ({:.3f}, {:.3f}, {:.3f})", p[0], p[1], p[2]);
                break;
            case RTDebugSceneMode::Mask:
                text += std::format("印 {}", MaskLetters(static_cast<uint32_t>(p[0] + 0.5f)));
                break;
            case RTDebugSceneMode::FrontFace:
                text += p[0] > 0.0f ? "表から当たった" : "裏から当たった";
                break;
            case RTDebugSceneMode::Motion:
                text += p[0] < 0.5f ? "止まっている" : (p[0] < 1.5f ? "動く形の決まった物" : "毎フレーム BLAS を作る物");
                break;
            case RTDebugSceneMode::DepthDifference:
                text += std::format("差 {:+.4f} m（ラスタ {:.3f} m・RT {:.3f} m）", p[0], p[1], p[2]);
                break;
            case RTDebugSceneMode::Candidates:
                text += std::format("三角形の候補 {}", static_cast<int>(p[0] + 0.5f));
                break;
            case RTDebugSceneMode::AlphaTests:
                text += std::format("アルファを調べた数 {}", static_cast<int>(p[0] + 0.5f));
                break;
            default:
                break;
            }
        }
        ImGui::TextUnformatted(text.c_str());
    }

    // -------------------------------------------------------------------------
    // レイの追跡
    // -------------------------------------------------------------------------
    void RayTracingDebugPanel::DrawTraceTab(const RTDebugQuery& query, RTDebugSystem& debug)
    {
        traceActive_ = true;
        const int clientW = query.graphics ? (std::max)(query.graphics->GetClientWidth(), 1) : 1;
        const int clientH = query.graphics ? (std::max)(query.graphics->GetClientHeight(), 1) : 1;

        int pixel[2] = { traceX_, traceY_ };
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
        if (ImGui::InputInt2("画素", pixel)) {
            traceHold_ = false;
        }
        traceX_ = std::clamp(pixel[0], 0, clientW - 1);
        traceY_ = std::clamp(pixel[1], 0, clientH - 1);
        ImGui::SameLine();
        if (ImGui::Button(tracePicking_ ? "ゲーム画面をクリック…" : "ゲーム画面で選ぶ")) {
            tracePicking_ = !tracePicking_;
        }
        if (tracePicking_ && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            tracePicking_ = false;
        }
        ImGui::SameLine();
        ImGui::Checkbox("マウスの下を追う", &traceFollowMouse_);
        ImGui::Checkbox("止める（今の結果を残す）", &traceHold_);
        ImGui::SameLine();
        ImGui::Checkbox("3D の線で描く", &drawTraceLines_);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("白 = カメラのレイ・ほかは表の左の印と同じ効果ごとの色。当たったレイは当たった所で止めて十字を付ける");
        }

        if (!traceHold_) {
            debug.RequestTrace(static_cast<uint32_t>(traceX_), static_cast<uint32_t>(traceY_));
        }

        const RTDebugTraceResult& result = debug.GetTraceResult();
        if (!result.valid) {
            ImGui::TextDisabled("まだ結果が無い（TLAS ができていないか、結果がまだ届いていない）");
            return;
        }
        ImGui::Text("画素 (%u, %u) の結果", result.x, result.y);
        ImGui::SameLine();
        ImGui::TextDisabled("（%llu フレーム前）", static_cast<unsigned long long>(query.frame > result.frame ? query.frame - result.frame : 0));
        if (result.hit) {
            ImGui::Text("当たった所 (%.3f, %.3f, %.3f)・面の法線 (%.3f, %.3f, %.3f)",
                result.hitPosition.x, result.hitPosition.y, result.hitPosition.z,
                result.hitNormal.x, result.hitNormal.y, result.hitNormal.z);
            ImGui::ColorButton("##baseColor", ImVec4(result.baseColor[0], result.baseColor[1], result.baseColor[2], 1.0f),
                ImGuiColorEditFlags_NoTooltip, ImVec2(ImGui::GetFontSize(), ImGui::GetFontSize()));
            ImGui::SameLine();
            ImGui::Text("ベースカラー (%.3f, %.3f, %.3f, %.3f)・UV (%.4f, %.4f)",
                result.baseColor[0], result.baseColor[1], result.baseColor[2], result.baseColor[3],
                result.uv[0], result.uv[1]);
        } else {
            ImGui::TextDisabled("カメラのレイは何にも当たっていない（空）");
        }

        RTScene* scene = query.domain ? query.domain->GetRTScene() : nullptr;
        const RTDebugRayRecordResult& record = debug.GetRayRecordResult();
        const bool recordValid = record.valid;
        if (recordValid) {
            // 記録に無いディスパッチを選んでいたら全部に戻す
            if (traceDispatchFilter_ >= static_cast<int>(record.dispatches.size())) {
                traceDispatchFilter_ = -1;
            }
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
            const char* preview = traceDispatchFilter_ < 0
                ? "全部の効果" : record.dispatches[static_cast<size_t>(traceDispatchFilter_)].label.c_str();
            if (ImGui::BeginCombo("効果", preview)) {
                if (ImGui::Selectable("全部の効果", traceDispatchFilter_ < 0)) {
                    traceDispatchFilter_ = -1;
                }
                for (size_t d = 0; d < record.dispatches.size(); ++d) {
                    const std::string label = std::format("{}##dispatch{}", record.dispatches[d].label, d);
                    if (ImGui::Selectable(label.c_str(), traceDispatchFilter_ == static_cast<int>(d))) {
                        traceDispatchFilter_ = static_cast<int>(d);
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("効果のレイ %u 本・値 %u 個", record.rayTotal, record.printTotal);
            if (record.rayTotal > record.rays.size()) {
                ImGui::TextColored(kColorWarning, "撃ったレイ %u 本のうち、先頭の %zu 本だけ記録した",
                    record.rayTotal, record.rays.size());
            }
        }
        auto shown = [this](uint32_t dispatch) {
            return traceDispatchFilter_ < 0 || static_cast<uint32_t>(traceDispatchFilter_) == dispatch;
        };

        const float swatch = ImGui::GetFontSize() * 0.8f;
        size_t shownRays = 0;
        if (recordValid) {
            for (const RTDebugRecordedRay& ray : record.rays) {
                shownRays += shown(ray.dispatch) ? 1 : 0;
            }
        }
        const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
        const ImVec2 tableSize(0.0f, shownRays > 12 ? rowHeight * 14.0f : 0.0f);
        ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders;
        if (shownRays > 12) {
            tableFlags |= ImGuiTableFlags_ScrollY;
        }
        if (ImGui::BeginTable("##TracedRays", 10, tableFlags, tableSize)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("効果");
            ImGui::TableSetupColumn("撃ち方");
            ImGui::TableSetupColumn("結果");
            ImGui::TableSetupColumn("距離");
            ImGui::TableSetupColumn("物");
            ImGui::TableSetupColumn("サブメッシュ");
            ImGui::TableSetupColumn("三角形");
            ImGui::TableSetupColumn("面");
            ImGui::TableSetupColumn("印");
            ImGui::TableSetupColumn("アルファ");
            ImGui::TableHeadersRow();

            // カメラのレイ（デバッグ窓が撃つ）
            {
                const RTDebugTracedRay& ray = result.cameraRay;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::ColorButton("##cameraColor", ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
                    ImGuiColorEditFlags_NoTooltip, ImVec2(swatch, swatch));
                ImGui::SameLine();
                ImGui::TextUnformatted("カメラ（この窓）");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("始点 (%.3f, %.3f, %.3f)\n向き (%.3f, %.3f, %.3f)\n最大 %.1f m\n候補 %u（当たり判定へ来た三角形の数）",
                        ray.origin.x, ray.origin.y, ray.origin.z, ray.direction.x, ray.direction.y, ray.direction.z,
                        ray.tMax, ray.candidates);
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted("いちばん近い所");
                ImGui::TableNextColumn();
                ray.hit ? ImGui::TextColored(kColorOk, "当たった") : ImGui::TextDisabled("抜けた");
                ImGui::TableNextColumn();
                ray.hit ? ImGui::Text("%.3f m", ray.hitT) : ImGui::TextDisabled("--");
                ImGui::TableNextColumn();
                DrawHitObjectCell(query, scene, ray.hit, ray.row);
                ImGui::TableNextColumn();
                ray.hit ? ImGui::Text("%u", ray.geometry) : ImGui::TextDisabled("--");
                ImGui::TableNextColumn();
                ray.hit ? ImGui::Text("%u", ray.primitive) : ImGui::TextDisabled("--");
                ImGui::TableNextColumn();
                ray.hit ? ImGui::TextUnformatted(ray.frontFace ? "表" : "裏") : ImGui::TextDisabled("--");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(MaskLetters(ray.instanceMask).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%u", ray.alphaTests);
            }

            // 効果が撃ったレイ（記録版のシェーダが書いたもの）
            if (recordValid) {
                int rowId = 0;
                for (const RTDebugRecordedRay& ray : record.rays) {
                    if (!shown(ray.dispatch)) {
                        continue;
                    }
                    ImGui::PushID(rowId++);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    const Vector3 color = DispatchColor(record, ray.dispatch);
                    ImGui::ColorButton("##color", ImVec4(color.x, color.y, color.z, 1.0f),
                        ImGuiColorEditFlags_NoTooltip, ImVec2(swatch, swatch));
                    ImGui::SameLine();
                    ImGui::TextUnformatted(DispatchLabel(record, ray.dispatch));
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s・撃った画素 (%u, %u)\n始点 (%.3f, %.3f, %.3f)\n向き (%.3f, %.3f, %.3f)\n範囲 %.3f〜%.3f m・フラグ 0x%X",
                            DispatchShader(record, ray.dispatch), ray.traceX, ray.traceY,
                            ray.origin.x, ray.origin.y, ray.origin.z, ray.direction.x, ray.direction.y, ray.direction.z,
                            ray.tMin, ray.tMax, ray.rayFlags);
                    }
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(RayFlagsText(ray.rayFlags).c_str());
                    ImGui::TableNextColumn();
                    if (ray.hit) {
                        // 遮られるかを見るレイは、当たった = 遮られた
                        const bool blocked = (ray.rayFlags & kRayFlagAcceptFirstHit) != 0;
                        ImGui::TextColored(blocked ? kColorWarning : kColorOk, blocked ? "遮られた" : "当たった");
                    } else {
                        ImGui::TextDisabled("抜けた");
                    }
                    ImGui::TableNextColumn();
                    ray.hit ? ImGui::Text("%.3f m", ray.hitT) : ImGui::TextDisabled("--");
                    ImGui::TableNextColumn();
                    DrawHitObjectCell(query, scene, ray.hit, ray.row);
                    ImGui::TableNextColumn();
                    ray.hit ? ImGui::Text("%u", ray.geometry) : ImGui::TextDisabled("--");
                    ImGui::TableNextColumn();
                    ray.hit ? ImGui::Text("%u", ray.primitive) : ImGui::TextDisabled("--");
                    ImGui::TableNextColumn();
                    ray.hit ? ImGui::TextUnformatted(ray.frontFace ? "表" : "裏") : ImGui::TextDisabled("--");
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(MaskLetters(ray.instanceMask).c_str());
                    ImGui::TableNextColumn();
                    if (ray.cutouts > 0) {
                        ImGui::Text("%u（抜け %u）", ray.alphaTests, ray.cutouts);
                    } else {
                        ImGui::Text("%u", ray.alphaTests);
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
        if (!recordValid || record.rays.empty()) {
            ImGui::TextDisabled("効果のレイはまだ無い（この画素を照らす効果が動いていないか、結果がまだ届いていない）");
        }
        ImGui::TextDisabled("アルファ = アルファで抜けるかを調べた候補の数（抜け = そのうち通り抜けた数）");
        ImGui::TextDisabled("効果のレイは記録版のシェーダで撃つ。初めて追跡するときは記録版を作るので少し止まる");

        // RT_DEBUG_PRINT の値
        SectionHeader("シェーダの値（RT_DEBUG_PRINT）");
        size_t shownPrints = 0;
        if (recordValid) {
            for (const RTDebugPrintedValue& value : record.prints) {
                shownPrints += shown(value.dispatch) ? 1 : 0;
            }
        }
        if (shownPrints == 0) {
            ImGui::TextDisabled("なし（効果のシェーダに RT_DEBUG_PRINT(値); と書くと、この画素で書いた値がここに出る）");
            return;
        }
        if (record.printTotal > record.prints.size()) {
            ImGui::TextColored(kColorWarning, "書いた値 %u 個のうち、先頭の %zu 個だけ記録した",
                record.printTotal, record.prints.size());
        }
        if (ImGui::BeginTable("##PrintedValues", 3,
                ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("効果");
            ImGui::TableSetupColumn("書いた所");
            ImGui::TableSetupColumn("値");
            ImGui::TableHeadersRow();
            for (const RTDebugPrintedValue& value : record.prints) {
                if (!shown(value.dispatch)) {
                    continue;
                }
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(DispatchLabel(record, value.dispatch));
                ImGui::TableNextColumn();
                ImGui::Text("%s : %u 行", DispatchShader(record, value.dispatch), value.line);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("書いた画素（効果のトレース解像度）(%u, %u)", value.traceX, value.traceY);
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(FormatPrintedValue(value).c_str());
            }
            ImGui::EndTable();
        }
    }

    // -------------------------------------------------------------------------
    // 加速構造
    // -------------------------------------------------------------------------
    void RayTracingDebugPanel::DrawAccelerationTab(const RTDebugQuery& query)
    {
        AccelerationStructureManager* asMgr = query.domain ? query.domain->GetAccelerationStructureManager() : nullptr;
        RTScene* scene = query.domain ? query.domain->GetRTScene() : nullptr;
        if (!asMgr || !asMgr->IsSupported()) {
            ImGui::TextDisabled("DXR を使っていません");
            return;
        }

        if (ImGui::BeginTable("##ASStats", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
            auto cell = [](const char* label, const std::string& value) {
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", label);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(value.c_str());
            };
            ImGui::TableNextRow();
            cell("BLAS（モデル）", std::to_string(asMgr->GetBLASCount()));
            cell("BLAS 合計", FormatBytes(asMgr->GetBLASTotalBytes()));
            ImGui::TableNextRow();
            cell("BLAS（動く形）", std::to_string(asMgr->GetDynamicBLASCount()));
            cell("TLAS", FormatBytes(asMgr->GetTLASResultBytes()));
            ImGui::TableNextRow();
            cell("解放した BLAS", std::to_string(asMgr->GetReleasedBLASCount()));
            cell("スクラッチ", FormatBytes(asMgr->GetScratchTotalBytes()));
            ImGui::TableNextRow();
            cell("形の LOD", scene ? std::to_string(scene->GetGeometryLod()) : std::string("--"));
            cell("解放待ち", std::to_string(asMgr->GetRetiredResourceCount()));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("ASBuild");
            ImGui::TableNextColumn();
            DrawGpuMs(FindPassGpuMs(gpuProfiler_, "ASBuild"));
            ImGui::EndTable();
        }
        if (!scene) {
            return;
        }

        // 出どころごとの数
        std::string sources;
        for (const RTScene::SourceStats& source : scene->GetSourceStats()) {
            sources += std::format("{}{} {}", sources.empty() ? "" : "・", source.name, source.instanceCount);
        }
        ImGui::Text("TLAS の行 %zu（%s）", scene->GetInstances().size(), sources.c_str());

        // 絞り込み
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
        ImGui::InputTextWithHint("##filter", "物・モデルの名前", accelFilter_, sizeof(accelFilter_));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
        const auto& sourceStats = scene->GetSourceStats();
        const char* sourcePreview = (accelSource_ >= 0 && static_cast<size_t>(accelSource_) < sourceStats.size())
            ? sourceStats[static_cast<size_t>(accelSource_)].name : "出どころ：全部";
        if (ImGui::BeginCombo("##source", sourcePreview)) {
            if (ImGui::Selectable("出どころ：全部", accelSource_ < 0)) {
                accelSource_ = -1;
            }
            for (size_t s = 0; s < sourceStats.size(); ++s) {
                if (ImGui::Selectable(sourceStats[s].name, accelSource_ == static_cast<int>(s))) {
                    accelSource_ = static_cast<int>(s);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
        const char* kShapeNames[] = { "形：全部", "形：静止形", "形：毎フレーム作る" };
        ImGui::Combo("##shape", &accelShape_, kShapeNames, IM_ARRAYSIZE(kShapeNames));
        ImGui::TextUnformatted("印：");
        for (const MaskBit& bit : kMaskBits) {
            ImGui::SameLine();
            const std::string label = std::format("{}##accelMask", bit.name);
            ImGui::CheckboxFlags(label.c_str(), &accelMask_, bit.bit);
        }
        ImGui::SameLine();
        ImGui::Checkbox("選んだ物の箱を描く", &drawSelectedBox_);

        const auto& instances = scene->GetInstances();
        const auto& rows = scene->GetRows();
        const size_t rowCount = (std::min)(instances.size(), rows.size());
        const std::string filter = accelFilter_;
        std::vector<uint32_t> visible;
        visible.reserve(rowCount);
        for (size_t i = 0; i < rowCount; ++i) {
            const RTScene::RowRecord& record = rows[i];
            if (accelSource_ >= 0 && record.source != static_cast<uint32_t>(accelSource_)) {
                continue;
            }
            if (accelShape_ == 1 && record.dynamicOwner) {
                continue;
            }
            if (accelShape_ == 2 && !record.dynamicOwner) {
                continue;
            }
            if ((instances[i].instanceMask & accelMask_) == 0) {
                continue;
            }
            if (!filter.empty()) {
                const std::string name = ObjectNameOf(query.objects, record.owner);
                const std::string model = record.resource ? FileNameOf(record.resource->GetFilePath()) : std::string();
                if (name.find(filter) == std::string::npos && model.find(filter) == std::string::npos) {
                    continue;
                }
            }
            visible.push_back(static_cast<uint32_t>(i));
        }
        if (selectedRow_ >= static_cast<int>(rowCount)) {
            selectedRow_ = -1;
        }

        ImGui::TextDisabled("%zu / %zu 行", visible.size(), rowCount);
        const ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders
            | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
        if (ImGui::BeginTable("##ASRows", 8, flags, ImVec2(0.0f, ImGui::GetContentRegionAvail().y))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("行");
            ImGui::TableSetupColumn("物");
            ImGui::TableSetupColumn("モデル");
            ImGui::TableSetupColumn("出どころ");
            ImGui::TableSetupColumn("印");
            ImGui::TableSetupColumn("形");
            ImGui::TableSetupColumn("三角形");
            ImGui::TableSetupColumn("BLAS");
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(visible.size()));
            while (clipper.Step()) {
                for (int v = clipper.DisplayStart; v < clipper.DisplayEnd; ++v) {
                    const uint32_t i = visible[static_cast<size_t>(v)];
                    const RTScene::RowRecord& record = rows[i];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    const std::string rowLabel = std::format("{}##row{}", i, i);
                    if (ImGui::Selectable(rowLabel.c_str(), selectedRow_ == static_cast<int>(i),
                            ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                        selectedRow_ = (selectedRow_ == static_cast<int>(i)) ? -1 : static_cast<int>(i);
                    }
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(ObjectNameOf(query.objects, record.owner).c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(record.resource ? FileNameOf(record.resource->GetFilePath()).c_str() : "?");
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(record.source < sourceStats.size() ? sourceStats[record.source].name : "?");
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(MaskLetters(instances[i].instanceMask).c_str());
                    if ((instances[i].flags & D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE) == 0) {
                        ImGui::SameLine();
                        ImGui::TextColored(kColorInfo, "抜");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("アルファで抜ける所を持つ（レイが材質を調べる）");
                        }
                    }
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(record.dynamicOwner ? "毎フレーム" : "静止形");
                    ImGui::TableNextColumn();
                    ImGui::Text("%llu", static_cast<unsigned long long>(RowTriangleCount(*scene, i)));
                    ImGui::TableNextColumn();
                    const uint64_t bytes = record.dynamicOwner ? asMgr->GetDynamicBLASBytes(record.dynamicOwner)
                                                               : asMgr->GetStaticBLASBytes(record.resource);
                    ImGui::TextUnformatted(FormatBytes(bytes).c_str());
                }
            }
            ImGui::EndTable();
        }
    }

    // -------------------------------------------------------------------------
    // チェック
    // -------------------------------------------------------------------------
    void RayTracingDebugPanel::DrawChecksTab([[maybe_unused]] RTDebugSystem& debug)
    {
        ImGui::Checkbox("中間結果を見回る", &scanChannels_);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("1 フレームに 1 枚ずつ、申告された中間結果の NaN と Inf を数える（入れている間だけ少し重い）");
        }
        ImGui::SameLine();
        if (ImGui::Button("今の結果を記録")) {
            recordedLines_ = checkLines_;
            hasRecord_ = true;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!hasRecord_);
        if (ImGui::Button("記録を消す")) {
            recordedLines_.clear();
            hasRecord_ = false;
        }
        ImGui::EndDisabled();
        if (hasRecord_) {
            ImGui::SameLine();
            ImGui::TextDisabled("（記録に無い結果に「新」を付ける）");
        }

        auto sameLine = [](const CheckLine& a, const CheckLine& b) {
            return a.check == b.check && a.severity == b.severity && a.message == b.message;
        };

        ImGui::BeginChild("##CheckLines");
        const std::string* currentCheck = nullptr;
        for (const CheckLine& line : checkLines_) {
            if (!currentCheck || *currentCheck != line.check) {
                currentCheck = &line.check;
                SectionHeader(line.check.c_str());
            }
            ImGui::TextColored(SeverityColor(line.severity), "%s", SeverityLabel(line.severity));
            ImGui::SameLine();
            if (hasRecord_ && std::none_of(recordedLines_.begin(), recordedLines_.end(),
                    [&](const CheckLine& recorded) { return sameLine(recorded, line); })) {
                ImGui::TextColored(kColorWarning, "新");
                ImGui::SameLine();
            }
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(line.message.c_str());
            ImGui::PopTextWrapPos();
        }

        if (hasRecord_) {
            bool headerShown = false;
            for (const CheckLine& recorded : recordedLines_) {
                if (std::any_of(checkLines_.begin(), checkLines_.end(),
                        [&](const CheckLine& line) { return sameLine(recorded, line); })) {
                    continue;
                }
                if (!headerShown) {
                    SectionHeader("記録にあって今は無い");
                    headerShown = true;
                }
                ImGui::TextColored(SeverityColor(recorded.severity), "%s", SeverityLabel(recorded.severity));
                ImGui::SameLine();
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("%s：%s", recorded.check.c_str(), recorded.message.c_str());
                ImGui::PopTextWrapPos();
            }
        }
        ImGui::EndChild();
    }

    // -------------------------------------------------------------------------
    // ゲーム画面へ重ねる
    // -------------------------------------------------------------------------
    bool RayTracingDebugPanel::DrawGameViewportOverlay(const Editor::GameViewportOverlayArgs& args)
    {
        if (!windowOpen_) {
            return false;
        }
        RTDebugSystem* debug = GetDebugSystem();
        GraphicsCore* graphics = engine_ ? engine_->GetService<GraphicsCore>() : nullptr;
        if (!debug || !graphics) {
            return false;
        }
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 size(args.imageMax.x - args.imageMin.x, args.imageMax.y - args.imageMin.y);
        if (size.x <= 0.0f || size.y <= 0.0f) {
            return false;
        }
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const float u = std::clamp((mouse.x - args.imageMin.x) / size.x, 0.0f, 1.0f);
        const float v = std::clamp((mouse.y - args.imageMin.y) / size.y, 0.0f, 1.0f);

        // 中間結果を重ねる
        const RTDebugRegistry& registry = RTDebugRegistry::Get();
        const int viewChannel = debug->GetViewChannel();
        const bool screenSpace = viewChannel >= 0 && static_cast<size_t>(viewChannel) < registry.Channels().size()
            && registry.Channels()[static_cast<size_t>(viewChannel)].screenSpace;
        const D3D12_GPU_DESCRIPTOR_HANDLE srv = debug->GetViewSRV();
        if (outputTarget_ != OutputTarget::Window && viewSource_ != ViewSource::None && screenSpace && srv.ptr != 0
            && IsFresh(debug->GetViewFrame(), graphics->Frame().FrameNumber())) {
            const RTDebugChannelDesc& channel = registry.Channels()[static_cast<size_t>(viewChannel)];
            bool probeSide = args.hovered;
            if (outputTarget_ == OutputTarget::Fullscreen) {
                drawList->AddImage((ImTextureID)srv.ptr, args.imageMin, args.imageMax);
            } else {
                const float midX = args.imageMin.x + size.x * 0.5f;
                drawList->AddImage((ImTextureID)srv.ptr, args.imageMin, ImVec2(midX, args.imageMax.y),
                    ImVec2(0.0f, 0.0f), ImVec2(0.5f, 1.0f));
                drawList->AddLine(ImVec2(midX, args.imageMin.y), ImVec2(midX, args.imageMax.y), IM_COL32(255, 255, 255, 200), 2.0f);
                probeSide = probeSide && u < 0.5f;
            }
            const std::string label = std::format("{} / {}", channel.effect, channel.name);
            drawList->AddText(ImVec2(args.imageMin.x + 8.0f, args.imageMin.y + 6.0f), IM_COL32(255, 255, 255, 230), label.c_str());
            if (probeSide) {
                const uint32_t width = debug->GetViewWidth();
                const uint32_t height = debug->GetViewHeight();
                if (width > 0 && height > 0) {
                    probeEnabled_ = true;
                    probeX_ = (std::min)(static_cast<uint32_t>(u * static_cast<float>(width)), width - 1);
                    probeY_ = (std::min)(static_cast<uint32_t>(v * static_cast<float>(height)), height - 1);
                }
            }
        }

        // 追う画素を選ぶ
        const int clientW = (std::max)(graphics->GetClientWidth(), 1);
        const int clientH = (std::max)(graphics->GetClientHeight(), 1);
        bool consumed = false;
        if (args.hovered && (tracePicking_ || traceFollowMouse_)) {
            const int px = std::clamp(static_cast<int>(u * static_cast<float>(clientW)), 0, clientW - 1);
            const int py = std::clamp(static_cast<int>(v * static_cast<float>(clientH)), 0, clientH - 1);
            if (traceFollowMouse_ && !traceHold_) {
                traceX_ = px;
                traceY_ = py;
            }
            if (tracePicking_) {
                drawList->AddLine(ImVec2(mouse.x - 10.0f, mouse.y), ImVec2(mouse.x + 10.0f, mouse.y), IM_COL32(255, 220, 60, 255), 1.5f);
                drawList->AddLine(ImVec2(mouse.x, mouse.y - 10.0f), ImVec2(mouse.x, mouse.y + 10.0f), IM_COL32(255, 220, 60, 255), 1.5f);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    traceX_ = px;
                    traceY_ = py;
                    tracePicking_ = false;
                    traceHold_ = false;
                }
                consumed = true;
            }
        }

        // 追っている画素の印
        if (traceActive_) {
            const ImVec2 mark(args.imageMin.x + (static_cast<float>(traceX_) + 0.5f) / static_cast<float>(clientW) * size.x,
                args.imageMin.y + (static_cast<float>(traceY_) + 0.5f) / static_cast<float>(clientH) * size.y);
            drawList->AddCircle(mark, 6.0f, IM_COL32(255, 220, 60, 255), 0, 1.5f);
        }
        return consumed;
    }

    // -------------------------------------------------------------------------
    // 線
    // -------------------------------------------------------------------------
    void RayTracingDebugPanel::SubmitLines(LineRendererPipeline& pipeline, [[maybe_unused]] const Camera* camera)
    {
        if (!windowOpen_) {
            return;
        }
        std::vector<Line> lines;

        RTDebugSystem* debug = GetDebugSystem();
        if (debug && traceActive_ && drawTraceLines_) {
            const RTDebugTraceResult& result = debug->GetTraceResult();
            if (result.valid) {
                const RTDebugTracedRay& ray = result.cameraRay;
                AddRayLines(lines, ray.origin, ray.direction, 0.0f, ray.tMax, ray.hit, ray.hitT, Vector3{ 1.0f, 1.0f, 1.0f });
            }
            const RTDebugRayRecordResult& record = debug->GetRayRecordResult();
            if (record.valid) {
                for (const RTDebugRecordedRay& ray : record.rays) {
                    if (traceDispatchFilter_ >= 0 && static_cast<uint32_t>(traceDispatchFilter_) != ray.dispatch) {
                        continue;
                    }
                    AddRayLines(lines, ray.origin, ray.direction, ray.tMin, ray.tMax, ray.hit, ray.hitT,
                        DispatchColor(record, ray.dispatch));
                }
            }
        }

        RenderDomainContext* domain = engine_ ? engine_->GetRenderDomainContext() : nullptr;
        RTScene* scene = domain ? domain->GetRTScene() : nullptr;
        if (drawSelectedBox_ && scene && selectedRow_ >= 0
            && static_cast<size_t>(selectedRow_) < scene->GetInstances().size()
            && static_cast<size_t>(selectedRow_) < scene->GetRows().size()) {
            const RTScene::RowRecord& record = scene->GetRows()[static_cast<size_t>(selectedRow_)];
            if (record.resource && record.resource->GetLocalBoundingBox().IsValid()) {
                const BoundingBox& box = record.resource->GetLocalBoundingBox();
                const auto& m = scene->GetInstances()[static_cast<size_t>(selectedRow_)].transform;
                Vector3 corners[8];
                for (int c = 0; c < 8; ++c) {
                    corners[c] = TransformPoint(m, (c & 1) ? box.max.x : box.min.x, (c & 2) ? box.max.y : box.min.y,
                        (c & 4) ? box.max.z : box.min.z);
                }
                constexpr int kEdges[12][2] = {
                    { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 },
                    { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
                };
                const Vector3 color{ 0.3f, 1.0f, 0.4f };
                for (const auto& edge : kEdges) {
                    lines.emplace_back(corners[edge[0]], corners[edge[1]], color, 1.0f, false);
                }
            }
        }

        if (!lines.empty()) {
            pipeline.AddLines(lines);
        }
    }

} // namespace CoreEngine

#endif // CORE_EDITOR
