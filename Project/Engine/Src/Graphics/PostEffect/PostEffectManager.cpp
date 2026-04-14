#include "PostEffectManager.h"

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/Render.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
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

// =============================================================================
// PingPongBuffer実装
// =============================================================================


namespace CoreEngine
{
PostEffectManager::PingPongBuffer::PingPongBuffer(DirectXCommon* dxCommon, Render* render)
    : dxCommon_(dxCommon)
    , render_(render)
    , currentInput_()
    , currentOutputIndex_(1)
{
}

void PostEffectManager::PingPongBuffer::Reset(D3D12_GPU_DESCRIPTOR_HANDLE input)
{
    currentInput_ = input;
    currentOutputIndex_ = 1;
}

bool PostEffectManager::PingPongBuffer::ApplyEffect(PostEffectBase* effect)
{
    if (!effect || !effect->IsEnabled()) {
        return false;
    }

    auto* cmdList = dxCommon_->GetCommandList();
    auto* renderTarget = GetRenderTarget(currentOutputIndex_);

    // エフェクトを現在の出力バッファに描画
    renderTarget->Begin(cmdList);
    effect->Draw(currentInput_);
    renderTarget->End(cmdList);

    // 今書き込んだバッファが次の入力になる
    currentInput_ = renderTarget->GetSRVHandle();

    // 次回の出力先を切り替え（ping-pong）
    currentOutputIndex_ = (currentOutputIndex_ == 0) ? 1 : 0;

    return true;
}

D3D12_GPU_DESCRIPTOR_HANDLE PostEffectManager::PingPongBuffer::GetCurrentOutput() const
{
    return currentInput_;
}

void PostEffectManager::PingPongBuffer::EnsureOutputInBuffer1(PostEffectBase* fullScreenEffect)
{
    // 最後に書き込まれたバッファを特定（currentOutputIndexは次回の出力先なので、その反対が最後の書き込み先）
    int lastWrittenIndex = (currentOutputIndex_ == 0) ? 1 : 0;

    // 既にバッファ1にある場合は何もしない
    if (lastWrittenIndex == 1) {
        return;
    }

    auto* cmdList = dxCommon_->GetCommandList();
    auto* renderTarget = GetRenderTarget(1);

    // バッファ0にある場合、バッファ1にコピー
    renderTarget->Begin(cmdList);
    fullScreenEffect->Draw(currentInput_);
    renderTarget->End(cmdList);

    // 出力を更新
    currentInput_ = renderTarget->GetSRVHandle();
    currentOutputIndex_ = 0; // 次回は0に書き込む
}

RenderTarget* PostEffectManager::PingPongBuffer::GetRenderTarget(int index) const
{
    // 名前ベースでレンダーターゲットを取得
    if (index == 0) {
        return render_->GetRenderTarget("Offscreen0");
    } else if (index == 1) {
        return render_->GetRenderTarget("Offscreen1");
    }
    return nullptr;
}

D3D12_GPU_DESCRIPTOR_HANDLE PostEffectManager::PingPongBuffer::GetSrvHandle(int index) const
{
    auto* renderTarget = GetRenderTarget(index);
    return renderTarget ? renderTarget->GetSRVHandle() : D3D12_GPU_DESCRIPTOR_HANDLE{};
}

// =============================================================================
// PostEffectManager実装
// =============================================================================

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

std::vector<std::string> PostEffectManager::CollectEnabledEffectNames(
    const std::vector<std::string>& effectNames) const
{
    std::vector<std::string> enabledNames;
    enabledNames.reserve(effectNames.size());

    for (const auto& name : effectNames) {
        if (auto* effect = GetEffectInternal(name); effect && effect->IsEnabled()) {
            enabledNames.push_back(name);
        }
    }

    return enabledNames;
}

D3D12_GPU_DESCRIPTOR_HANDLE PostEffectManager::ExecuteEffectChain(
    D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle)
{
    // 有効なエフェクト名を収集
    auto enabledNames = CollectEnabledEffectNames(effectChain_);

    // 有効なエフェクトがない場合は入力をそのまま返す
    if (enabledNames.empty()) {
        finalDisplayHandle_ = inputSrvHandle;
        return inputSrvHandle;
    }

    // Ping-Pongバッファで順次エフェクトを適用
    PingPongBuffer pingPong(directXCommon_, render_);
    pingPong.Reset(inputSrvHandle);

    for (const auto& name : enabledNames) {
        if (auto* effect = GetEffectInternal(name)) {
            pingPong.ApplyEffect(effect);
        }
    }

    auto* fullScreenEffect = GetEffect<FullScreen>(PostEffectNames::FullScreen);
    assert(fullScreenEffect);
    pingPong.EnsureOutputInBuffer1(fullScreenEffect);

    // 最終結果を保存して返す
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
    effectChain_ = effectNames;
}

const std::vector<std::string>& PostEffectManager::GetEffectChain() const
{
    return effectChain_;
}

void PostEffectManager::Update(float deltaTime)
{
    // 全エフェクトに対してUpdate呼び出し
    for (auto& [name, effect] : effects_) {
        effect->Update(deltaTime);
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
    std::vector<std::string> displayList;
    displayList.reserve(effectChain_.size() + 4);
    for (const auto& name : effectChain_) {
        displayList.push_back(name);
    }
    for (const auto& [name, effect] : effects_) {
        if (std::find(effectChain_.begin(), effectChain_.end(), name) == effectChain_.end()) {
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
    const float listWidth = 200.0f;

    // ─── 左パネル: エフェクト一覧 ───
    if (auto listPanel = UI::Scope::ChildScope("##effectlist", ImVec2(listWidth, panelHeight), ImGuiChildFlags_Border)) {
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

                const bool isAlwaysOn = (imguiSelectedEffect_ == PostEffectNames::FullScreen
                    || imguiSelectedEffect_ == PostEffectNames::DeferredLighting
                    || imguiSelectedEffect_ == PostEffectNames::ToneMapping);

                if (isAlwaysOn) {
                    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "常時有効");
                } else {
                    bool enabled = effect->IsEnabled();
                    if (UI::Widgets::ToggleSwitch("有効", &enabled)) {
                        effect->SetEnabled(enabled);
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
