#include "PostEffectManager.h"

#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Graphics/Render/Render.h"
#include "Engine/Graphics/PostEffect/PostEffectNames.h"
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
#include "PostEffectPresetManager.h"
#include "Engine/Utility/Debug/ImGui/ImguiManager.h"
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

	// エフェクトを現在の出力バッファに描画
	render_->OffscreenPreDraw(currentOutputIndex_);
	effect->Draw(currentInput_);
	render_->OffscreenPostDraw(currentOutputIndex_);

	// 今書き込んだバッファが次の入力になる
	currentInput_ = GetSrvHandle(currentOutputIndex_);

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

	// バッファ0にある場合、バッファ1にコピー
	render_->OffscreenPreDraw(1);
	fullScreenEffect->Draw(currentInput_);
	render_->OffscreenPostDraw(1);

	// 出力を更新
	currentInput_ = GetSrvHandle(1);
	currentOutputIndex_ = 0; // 次回は0に書き込む
}

D3D12_GPU_DESCRIPTOR_HANDLE PostEffectManager::PingPongBuffer::GetSrvHandle(int index) const
{
	return (index == 0)
		? dxCommon_->GetOffScreenSrvHandle()
		: dxCommon_->GetOffScreen2SrvHandle();
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
	if (ImGui::Begin("Post Effects")) {
		// プリセット管理タブ
		presetManager_->ShowImGui(this);

		ImGui::Separator();

		// エフェクトチェーン状態の表示
		if (ImGui::CollapsingHeader("エフェクトチェーン状態", ImGuiTreeNodeFlags_DefaultOpen)) {
			auto enabledNames = CollectEnabledEffectNames(effectChain_);

			ImGui::Text("エフェクトチェーン: %s",
				enabledNames.empty() ? "非アクティブ (パススルー)" : "アクティブ");

			ImGui::Text("登録済みエフェクト数: %zu", effects_.size());
			ImGui::Text("有効なエフェクト数: %zu", enabledNames.size());
			ImGui::Text("エフェクトチェーン:");

			for (const auto& name : effectChain_) {
				auto* effect = GetEffectInternal(name);
				if (effect && effect->IsEnabled()) {
					ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "  - %s", name.c_str());
				} else {
					ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "  - %s (無効)", name.c_str());
				}
			}

			if (enabledNames.empty()) {
				ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
					"エフェクトが無効 - 元の画像を描画中");
			}

			ImGui::Separator();
		}

		// 各エフェクトのパラメータ調整
		for (auto& [name, effect] : effects_) {
			ImGui::PushID(name.c_str());

			if (ImGui::CollapsingHeader(name.c_str())) {
				// FullScreenエフェクト以外はenable/disableチェックボックスを表示
				if (name != PostEffectNames::FullScreen) {
					bool enabled = effect->IsEnabled();
					if (ImGui::Checkbox("有効", &enabled)) {
						effect->SetEnabled(enabled);
					}
					ImGui::Separator();
				}

				// エフェクトのパラメータ調整
				effect->DrawImGui();
			}

			ImGui::PopID();
		}
	}
	ImGui::End();
}

D3D12_GPU_DESCRIPTOR_HANDLE PostEffectManager::GetFinalDisplayTextureHandle() const
{
	return finalDisplayHandle_;
}
}
