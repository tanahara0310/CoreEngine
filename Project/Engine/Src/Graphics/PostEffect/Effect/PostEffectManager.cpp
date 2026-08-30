#include "pch.h"
#include "PostEffectManager.h"

#include "Graphics/RHI/GraphicsCore.h"
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
#include "LoadingScreen/LoadingScreenEffect.h"
#include "Bloom/Bloom.h"
#include "LensFlare/LensFlare.h"
#include "Dissolve/Dissolve.h"
#include "ToneMapping/ToneMapping.h"
#include "FilmGrain/FilmGrain.h"
#include "MotionBlur/MotionBlur.h"
#include "LocalExposure/LocalExposure.h"
#include "ColorLUT/ColorLUT.h"
#include "DepthOfField/DepthOfField.h"
#include "Outline/Outline.h"
#include "PostEffectPresetManager.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Utility/Logger/Logger.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <unordered_set>

// =============================================================================
// PostEffectManager実装
// =============================================================================

namespace CoreEngine
{
void PostEffectManager::Initialize(GraphicsCore* dxCommon, Render* render, ShaderProgramCache* shaderProgramCache)
{
    shaderProgramCache_ = shaderProgramCache;
    assert(dxCommon);
    assert(render);
    graphicsCore_ = dxCommon;
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
    RegisterEffect<FilmGrain>(PostEffectNames::FilmGrain);
    RegisterEffect<MotionBlur>(PostEffectNames::MotionBlur);
    RegisterEffect<LocalExposure>(PostEffectNames::LocalExposure);
    RegisterEffect<ColorLUT>(PostEffectNames::ColorLUT);
    RegisterEffect<DepthOfField>(PostEffectNames::DepthOfField);
    RegisterEffect<ToneMapping>(PostEffectNames::ToneMapping);
    RegisterEffect<LoadingScreenEffect>(PostEffectNames::LoadingScreen);

    // エフェクトチェーンの順序を登録と同じ場所で定義（二重管理を防ぐ）
    // 並びは PostEffectStage の昇順でなければならない（ValidateChain が検証する）。
    // 原則: 光学現象と露出・グレーディングはトーンマップ前、記録と演出はトーンマップ後。
    effectChain_ = {
        // ---- SceneHDR: トーンカーブを通る前の物理量に対して効くもの ----
        // モーションブラーは露光中の積分そのものなので最初。ブラー後の画像に Bloom が乗る
        PostEffectNames::MotionBlur,
        // DoF はレンズの結像なので Bloom（レンズ内散乱）より前
        PostEffectNames::DepthOfField,
        PostEffectNames::Bloom,
        PostEffectNames::LensFlare,
        // ローカル露出は光学現象（Bloom/LensFlare）の後、色調整の前。
        // 露出の一種なのでトーンマップへ渡る直前の輝度分布に対して効かせる
        PostEffectNames::LocalExposure,
        PostEffectNames::ChromaticAberration,
        PostEffectNames::Vignette,
        PostEffectNames::ColorGrading,
        // ---- Tonemap: HDR→LDR の境界。常時有効・ちょうど 1 つ ----
        PostEffectNames::ToneMapping,
        // ---- PostTonemap: 表示色に対して効く演出系 ----
        // LUT はトーンマップ直後の表示色に対するルック。演出系より前に置く
        PostEffectNames::ColorLUT,
        PostEffectNames::FadeEffect,
        PostEffectNames::Shockwave,
        PostEffectNames::Blur,
        PostEffectNames::Random,
        PostEffectNames::RadialBlur,
        PostEffectNames::RasterScroll,
        PostEffectNames::Sepia,
        PostEffectNames::Invert,
        PostEffectNames::GrayScale,
        // グレインは色をいじる演出（セピア・モノクロ）より後。先に乗せると粒まで脱色される
        PostEffectNames::FilmGrain,
        PostEffectNames::Outline,
        PostEffectNames::Dissolve,
        // ローディング画面は他の演出より前に出す
        PostEffectNames::LoadingScreen,
    };

    ValidateChain();
    RebuildEffectPtrCache();
}

void PostEffectManager::RebuildEffectPtrCache()
{
    effectPtrCache_.clear();
    effectNameCache_.clear();
    prepareCache_.clear();
    effectPtrCache_.reserve(effectChain_.size());
    effectNameCache_.reserve(effectChain_.size());
    prepareCache_.reserve(effects_.size());

    std::unordered_set<std::string> chainMembers;
    chainMembers.reserve(effectChain_.size());

    for (const auto& name : effectChain_) {
        chainMembers.insert(name);
        if (auto* effect = GetEffectInternal(name); effect && effect->IsEnabled()) {
            effectPtrCache_.push_back(effect);
            effectNameCache_.push_back(name);
            prepareCache_.push_back(effect);
        }
    }

    // チェーン外に登録されたエフェクト（FullScreen 等）も文脈は受け取る必要がある。
    // ここで一度だけ集めておき、毎フレームの線形探索（旧 Update の O(n*m)）を無くす。
    for (auto& [name, effect] : effects_) {
        if (effect->IsEnabled() && !chainMembers.contains(name)) {
            prepareCache_.push_back(effect.get());
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
    ValidateChain();
    RebuildEffectPtrCache();
}

bool PostEffectManager::ValidateChain() const
{
    std::array<int, kPostEffectStageCount> stageCounts{};
    PostEffectStage previousStage = PostEffectStage::SceneHDR;
    std::string previousName;
    bool valid = true;

    for (const auto& name : effectChain_) {
        const PostEffectBase* effect = GetEffectInternal(name);
        if (!effect) {
            Logger::GetInstance().Errorf(LogCategory::Graphics,
                "[PostEffect] チェーンに未登録のエフェクトが含まれています: {}", name);
            valid = false;
            continue;
        }

        const PostEffectStage stage = effect->GetStage();
        ++stageCounts[static_cast<size_t>(stage)];

        // 段は昇順にしか進めない。逆行は「トーンマップ後に光学現象を掛ける」等の設計崩れを意味する
        if (static_cast<uint8_t>(stage) < static_cast<uint8_t>(previousStage)) {
            Logger::GetInstance().Errorf(LogCategory::Graphics,
                "[PostEffect] 段が逆行しています: {} は {} ですが直前の {} は {} でした",
                name, ToString(stage), previousName, ToString(previousStage));
            valid = false;
        }

        previousStage = stage;
        previousName = name;
    }

    const int tonemapCount = stageCounts[static_cast<size_t>(PostEffectStage::Tonemap)];
    if (tonemapCount != 1) {
        Logger::GetInstance().Errorf(LogCategory::Graphics,
            "[PostEffect] Tonemap 段はちょうど 1 つでなければなりません（現在 {} 個）", tonemapCount);
        valid = false;
    }

    if (valid) {
        Logger::GetInstance().Infof(LogCategory::Graphics,
            "[PostEffect] chain validated: SceneHDR={} Tonemap={} PostTonemap={}",
            stageCounts[static_cast<size_t>(PostEffectStage::SceneHDR)],
            stageCounts[static_cast<size_t>(PostEffectStage::Tonemap)],
            stageCounts[static_cast<size_t>(PostEffectStage::PostTonemap)]);
    }

    assert(valid && "PostEffect チェーンの段が不正です（詳細は Graphics カテゴリのログ）");
    return valid;
}

void PostEffectManager::PrepareFrame(const PostEffectFrameContext& ctx)
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

    // 実行順（チェーン順）→ チェーン外、の決まった順序で配る
    for (PostEffectBase* effect : prepareCache_) {
        effect->PrepareFrame(ctx);
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
