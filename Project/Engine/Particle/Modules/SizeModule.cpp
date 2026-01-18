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
        Vector3 currentSize = LerpVector3(startSize, sizeData_.endSize3D, curveValue);
        
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

#ifdef _DEBUG
bool SizeModule::ShowImGui() {
    bool changed = false;
    
    // 有効/無効の切り替え
    if (ImGui::Checkbox("有効##サイズ", &enabled_)) {
        changed = true;
    }

    if (!enabled_) {
        ImGui::BeginDisabled();
    }

    ImGui::TextDisabled("注意: 初期サイズはMainModuleで設定してください");
    ImGui::Separator();

    // 3Dサイズ設定
    changed |= ImGui::Checkbox("3Dサイズ使用", &sizeData_.use3DSize);
    
    if (sizeData_.use3DSize) {
     changed |= ImGui::DragFloat3("終了サイズ3D", &sizeData_.endSize3D.x, 0.01f, 0.0f, 10.0f);
  changed |= ImGui::Checkbox("均等スケーリング", &sizeData_.uniformScaling);
    } else {
        changed |= ImGui::DragFloat("終了サイズ", &sizeData_.endSize, 0.01f, 0.0f, 10.0f);
    }
    
    changed |= ImGui::Checkbox("寿命に応じたサイズ変化", &sizeData_.sizeOverLifetime);

  // サイズカーブ設定
    static const char* sizeCurveNames[] = {
        "線形", "イーズイン", "イーズアウト", "イーズインアウト", "一定"
    };
    int currentCurve = static_cast<int>(sizeData_.sizeCurve);
    if (ImGui::Combo("サイズカーブ", &currentCurve, sizeCurveNames, IM_ARRAYSIZE(sizeCurveNames))) {
        sizeData_.sizeCurve = static_cast<SizeData::SizeCurve>(currentCurve);
        changed = true;
    }

    // サイズ制限
    changed |= ImGui::DragFloat("最小サイズ", &sizeData_.minSize, 0.01f, 0.01f, 1.0f);
 changed |= ImGui::DragFloat("最大サイズ", &sizeData_.maxSize, 0.1f, 1.0f, 50.0f);

    if (!enabled_) {
     ImGui::EndDisabled();
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

Vector3 SizeModule::LerpVector3(const Vector3& start, const Vector3& end, float t)
{
    return {
        start.x + (end.x - start.x) * t,
        start.y + (end.y - start.y) * t,
        start.z + (end.z - start.z) * t
    };
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
