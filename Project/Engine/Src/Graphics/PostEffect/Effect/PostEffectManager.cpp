#include "pch.h"
#include "PostEffectManager.h"

#include "Graphics/Common/DirectXCommon.h"
#include "Utility/CVar/CVarRegistry.h"
#include "Graphics/Render/Render.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "GrayScale/GrayScale.h"
#include "../FullScreen.h"
#include "Blur/Blur.h"
#include "Shockwave/Shockwave.h"
#include "Vignette/Vignette.h"
#include "RadialBlur/RadialBlur.h"
#include "ColorGrading/ColorGrading.h"
#include "ChromaticAberration/ChromaticAberration.h"
#include "Sepia/Sepia.h"
#include "Invert/Invert.h"
#include "Random/Random.h"
#include "RasterScroll/RasterScroll.h"
#include "FadeEffect/FadeEffect.h"
#include "Bloom/Bloom.h"
#include "LensFlare/LensFlare.h"
#include "Dissolve/Dissolve.h"
#include "ToneMapping/ToneMapping.h"
#include "Outline/Outline.h"
#include "PostEffectPresetManager.h"
#include "Editor/ImGui/ImguiManager.h"
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
    if (RenderTarget* sceneColorTarget = render_->GetRenderTarget(RenderTargetNames::SceneColor)) {
        finalDisplayHandle_ = sceneColorTarget->GetSRVHandle();
    }
}

void PostEffectManager::RegisterAllEffects()
{
    // 各エフェクトの既定の有効/無効は CVar（"r.<Effect>.Enabled"）の既定値が持つ。
    // ここで指定していたフラグは二重管理になるため廃止した。
    // FullScreen / ToneMapping は CVar を持たず常時有効（PostEffectBase::enabled_ = true）
    RegisterEffect<FullScreen>(PostEffectNames::FullScreen);
    RegisterEffect<FadeEffect>(PostEffectNames::FadeEffect);
    RegisterEffect<GrayScale>(PostEffectNames::GrayScale);
    RegisterEffect<Random>(PostEffectNames::Random);
    RegisterEffect<Blur>(PostEffectNames::Blur);
    RegisterEffect<RadialBlur>(PostEffectNames::RadialBlur);
    RegisterEffect<Shockwave>(PostEffectNames::Shockwave);
    RegisterEffect<Vignette>(PostEffectNames::Vignette);
    RegisterEffect<ColorGrading>(PostEffectNames::ColorGrading);
    RegisterEffect<ChromaticAberration>(PostEffectNames::ChromaticAberration);
    RegisterEffect<Sepia>(PostEffectNames::Sepia);
    RegisterEffect<Invert>(PostEffectNames::Invert);
    RegisterEffect<RasterScroll>(PostEffectNames::RasterScroll);
    RegisterEffect<Bloom>(PostEffectNames::Bloom);
    RegisterEffect<LensFlare>(PostEffectNames::LensFlare);
    RegisterEffect<Dissolve>(PostEffectNames::Dissolve);
    RegisterEffect<Outline>(PostEffectNames::Outline);
    RegisterEffect<ToneMapping>(PostEffectNames::ToneMapping);

    // エフェクトチェーンの順序を登録と同じ場所で定義（二重管理を防ぐ）
    effectChain_ = {
        PostEffectNames::Bloom,
        PostEffectNames::LensFlare, // HDR 空間で合成するため ToneMapping より前
        PostEffectNames::ToneMapping,
        PostEffectNames::FadeEffect,
        PostEffectNames::Shockwave,
        PostEffectNames::Blur,
        PostEffectNames::Random,
        PostEffectNames::RadialBlur,
        PostEffectNames::RasterScroll,
        PostEffectNames::ColorGrading,
        PostEffectNames::ChromaticAberration,
        PostEffectNames::Sepia,
        PostEffectNames::Invert,
        PostEffectNames::GrayScale,
        PostEffectNames::Vignette,
        PostEffectNames::Outline,
        PostEffectNames::Dissolve,
    };

    RebuildEffectPtrCache();
}

void PostEffectManager::RebuildEffectPtrCache()
{
    effectPtrCache_.clear();
    effectPtrCache_.reserve(effectChain_.size());
    effectNameCache_.clear();
    effectNameCache_.reserve(effectChain_.size());
    for (const auto& name : effectChain_) {
        if (auto* effect = GetEffectInternal(name); effect && effect->IsEnabled()) {
            effectPtrCache_.push_back(effect);
            effectNameCache_.push_back(name);
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

void PostEffectManager::Update(float deltaTime)
{
    // 有効/無効は CVar（"r.<Effect>.Enabled"）が持つため、SetEffectEnabled を通らない経路でも
    // 変化しうる（CVars.json からの復元、コンソール入力、プリセット適用など）。
    // その場合キャッシュが古いままだと「タブ上は有効なのに画面に反映されない」状態になるので、
    // CVar の変更通番を見て再構築する
    const uint32_t cvarRevision = CVarRegistry::Get().GetGlobalRevision();
    if (cvarRevision != lastCVarRevision_) {
        lastCVarRevision_ = cvarRevision;
        RebuildEffectPtrCache();
    }

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

void PostEffectManager::SetFinalDisplayTextureHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle)
{
    finalDisplayHandle_ = handle;
}
}
