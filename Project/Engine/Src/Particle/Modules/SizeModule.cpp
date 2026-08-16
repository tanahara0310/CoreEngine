#include "pch.h"
#include "SizeModule.h"
#include "../ParticleSystem.h"
#include <algorithm>

// コンストラクタでデフォルトパラメータを設定

namespace CoreEngine
{
SizeModule::SizeModule() {
    sizeData_.endSize = 0.0f;
    sizeData_.sizeOverLifetime = true;
    sizeData_.use3DSize = false;
    sizeData_.endSize3D = { 0.0f, 0.0f, 0.0f };
    sizeData_.uniformScaling = true;
    sizeData_.minSize = 0.0f;
    sizeData_.maxSize = 100.0f;
}

void SizeModule::UpdateSize(Particle& particle)
{
    if (!enabled_ || !sizeData_.sizeOverLifetime) {
        return;
    }

    // ライフタイム係数を取得
  float lifetimeRatio = GetLifetimeRatio(particle);
    
    // カーブを適用
    float curveValue = ApplyCurve(lifetimeRatio, sizeData_.sizeCurve);

    if (sizeData_.use3DSize) {
        // 3Dサイズでの補間
        // MainModuleで設定された初期サイズを使用
        Vector3 startSize = particle.initialScale;
        Vector3 currentSize = MathCore::Lerp(startSize, sizeData_.endSize3D, curveValue);
        
      // サイズ制限を適用
        currentSize.x = std::clamp(currentSize.x, sizeData_.minSize, sizeData_.maxSize);
        currentSize.y = std::clamp(currentSize.y, sizeData_.minSize, sizeData_.maxSize);
      currentSize.z = std::clamp(currentSize.z, sizeData_.minSize, sizeData_.maxSize);
        
        particle.transform.scale = currentSize;
    } else {
        // 1Dサイズでの補間（線形補間）
        // MainModuleで設定された初期サイズを使用（均等スケーリング想定）
        float startSize = particle.initialScale.x;
   float currentSize = startSize + (sizeData_.endSize - startSize) * curveValue;
      currentSize = std::clamp(currentSize, sizeData_.minSize, sizeData_.maxSize);
        particle.transform.scale = {currentSize, currentSize, currentSize};
    }
}

#ifdef USE_IMGUI
bool SizeModule::ShowImGui() {
    bool changed = false;

    changed |= UI::Widgets::ToggleSwitch("寿命に応じてサイズを変化", &sizeData_.sizeOverLifetime);
    UI::Tooltip("開始サイズ（メインの「サイズ」）から終了サイズへ、寿命に沿って変化させます");

    {
        UI::Scope::DisabledScope ds(!sizeData_.sizeOverLifetime);

        changed |= UI::Widgets::ToggleSwitch("XYZ個別に設定", &sizeData_.use3DSize);

        if (sizeData_.use3DSize) {
            changed |= UI::DragVec3("終了サイズ", sizeData_.endSize3D, 0.01f, 0.0f, 10.0f);
        } else {
            changed |= UI::DragFloat("終了サイズ", sizeData_.endSize, 0.01f, 0.0f, 10.0f);
        }
        UI::SameLine();
        UI::HelpMarker("寿命が尽きる瞬間のサイズ。\n0 にすると縮みながら消えます。\n開始サイズはメインモジュールの「サイズ」です。");

        // 変化カーブ
        static const char* sizeCurveNames[] = {
            "線形", "加速（イーズイン）", "減速（イーズアウト）", "加減速（イーズインアウト）", "一定（開始サイズを維持）"
        };
        int currentCurve = static_cast<int>(sizeData_.sizeCurve);
        if (ImGui::Combo("変化カーブ", &currentCurve, sizeCurveNames, IM_ARRAYSIZE(sizeCurveNames))) {
            sizeData_.sizeCurve = static_cast<SizeData::SizeCurve>(currentCurve);
            changed = true;
        }

        // サイズ制限
        changed |= UI::DragFloat("最小サイズ", sizeData_.minSize, 0.01f, 0.0f, 1.0f);
        changed |= UI::DragFloat("最大サイズ", sizeData_.maxSize, 0.1f, 1.0f, 100.0f);
    }

    return changed;
}
#endif

float SizeModule::GetLifetimeRatio(const Particle& particle)
{
    if (particle.lifeTime <= 0.0f) {
        return 1.0f; // ライフタイムが0以下の場合は終了扱い
    }
    
    float ratio = particle.currentTime / particle.lifeTime;
    return std::clamp(ratio, 0.0f, 1.0f);
}

float SizeModule::ApplyCurve(float t, SizeData::SizeCurve curve)
{
    switch (curve) {
        case SizeData::SizeCurve::Linear:
            return t;
            
        case SizeData::SizeCurve::EaseIn:
            return t * t; // 二次関数（加速）
            
        case SizeData::SizeCurve::EaseOut:
            return 1.0f - (1.0f - t) * (1.0f - t); // 減速
            
        case SizeData::SizeCurve::EaseInOut:
            if (t < 0.5f) {
                return 2.0f * t * t; // 前半は加速
            } else {
                return 1.0f - 2.0f * (1.0f - t) * (1.0f - t); // 後半は減速
            }
            
        case SizeData::SizeCurve::Constant:
            return 0.0f; // 変化なし（開始サイズを維持）
            
        default:
            return t;
    }
}

float SizeModule::ApplyRandomness(float baseSize, float randomness)
{
    if (randomness <= 0.0f) {
        return baseSize;
    }
    
    // ±randomness の範囲でランダム係数を生成
    float randomFactor = 1.0f + random_.GetFloat(-randomness, randomness);
    
    return baseSize * randomFactor;
}
}
