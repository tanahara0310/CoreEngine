#include "PostEffectManager.h"

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/Render.h"
#include "Graphics/PostEffect/PostEffectNames.h"
#include "Effect/GrayScale.h"
#include "FullScreen.h"
#include "Effect/Blur.h"
#include "Effect/Shockwave.h"
#include "Effect/Vignette.h"
#include "Effect/RadialBlur.h"
#include "Effect/ColorGrading.h"
#include "Effect/ChromaticAberration.h"
#include "Effect/Sepia.h"
#include "Effect/Invert.h"
#include "Effect/RasterScroll.h"
#include "Effect/FadeEffect.h"
#include "Effect/Bloom.h"
#include "Effect/Dissolve.h"
#include "Effect/DeferredLighting.h"
#include "Effect/ToneMapping.h"
#include "PostEffectPresetManager.h"
#include "Utility/Debug/ImGui/ImguiManager.h"
#include <algorithm>
#include <cassert>
#include <unordered_set>

// =============================================================================
// PostEffectManager実装
// =============================================================================

namespace CoreEngine
{
void PostEffectManager::Initialize(DirectXCommon* dxCommon, Render* render)
{
    assert(dxCommon);
    assert(render);
    directXCommon_ = dxCommon;
    render_ = render;

    // プリセットマネージャーの初期化
    presetManager_ = std::make_unique<PostEffectPresetManager>();

    RegisterAllEffects();

    // 最終テクスチャハンドルの初期化
    finalDisplayHandle_ = directXCommon_->GetOffScreenSrvHandle();
}

void PostEffectManager::RegisterAllEffects()
{
    // FullScreenは常に有効（コピー用）
    RegisterEffect<FullScreen>(PostEffectNames::FullScreen, true);

    // DeferredLightingはRenderPass側から明示実行する（チェーンには含めない）
    RegisterEffect<DeferredLighting>(PostEffectNames::DeferredLighting, true);

    // FadeEffectはデフォルトで有効
    RegisterEffect<FadeEffect>(PostEffectNames::FadeEffect, true);

    // その他のエフェクトはデフォルトで無効
    RegisterEffect<GrayScale>(PostEffectNames::GrayScale, false);
    RegisterEffect<Blur>(PostEffectNames::Blur, false);
    RegisterEffect<RadialBlur>(PostEffectNames::RadialBlur, false);
    RegisterEffect<Shockwave>(PostEffectNames::Shockwave, false);
    RegisterEffect<Vignette>(PostEffectNames::Vignette, false);
    RegisterEffect<ColorGrading>(PostEffectNames::ColorGrading, false);
    RegisterEffect<ChromaticAberration>(PostEffectNames::ChromaticAberration, false);
    RegisterEffect<Sepia>(PostEffectNames::Sepia, false);
    RegisterEffect<Invert>(PostEffectNames::Invert, false);
    RegisterEffect<RasterScroll>(PostEffectNames::RasterScroll, false);
    RegisterEffect<Bloom>(PostEffectNames::Bloom, false);
    RegisterEffect<Dissolve>(PostEffectNames::Dissolve, false);

    // トーンマッピングは常に有効（HDR→LDR変換）
    RegisterEffect<ToneMapping>(PostEffectNames::ToneMapping, true);

    // エフェクトチェーンの順序を登録と同じ場所で定義（二重管理を防ぐ）
    effectChain_ = {
        PostEffectNames::Bloom,
        PostEffectNames::ToneMapping,
        PostEffectNames::FadeEffect,
        PostEffectNames::Shockwave,
        PostEffectNames::Blur,
        PostEffectNames::RadialBlur,
        PostEffectNames::RasterScroll,
        PostEffectNames::ColorGrading,
        PostEffectNames::ChromaticAberration,
        PostEffectNames::Sepia,
        PostEffectNames::Invert,
        PostEffectNames::GrayScale,
        PostEffectNames::Vignette,
        PostEffectNames::Dissolve,
    };

    RebuildEffectPtrCache();
}

void PostEffectManager::RebuildEffectPtrCache()
{
    effectPtrCache_.clear();
    effectPtrCache_.reserve(effectChain_.size());
    for (const auto& name : effectChain_) {
        if (auto* effect = GetEffectInternal(name); effect && effect->IsEnabled()) {
            effectPtrCache_.push_back(effect);
        }
    }
}

void PostEffectManager::RegisterEffectInternal(const std::string& name, std::unique_ptr<PostEffectBase> effect)
{
    effects_[name] = std::move(effect);
}

PostEffectBase* PostEffectManager::GetEffectInternal(const std::string& name)
{
    auto it = effects_.find(name);
    if (it != effects_.end()) {
        return it->second.get();
    }
    return nullptr;
}

const PostEffectBase* PostEffectManager::GetEffectInternal(const std::string& name) const
{
    auto it = effects_.find(name);
    if (it != effects_.end()) {
        return it->second.get();
    }
    return nullptr;
}

D3D12_GPU_DESCRIPTOR_HANDLE PostEffectManager::ExecuteEffectChain(
    D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle)
{
    // 有効エフェクトがない場合は入力をそのまま返す
    if (effectPtrCache_.empty()) {
        finalDisplayHandle_ = inputSrvHandle;
        return inputSrvHandle;
    }

    // Ping-Pongバッファで順次エフェクトを適用（キャッシュ済みポインタを直接使用しmapルックアップを排除）
    PingPongBuffer pingPong(directXCommon_, render_);
    pingPong.Reset(inputSrvHandle);

    for (auto* effect : effectPtrCache_) {
        pingPong.ApplyEffect(effect);
    }

    // 最終結果を保存して返す（どちらのバッファにあってもそのまま返す）
    finalDisplayHandle_ = pingPong.GetCurrentOutput();
    return finalDisplayHandle_;
}

void PostEffectManager::ExecuteEffect(const std::string& name, D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle)
{
    auto* effect = GetEffectInternal(name);
    if (effect) {
        effect->Draw(inputSrvHandle);
    }
}

void PostEffectManager::ExecuteEffectToBackBuffer(const std::string& name, D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle)
{
    auto* effect = GetEffectInternal(name);
    if (effect) {
        effect->DrawToBackBuffer(inputSrvHandle);
    }
}

void PostEffectManager::SetEffectEnabled(const std::string& effectName, bool enabled)
{
    auto* effect = GetEffectInternal(effectName);
    if (effect) {
        effect->SetEnabled(enabled);
        RebuildEffectPtrCache();
    }
}

bool PostEffectManager::IsEffectEnabled(const std::string& effectName) const
{
    auto it = effects_.find(effectName);
    if (it != effects_.end()) {
        return it->second->IsEnabled();
    }
    return false;
}

void PostEffectManager::SetEffectChain(const std::vector<std::string>& effectNames)
{
#ifdef _DEBUG
    for (const auto& name : effectNames) {
        assert(effects_.contains(name) && "SetEffectChain: 未登録のエフェクト名が含まれています");
    }
#endif
    effectChain_ = effectNames;
    RebuildEffectPtrCache();
}

const std::vector<std::string>& PostEffectManager::GetEffectChain() const
{
    return effectChain_;
}

void PostEffectManager::Update(float deltaTime)
{
    // effectChain_順に有効エフェクトを更新（実行順と一致させ、毎回同じ順序を保証）
    for (const auto& name : effectChain_) {
        auto* effect = GetEffectInternal(name);
        if (effect && effect->IsEnabled()) {
            effect->Update(deltaTime);
        }
    }
    // チェーン外に登録されたエフェクト（DeferredLighting、FullScreen等）も更新
    for (auto& [name, effect] : effects_) {
        if (effect->IsEnabled()) {
            bool inChain = false;
            for (const auto& chainName : effectChain_) {
                if (chainName == name) { inChain = true; break; }
            }
            if (!inChain) {
                effect->Update(deltaTime);
            }
        }
    }
}

void PostEffectManager::DrawImGui()
{
#ifdef USE_IMGUI
    if (ImGui::Begin("Post Effects")) {
        DrawImGuiContent();
    }
    ImGui::End();
#endif // USE_IMGUI
}

void PostEffectManager::DrawImGuiContent()
{
#ifdef USE_IMGUI
    // プリセット管理
    presetManager_->ShowImGui(this);
    UI::Separator();

    // 検索ボックス
    UI::SectionHeader("エフェクト一覧");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##effectsearch", "検索...", imguiSearchBuf_, sizeof(imguiSearchBuf_));

    std::string searchStr = imguiSearchBuf_;
    std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    UI::Spacing();

    // 全エフェクト表示リストを構築（effectChain_ 順 + チェーン外エフェクト）
    std::unordered_set<std::string> chainSet(effectChain_.begin(), effectChain_.end());
    std::vector<std::string> displayList;
    displayList.reserve(effectChain_.size() + 4);
    for (const auto& name : effectChain_) {
        displayList.push_back(name);
    }
    for (const auto& [name, effect] : effects_) {
        if (!chainSet.contains(name)) {
            displayList.push_back(name);
        }
    }

    // 検索フィルタリング
    std::vector<std::string> filteredList;
    filteredList.reserve(displayList.size());
    for (const auto& name : displayList) {
        if (searchStr.empty()) {
            filteredList.push_back(name);
        } else {
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.find(searchStr) != std::string::npos) {
                filteredList.push_back(name);
            }
        }
    }

    // フィルタ後に選択中エフェクトが消えた場合はクリア
    if (!imguiSelectedEffect_.empty()) {
        if (std::find(filteredList.begin(), filteredList.end(), imguiSelectedEffect_) == filteredList.end()) {
            imguiSelectedEffect_.clear();
        }
    }

    const float panelHeight = ImGui::GetContentRegionAvail().y;

    // ─── 左パネル: エフェクト一覧 ───
    if (auto listPanel = UI::Scope::ChildScope("##effectlist", ImVec2(kEffectListPanelWidth, panelHeight), ImGuiChildFlags_Border)) {
        for (const auto& name : filteredList) {
            auto* effect = GetEffectInternal(name);
            if (!effect) continue;

            ImGui::PushID(name.c_str());

            const bool isEnabled = effect->IsEnabled();
            const bool isSelected = (imguiSelectedEffect_ == name);

            if (isEnabled) {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "●");
            } else {
                ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.45f, 1.0f), "●");
            }
            ImGui::SameLine();

            if (ImGui::Selectable(name.c_str(), isSelected)) {
                imguiSelectedEffect_ = name;
            }

            ImGui::PopID();
        }

        if (filteredList.empty()) {
            ImGui::TextDisabled("該当なし");
        }
    }

    ImGui::SameLine();

    // ─── 右パネル: 選択エフェクトの詳細 ───
    if (auto detailPanel = UI::Scope::ChildScope("##effectdetail", ImVec2(0.0f, panelHeight), ImGuiChildFlags_Border)) {
        if (imguiSelectedEffect_.empty()) {
            ImGui::TextDisabled("エフェクトを選択してください");
        } else {
            auto* effect = GetEffectInternal(imguiSelectedEffect_);
            if (!effect) {
                ImGui::TextDisabled("エフェクトが見つかりません");
            } else {
                UI::SectionHeader(imguiSelectedEffect_.c_str());

                if (effect->IsAlwaysEnabled()) {
                    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "常時有効");
                } else {
                    bool enabled = effect->IsEnabled();
                    if (UI::Widgets::ToggleSwitch("有効", &enabled)) {
                        SetEffectEnabled(imguiSelectedEffect_, enabled);
                    }
                }

                UI::Separator();

                effect->DrawImGui();
            }
        }
    }
#endif // USE_IMGUI
}

D3D12_GPU_DESCRIPTOR_HANDLE PostEffectManager::GetFinalDisplayTextureHandle() const
{
    return finalDisplayHandle_;
}
}
