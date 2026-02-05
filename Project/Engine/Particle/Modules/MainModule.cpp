#include "MainModule.h"
#include <algorithm>
#include <cmath>

#ifdef _DEBUG
#include "Engine/Utility/Debug/ImGui/ImguiManager.h"
#endif


namespace CoreEngine
{
MainModule::MainModule() {
    // デフォルト値を設定
    mainData_.duration = 5.0f;
    mainData_.looping = true;
    mainData_.playOnAwake = true;
    mainData_.maxParticles = 1000;
    mainData_.simulationSpace = SimulationSpace::World;

    mainData_.startLifetime = 1.0f;
    mainData_.startLifetimeRandomness = 0.0f;

    mainData_.startSpeed = 5.0f;
    mainData_.startSpeedRandomness = 0.0f;

    mainData_.startSize = { 1.0f, 1.0f, 1.0f };
    mainData_.startSizeRandomness = 0.0f;

    mainData_.startRotation = { 0.0f, 0.0f, 0.0f };
    mainData_.startRotationRandomness = 0.0f;

    mainData_.startColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    mainData_.startColorRandomness = 0.0f;

    mainData_.gravityModifier = 0.0f;
}

void MainModule::Play() {
    isPlaying_ = true;
}

void MainModule::Stop() {
    isPlaying_ = false;
}

void MainModule::Restart() {
    elapsedTime_ = 0.0f;
    isPlaying_ = true;
}

void MainModule::UpdateTime(float deltaTime) {
    if (!enabled_) {
        return;
    }

    // 再生中のみ時間更新
    if (isPlaying_) {
        elapsedTime_ += deltaTime;

        // ループ処理
        if (mainData_.looping && elapsedTime_ >= mainData_.duration) {
            elapsedTime_ = 0.0f;  // 時間をリセット
        }

        // ループしない場合、持続時間を超えたら停止
        if (!mainData_.looping && elapsedTime_ >= mainData_.duration) {
            isPlaying_ = false;
        }
    }
}

bool MainModule::IsFinished() const {
    if (mainData_.looping) {
        return false;  // ループする場合は終了しない
    }
    return !isPlaying_ && elapsedTime_ >= mainData_.duration;
}

float MainModule::GenerateStartLifetime() const {
    return ApplyRandomness(mainData_.startLifetime, mainData_.startLifetimeRandomness);
}

float MainModule::GenerateStartSpeed() const {
    return ApplyRandomness(mainData_.startSpeed, mainData_.startSpeedRandomness);
}

Vector3 MainModule::GenerateStartSize() const {
    float randomness = mainData_.startSizeRandomness;
    return {
        ApplyRandomness(mainData_.startSize.x, randomness),
     ApplyRandomness(mainData_.startSize.y, randomness),
        ApplyRandomness(mainData_.startSize.z, randomness)
    };
}

Vector3 MainModule::GenerateStartRotation() const {
    float randomness = mainData_.startRotationRandomness;
    // 度からラジアンに変換
    constexpr float kDegToRad = 3.14159265f / 180.0f;
    return {
 ApplyRandomness(mainData_.startRotation.x, randomness) * kDegToRad,
        ApplyRandomness(mainData_.startRotation.y, randomness) * kDegToRad,
        ApplyRandomness(mainData_.startRotation.z, randomness) * kDegToRad
    };
}

Vector4 MainModule::GenerateStartColor() const {
    float randomness = mainData_.startColorRandomness;
    return {
        ApplyRandomness(mainData_.startColor.x, randomness),
  ApplyRandomness(mainData_.startColor.y, randomness),
        ApplyRandomness(mainData_.startColor.z, randomness),
      ApplyRandomness(mainData_.startColor.w, randomness)
    };
}

float MainModule::ApplyRandomness(float base, float randomness) const {
    if (randomness <= 0.0f) {
        return base;
    }

    // -randomness ～ +randomness の範囲でランダム値を生成
    float range = base * randomness;
    float randomValue = random_.GetFloat(-range, range);
    float result = base + randomValue;
    return (result < 0.0f) ? 0.0f : result;  // 負の値を防ぐ
}

#ifdef _DEBUG
bool MainModule::ShowImGui() {
    bool changed = false;

    // 有効/無効の切り替え
    if (ImGui::Checkbox("有効##Main", &enabled_)) {
        changed = true;
    }

    if (!enabled_) {
        ImGui::BeginDisabled();
    }

    // システム設定
    ImGui::Separator();
    ImGui::Text("システム設定");
    
    changed |= ImGui::DragFloat("持続時間", &mainData_.duration, 0.1f, 0.1f, 60.0f, "%.1f秒");
    ImGui::TextDisabled("パーティクルシステム全体の動作時間");
    ImGui::TextDisabled("注意: バーストタイミングがこの時間を超える場合、");
    ImGui::TextDisabled("   持続時間到達時に強制的にバーストが発生します");
    
    changed |= ImGui::Checkbox("ループ", &mainData_.looping);
    ImGui::TextDisabled("持続時間後に自動的にリセットして再開");
    
    changed |= ImGui::Checkbox("起動時に再生", &mainData_.playOnAwake);
    
    changed |= ImGui::DragInt("最大パーティクル数", reinterpret_cast<int*>(&mainData_.maxParticles), 10, 1, 10000);

    // シミュレーション空間
    {
        const char* spaceNames[] = { "ローカル", "ワールド" };
        int currentSpace = static_cast<int>(mainData_.simulationSpace);
        if (ImGui::Combo("シミュレーション空間", &currentSpace, spaceNames, IM_ARRAYSIZE(spaceNames))) {
            mainData_.simulationSpace = static_cast<SimulationSpace>(currentSpace);
            changed = true;
        }
        ImGui::TextDisabled("ローカル: エミッターに追従 / ワールド: 独立");
    }

    // 初期寿命
    ImGui::Separator();
    ImGui::Text("初期寿命");
    changed |= ImGui::DragFloat("寿命", &mainData_.startLifetime, 0.1f, 0.01f, 10.0f, "%.2f秒");
    ImGui::TextDisabled("各パーティクルが消えるまでの時間");
    ImGui::TextDisabled("注意: 持続時間（duration）とは別の設定です");
    
    changed |= ImGui::SliderFloat("ランダム性##Lifetime", &mainData_.startLifetimeRandomness, 0.0f, 1.0f, "%.2f");

    // 初期速度
    ImGui::Separator();
    ImGui::Text("初期速度");
    changed |= ImGui::DragFloat("速度", &mainData_.startSpeed, 0.1f, 0.0f, 50.0f, "%.1f");
    changed |= ImGui::SliderFloat("ランダム性##Speed", &mainData_.startSpeedRandomness, 0.0f, 1.0f, "%.2f");

    // 初期サイズ
    ImGui::Separator();
    ImGui::Text("初期サイズ");
    changed |= ImGui::DragFloat3("サイズ", &mainData_.startSize.x, 0.1f, 0.1f, 10.0f);
    changed |= ImGui::SliderFloat("ランダム性##Size", &mainData_.startSizeRandomness, 0.0f, 1.0f, "%.2f");

    // 初期回転
    ImGui::Separator();
    ImGui::Text("初期回転");
    changed |= ImGui::DragFloat3("回転（度）", &mainData_.startRotation.x, 1.0f, -180.0f, 180.0f);
    changed |= ImGui::SliderFloat("ランダム性##Rotation", &mainData_.startRotationRandomness, 0.0f, 1.0f, "%.2f");

    // 初期色
    ImGui::Separator();
    ImGui::Text("初期色");
    changed |= ImGui::ColorEdit4("色", &mainData_.startColor.x);
    changed |= ImGui::SliderFloat("ランダム性##Color", &mainData_.startColorRandomness, 0.0f, 1.0f, "%.2f");

    // 重力
    ImGui::Separator();
    ImGui::Text("物理設定");
    changed |= ImGui::DragFloat("重力の影響", &mainData_.gravityModifier, 0.1f, -2.0f, 2.0f, "%.1f");
    ImGui::TextDisabled("0.0=無効, 1.0=通常, 負の値=上昇");

    if (!enabled_) {
        ImGui::EndDisabled();
    }

    return changed;
}
#endif
}
