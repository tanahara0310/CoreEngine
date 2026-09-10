#include "pch.h"
#include "MapViewComponent.h"

#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "GameObject/Text3D/Text3DObject.h"
#include "MapGeneratorComponent.h"
#include "Components/Utility/BlockModelLayout.h"
#include "Components/Utility/ModelRenderPoolComponent.h"
#include "Camera/Camera.h"
#include "Input/InputAction.h"
#include "Input/InputManager.h"
#include "Text/FontManager.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Random/Hash.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#endif

using namespace CoreEngine;

namespace {
    constexpr std::size_t kDistanceMarkerIntervalMeters = 5;
    constexpr float kDistanceMarkerFontSize = 0.45f;

    // banana_tree.obj の実寸（モデル空間）。原点は根元で Y=0〜1.6、根元の幹は半径 0.2。
    // 風揺れで葉先がどれだけ振れて根元がどれだけ浮くかを、インスペクタへ出すのに使う。
    constexpr float kBananaTreeModelHeight = 1.6f;
    constexpr float kBananaTreeModelBaseRadius = 0.2f;

    // ゲームカメラ（Presets/CameraRigs/GameCamera.json の offset [0, 20, -18]、FOV45度）
    // で 1080p のときの、ワールド 1m あたりの画面ピクセル数の概算。
    // 揺れが画面上で何 px になるかをインスペクタへ出すためだけの目安。
    constexpr float kGameCameraPixelsPerMeter = 49.0f;

    // 同じカメラでの、ワールドの縦方向の変位が画面の縦方向へ残る割合（cos48度）。
    // 見下ろしているぶん、上下の動きは横の動きより読み取りにくい。
    constexpr float kGameCameraVerticalScreenRate = 0.67f;

    // 駅で待つサルのプールの事前生成数。描画範囲に同時に入る未使用の駅の数ぶんあればよく、
    // 足りなければプール側が伸ばす。
    constexpr std::size_t kStationMonkeyPoolCapacity = 8;
}

json GameComponents::MapViewComponent::OnSerialize() const {
    return {
        { "gridSize", gridSize_ },
        { "viewDistanceX", viewDistanceX_ },
        { "groundSkirtHeight", groundSkirtHeight_ },
        { "groundTintStrength", groundTintStrength_ },
        { "groundTintHueSwing", groundTintHueSwing_ },
        { "groundTintFadeStart", groundTintFadeStart_ },
        { "groundTintFadeRange", groundTintFadeRange_ },
        { "stationPopDuration", stationPopDuration_ },
        { "stationPopSquash", stationPopSquash_ },
        { "stationIdleBreath", stationIdleBreath_ },
        { "stationWakeBreath", stationWakeBreath_ },
        { "stationIdleSpeed", stationIdleSpeed_ },
        { "stationWakeSpeed", stationWakeSpeed_ },
        { "stationWakeRange", stationWakeRange_ },
        { "stationUsedTint", stationUsedTint_ },
        { "stationMonkeyScale", stationMonkeyScale_ },
        { "stationMonkeyHop", stationMonkeyHop_ },
        { "stationMonkeyLeap", stationMonkeyLeap_ },
        { "bananaTreeShakeDuration", bananaTreeShakeDuration_ },
        { "bananaTreeShakeLean", bananaTreeShakeLean_ },
        { "bananaTreeShakeSquash", bananaTreeShakeSquash_ },
        { "bananaTreeSwayAngle", bananaTreeSwayAngle_ },
        { "bananaTreeSwaySpeed", bananaTreeSwaySpeed_ },
        { "bananaTreeSwaySubSpeed", bananaTreeSwaySubSpeed_ },
        { "bananaTreeSwaySubRate", bananaTreeSwaySubRate_ },
        { "bananaTreeSwayLean", bananaTreeSwayLean_ },
        { "bananaTreeSwayYaw", bananaTreeSwayYaw_ },
        { "bananaTreeSwayBreath", bananaTreeSwayBreath_ },
        { "bananaTreeWindDirX", bananaTreeWindDirX_ },
        { "bananaTreeWindDirZ", bananaTreeWindDirZ_ },
        { "bananaTreeSinkDepth", bananaTreeSinkDepth_ }
    };
}

void GameComponents::MapViewComponent::OnDeserialize(const json& j) {
    gridSize_ = std::max(0.01f, JsonManager::SafeGet<float>(j, "gridSize", gridSize_));
    viewDistanceX_ = std::max<uint32_t>(1, JsonManager::SafeGet<uint32_t>(j, "viewDistanceX", viewDistanceX_));
    groundSkirtHeight_ = std::max(0.0f,
        JsonManager::SafeGet<float>(j, "groundSkirtHeight", groundSkirtHeight_));
    groundTintStrength_ = std::max(0.0f,
        JsonManager::SafeGet<float>(j, "groundTintStrength", groundTintStrength_));
    groundTintHueSwing_ = std::max(0.0f,
        JsonManager::SafeGet<float>(j, "groundTintHueSwing", groundTintHueSwing_));
    groundTintFadeStart_ = std::max(0.0f,
        JsonManager::SafeGet<float>(j, "groundTintFadeStart", groundTintFadeStart_));
    groundTintFadeRange_ = std::max(0.0f,
        JsonManager::SafeGet<float>(j, "groundTintFadeRange", groundTintFadeRange_));
    stationPopDuration_ = std::max(0.01f,
        JsonManager::SafeGet<float>(j, "stationPopDuration", stationPopDuration_));
    stationPopSquash_ = std::clamp(
        JsonManager::SafeGet<float>(j, "stationPopSquash", stationPopSquash_), 0.0f, 1.0f);
    stationIdleBreath_ = std::clamp(
        JsonManager::SafeGet<float>(j, "stationIdleBreath", stationIdleBreath_), 0.0f, 0.5f);
    stationWakeBreath_ = std::clamp(
        JsonManager::SafeGet<float>(j, "stationWakeBreath", stationWakeBreath_), 0.0f, 0.5f);
    stationIdleSpeed_ = std::max(0.0f,
        JsonManager::SafeGet<float>(j, "stationIdleSpeed", stationIdleSpeed_));
    stationWakeSpeed_ = std::max(0.0f,
        JsonManager::SafeGet<float>(j, "stationWakeSpeed", stationWakeSpeed_));
    stationWakeRange_ = std::max(0.0f,
        JsonManager::SafeGet<float>(j, "stationWakeRange", stationWakeRange_));
    stationUsedTint_ = std::clamp(
        JsonManager::SafeGet<float>(j, "stationUsedTint", stationUsedTint_), 0.0f, 1.0f);
    stationMonkeyScale_ = std::clamp(
        JsonManager::SafeGet<float>(j, "stationMonkeyScale", stationMonkeyScale_), 0.0f, 3.0f);
    stationMonkeyHop_ = std::max(0.0f,
        JsonManager::SafeGet<float>(j, "stationMonkeyHop", stationMonkeyHop_));
    stationMonkeyLeap_ = std::max(0.0f,
        JsonManager::SafeGet<float>(j, "stationMonkeyLeap", stationMonkeyLeap_));
    bananaTreeShakeDuration_ = std::max(0.01f,
        JsonManager::SafeGet<float>(j, "bananaTreeShakeDuration", bananaTreeShakeDuration_));
    bananaTreeShakeLean_ = std::clamp(
        JsonManager::SafeGet<float>(j, "bananaTreeShakeLean", bananaTreeShakeLean_), 0.0f, 1.5f);
    bananaTreeShakeSquash_ = std::clamp(
        JsonManager::SafeGet<float>(j, "bananaTreeShakeSquash", bananaTreeShakeSquash_), 0.0f, 1.0f);
    bananaTreeSwayAngle_ = std::clamp(
        JsonManager::SafeGet<float>(j, "bananaTreeSwayAngle", bananaTreeSwayAngle_), 0.0f, 0.5f);
    bananaTreeSwaySpeed_ = std::max(0.0f,
        JsonManager::SafeGet<float>(j, "bananaTreeSwaySpeed", bananaTreeSwaySpeed_));
    bananaTreeSwaySubSpeed_ = std::max(0.0f,
        JsonManager::SafeGet<float>(j, "bananaTreeSwaySubSpeed", bananaTreeSwaySubSpeed_));
    bananaTreeSwaySubRate_ = std::clamp(
        JsonManager::SafeGet<float>(j, "bananaTreeSwaySubRate", bananaTreeSwaySubRate_), 0.0f, 1.0f);
    bananaTreeSwayLean_ = std::clamp(
        JsonManager::SafeGet<float>(j, "bananaTreeSwayLean", bananaTreeSwayLean_), 0.0f, 0.5f);
    bananaTreeSwayYaw_ = std::clamp(
        JsonManager::SafeGet<float>(j, "bananaTreeSwayYaw", bananaTreeSwayYaw_), 0.0f, 1.5f);
    bananaTreeSwayBreath_ = std::clamp(
        JsonManager::SafeGet<float>(j, "bananaTreeSwayBreath", bananaTreeSwayBreath_), 0.0f, 0.5f);
    bananaTreeWindDirX_ = JsonManager::SafeGet<float>(j, "bananaTreeWindDirX", bananaTreeWindDirX_);
    bananaTreeWindDirZ_ = JsonManager::SafeGet<float>(j, "bananaTreeWindDirZ", bananaTreeWindDirZ_);
    bananaTreeSinkDepth_ = std::clamp(
        JsonManager::SafeGet<float>(j, "bananaTreeSinkDepth", bananaTreeSinkDepth_), 0.0f, 0.5f);
    // 旧データのモデル別高さ・スケールは使わず、共通ブロック寸法から求める。
}

#ifdef USE_IMGUI
bool GameComponents::MapViewComponent::DrawInspector() {
    bool changed = false;
    changed |= ImGui::DragFloat("グリッドサイズ", &gridSize_, 0.05f, 0.01f, 20.0f);
    int distance = static_cast<int>(viewDistanceX_);
    if (ImGui::DragInt("描画距離X", &distance, 1.0f, 1, 500)) { viewDistanceX_ = static_cast<uint32_t>(std::max(distance, 1)); changed = true; }
    ImGui::TextDisabled("共通モデルスケール: %.3f", BlockModelLayout::GetScale(gridSize_));
    ImGui::TextDisabled("接地面の高さ: %.3f", BlockModelLayout::GetSurfaceHeight(gridSize_));

    ImGui::SeparatorText("地面ブロックの伸ばし");
    changed |= ImGui::DragFloat("柱の長さ", &groundSkirtHeight_, 0.05f, 0.0f, 20.0f);
    ImGui::TextDisabled("柱の底: %.2f m",
        BlockModelLayout::GetGroundSkirtBottomHeight(gridSize_, groundSkirtHeight_));
    ImGui::TextDisabled("草が出始める高さ: %.2f m",
        BlockModelLayout::GetGroundSkirtGrassTopHeight(gridSize_, groundSkirtHeight_));
    ImGui::TextDisabled("上面は動かないので他のオブジェクトの高さは変わらない");
    ImGui::TextDisabled("「草が出始める高さ」より下で雲が不透明になっていないと、");
    ImGui::TextDisabled("逆さに吊るした草の緑が見えてしまう（ゲーム設定の Game.Fog.*）");

    ImGui::SeparatorText("地面の色ムラ");
    changed |= ImGui::DragFloat("明度のふり幅", &groundTintStrength_, 0.005f, 0.0f, 1.0f);
    changed |= ImGui::DragFloat("色味のふり幅", &groundTintHueSwing_, 0.005f, 0.0f, 1.0f);
    changed |= ImGui::DragFloat("フェード開始距離", &groundTintFadeStart_, 0.5f, 0.0f, 300.0f);
    changed |= ImGui::DragFloat("フェード距離", &groundTintFadeRange_, 0.5f, 0.0f, 300.0f);
    ImGui::TextDisabled("0 にすると従来どおりの一色になる");
    ImGui::SeparatorText("駅の出現演出");
    changed |= ImGui::DragFloat("駅の反動時間", &stationPopDuration_, 0.01f, 0.01f, 5.0f);
    changed |= ImGui::SliderFloat("駅の沈み込み", &stationPopSquash_, 0.0f, 1.0f);

    ImGui::SeparatorText("駅の待機演出");
    changed |= ImGui::SliderFloat("待機の呼吸", &stationIdleBreath_, 0.0f, 0.3f);
    changed |= ImGui::SliderFloat("接近時の呼吸", &stationWakeBreath_, 0.0f, 0.3f);
    changed |= ImGui::DragFloat("待機の速さ", &stationIdleSpeed_, 0.01f, 0.0f, 5.0f);
    changed |= ImGui::DragFloat("接近時の速さ", &stationWakeSpeed_, 0.01f, 0.0f, 8.0f);
    changed |= ImGui::DragFloat("起き出す距離", &stationWakeRange_, 0.1f, 0.0f, 60.0f);
    changed |= ImGui::SliderFloat("済んだ駅の暗さ", &stationUsedTint_, 0.2f, 1.0f);
    {
        // 屋根がどれだけ画面上で動くかを出す。上下の動きは見下ろしぶん潰れる。
        const float stationHeight =
            BlockModelLayout::kStationModelHeight * BlockModelLayout::GetScale(gridSize_);
        const float pixelsPerBreath =
            stationHeight * kGameCameraPixelsPerMeter * kGameCameraVerticalScreenRate;
        ImGui::TextDisabled("屋根の上下: 待機 %.1f px / 接近 %.1f px",
            stationIdleBreath_ * pixelsPerBreath,
            stationWakeBreath_ * pixelsPerBreath);
        ImGui::TextDisabled("速さは周波数ではなく2本目の波の混ざり方で上げている");
        ImGui::TextDisabled("使い終わった駅は完全に止まる（動いている＝まだ取れる）");
    }

    ImGui::SeparatorText("駅で待つサル");
    changed |= ImGui::SliderFloat("サルの大きさ", &stationMonkeyScale_, 0.0f, 2.0f);
    changed |= ImGui::DragFloat("跳ねる高さ", &stationMonkeyHop_, 0.005f, 0.0f, 1.0f);
    changed |= ImGui::DragFloat("飛び降りる距離", &stationMonkeyLeap_, 0.01f, 0.0f, 3.0f);
    {
        const float hopPixels = stationMonkeyHop_ * gridSize_
            * kGameCameraPixelsPerMeter * kGameCameraVerticalScreenRate;
        ImGui::TextDisabled("跳ねの上下: 待機 %.1f px / 接近 %.1f px",
            hopPixels * 0.35f, hopPixels);
        ImGui::TextDisabled("屋根の頂点は 0.5m 角。大きくしすぎると足元が食み出す");
        ImGui::TextDisabled("0 にするとサルを出さない");
    }

    ImGui::SeparatorText("バナナの木の収穫演出");
    changed |= ImGui::DragFloat("しなりの時間", &bananaTreeShakeDuration_, 0.01f, 0.01f, 5.0f);
    changed |= ImGui::SliderFloat("しなりの角度", &bananaTreeShakeLean_, 0.0f, 1.5f);
    changed |= ImGui::SliderFloat("しなりの縮み", &bananaTreeShakeSquash_, 0.0f, 1.0f);
    ImGui::TextDisabled("サルが取った向きへ倒れて、1.5往復しながら収まる");

    ImGui::SeparatorText("バナナの木の風揺れ");
    changed |= ImGui::SliderFloat("傾きの角度", &bananaTreeSwayAngle_, 0.0f, 0.3f);
    changed |= ImGui::SliderFloat("ねじれの角度", &bananaTreeSwayYaw_, 0.0f, 0.6f);
    changed |= ImGui::SliderFloat("葉の開閉", &bananaTreeSwayBreath_, 0.0f, 0.2f);
    changed |= ImGui::DragFloat("揺れの速さ", &bananaTreeSwaySpeed_, 0.01f, 0.0f, 5.0f);
    changed |= ImGui::DragFloat("2本目の速さ", &bananaTreeSwaySubSpeed_, 0.01f, 0.0f, 5.0f);
    changed |= ImGui::SliderFloat("2本目の割合", &bananaTreeSwaySubRate_, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("風下への傾き", &bananaTreeSwayLean_, 0.0f, 0.2f);
    changed |= ImGui::DragFloat("風向X", &bananaTreeWindDirX_, 0.01f, -1.0f, 1.0f);
    changed |= ImGui::DragFloat("風向Z", &bananaTreeWindDirZ_, 0.01f, -1.0f, 1.0f);
    changed |= ImGui::DragFloat("地面へ埋める深さ", &bananaTreeSinkDepth_, 0.005f, 0.0f, 0.5f);
    {
        // 角度を上げると葉先の振れと一緒に根元の浮きも増える。数字で並べておかないと
        // 「埋める深さ」が足りているかを目で確かめるしかない。
        const float treeScaleForUI = BlockModelLayout::GetScale(gridSize_);
        const float maxAngle = bananaTreeSwayAngle_ + bananaTreeSwayLean_;
        ImGui::TextDisabled("最大 %.1f 度 / 葉先の振れ %.1f cm / 根元の浮き %.1f mm",
            maxAngle * 180.0f / std::numbers::pi_v<float>,
            maxAngle * kBananaTreeModelHeight * treeScaleForUI * 100.0f,
            maxAngle * kBananaTreeModelBaseRadius * treeScaleForUI * 1000.0f);
        ImGui::TextDisabled("埋める深さ %.1f mm。根元の浮きより大きければ隙間は出ない",
            bananaTreeSinkDepth_ * gridSize_ * 1000.0f);
        // 見下ろしカメラだと数 px しか動かないことに気づきにくいので、
        // 画面上で何 px 動くところまで出す。カメラの既定値から求めた概算。
        ImGui::TextDisabled("見下ろしカメラ（27m・FOV45・1080p）で葉先が約 %.0f px 振れる",
            maxAngle * kBananaTreeModelHeight * treeScaleForUI * kGameCameraPixelsPerMeter);
        ImGui::TextDisabled("ねじれと葉の開閉は根元を浮かせないので大きく取れる");
    }
    ImGui::TextDisabled("揺れはマスごとに位相がずれる。速さを 0 にすると止まる");
    return changed;
}
#endif

void GameComponents::MapViewComponent::PlayStationPop(int32_t gridX, int32_t gridZ) {
    // ここが鳴るのは、減速した駅（StationSlowdownEffect）か連結した駅（GameScene）の
    // どちらかだけ。つまり「鳴った駅＝使い終わった駅」なので、待機の呼吸を止める
    // 印もここで付ける。止める瞬間は駅へ着いたときか連結したときかで前後するが、
    // どちらもポップの直後なので「大きく反応して静まる」1つの流れとして読める。
    MarkStationUsed(gridX, gridZ);

    // 同じ駅が続けて鳴ったら、重ねずに頭から鳴らし直す。
    for (auto& pop : stationPops_) {
        if (pop.gridX == gridX && pop.gridZ == gridZ) {
            pop.elapsed = 0.0f;
            return;
        }
    }
    stationPops_.push_back({ gridX, gridZ, 0.0f });
}

void GameComponents::MapViewComponent::PlayBananaTreeShake(
    int32_t gridX, int32_t gridZ, float towardX, float towardZ) {
    // 同じ木が続けて取られたら、重ねずに頭から鳴らし直す。
    // 列車が長いと後続のサルが同じ木を次々に取るので、ここは頻繁に通る。
    for (auto& shake : bananaTreeShakes_) {
        if (shake.gridX == gridX && shake.gridZ == gridZ) {
            shake.towardX = towardX;
            shake.towardZ = towardZ;
            shake.elapsed = 0.0f;
            return;
        }
    }
    bananaTreeShakes_.push_back({ gridX, gridZ, towardX, towardZ, 0.0f });
}

void GameComponents::MapViewComponent::UpdateBananaTreeShakes(float deltaTime) {
    const float safeDeltaTime = std::max(deltaTime, 0.0f);
    for (auto& shake : bananaTreeShakes_) {
        shake.elapsed += safeDeltaTime;
    }
    std::erase_if(bananaTreeShakes_, [this](const BananaTreeShake& shake) {
        return shake.elapsed >= bananaTreeShakeDuration_;
    });
}

void GameComponents::MapViewComponent::ApplyBananaTreeShake(
    std::size_t x, std::size_t z, Vector3& rotate, Vector3& scale) const {
    for (const auto& shake : bananaTreeShakes_) {
        if (shake.gridX < 0 || shake.gridZ < 0 ||
            static_cast<std::size_t>(shake.gridX) != x ||
            static_cast<std::size_t>(shake.gridZ) != z) {
            continue;
        }

        // もぎ取られた向きへ大きくしなり、跳ね返りながら収まる。
        // 減衰する正弦波1.5周期ぶんで、最初の山がサル側への倒れ込みになる。
        const float progress = std::clamp(shake.elapsed / bananaTreeShakeDuration_, 0.0f, 1.0f);
        const float decay = (1.0f - progress) * (1.0f - progress);
        const float wave =
            std::sin(progress * 3.0f * std::numbers::pi_v<float>) * decay;

        // モデルの原点は幹の根元なので、回転させると木がそこを支点に倒れる。
        // 左手系では X 軸回転が +Y を +Z へ、Z 軸回転が +Y を -X へ倒す。
        const float lean = wave * bananaTreeShakeLean_;
        rotate.x += shake.towardZ * lean;
        rotate.z += -shake.towardX * lean;

        // 倒れる向きに関わらず、しなっている間は縦に縮んで横へ広がる。
        const float squash = std::abs(wave) * bananaTreeShakeSquash_;
        scale.y *= 1.0f - squash;
        scale.x *= 1.0f + squash * 0.5f;
        scale.z *= 1.0f + squash * 0.5f;
        return;
    }
}

void GameComponents::MapViewComponent::ApplyBananaTreeSway(
    std::size_t x, std::size_t z, Vector3& rotate, Vector3& scale) const {
    if (bananaTreeSwayAngle_ <= 0.0f && bananaTreeSwayLean_ <= 0.0f &&
        bananaTreeSwayYaw_ <= 0.0f && bananaTreeSwayBreath_ <= 0.0f) {
        return;
    }

    // 位相はマス座標から作る。全部が同位相で揺れると、木ではなく地面ごと揺れて見える。
    // プールの要素番号を種にするのは不可で、描画範囲が 1 マスずれた瞬間に担当要素が
    // ずれて位相が飛ぶ（ModelRenderPoolComponent の entryByPosition_ と同じ罠）。
    // Hash::Cell01 は同じマスなら何フレーム後でも同じ値なので、担当が入れ替わっても
    // 揺れは途切れずに続く。
    const float cellPhase = Hash::Cell01(
        static_cast<std::int32_t>(x), static_cast<std::int32_t>(z))
        * 2.0f * std::numbers::pi_v<float>;

    // ポーズ中は木も止めたいので、timeScale を適用した累積時間を使う。
    const float time = Time::TimeSinceStartup();
    constexpr float tau = 2.0f * std::numbers::pi_v<float>;

    // 正弦1本だとメトロノームに見えるので、割り切れない周期をもう1本重ねて山をばらす。
    // 2本目の位相へ掛ける係数に意味は無く、1本目と山が揃わなければ何でもよい。
    // 返す値は -1..1。傾き・ねじれ・葉の開閉が同じ拍で動くと機械仕掛けに見えるので、
    // それぞれ位相をずらした波を引いて別々の拍にする。
    const auto swayWave = [&](float phaseOffset) {
        const float wave =
            std::sin(time * bananaTreeSwaySpeed_ * tau + cellPhase + phaseOffset) +
            std::sin(time * bananaTreeSwaySubSpeed_ * tau + cellPhase * 1.7f + phaseOffset)
                * bananaTreeSwaySubRate_;
        // 2本足したぶん山が伸びるので、-1..1 に収まるよう割り戻す。
        return wave / (1.0f + bananaTreeSwaySubRate_);
    };

    constexpr float pi = std::numbers::pi_v<float>;

    // ===== 傾き =====
    // 風向が決まらないと倒す先が無いので、長さ 0 なら傾けない。
    const float windLength = std::sqrt(
        bananaTreeWindDirX_ * bananaTreeWindDirX_ +
        bananaTreeWindDirZ_ * bananaTreeWindDirZ_);
    if (windLength > 1e-4f) {
        const float windX = bananaTreeWindDirX_ / windLength;
        const float windZ = bananaTreeWindDirZ_ / windLength;

        // 揺れの中心を風下へ倒しておく。まっすぐ立った木が左右へ振れるより、
        // 風に押されたまま揺れているほうが常夏の島に見える。
        const float lean =
            swayWave(0.0f) * bananaTreeSwayAngle_ + bananaTreeSwayLean_;

        // 倒す向きの作り方は ApplyBananaTreeShake と同じ。
        // 左手系では X 軸回転が +Y を +Z へ、Z 軸回転が +Y を -X へ倒す。
        rotate.x += windZ * lean;
        rotate.z += -windX * lean;
    }

    // ===== ねじれ =====
    // 見下ろしカメラでは傾きの変位が読み取りにくいので、幹を軸に回して
    // シルエットそのものを動かす。banana_tree.obj の葉は前後左右へ非対称に
    // 茂っているので、回すと真上から見ても輪郭が変わる。
    rotate.y += swayWave(pi * 0.5f) * bananaTreeSwayYaw_;

    // ===== 葉の開閉 =====
    // 横へ広げたぶん少し縦を縮める。体積保存の式どおり（縦を横の2乗ぶん縮める）だと
    // 潰れて見えるので、見た目優先で弱く連動させている。
    const float breath = swayWave(pi * 1.25f) * bananaTreeSwayBreath_;
    scale.x *= 1.0f + breath;
    scale.z *= 1.0f + breath;
    scale.y *= 1.0f - breath;
}

void GameComponents::MapViewComponent::UpdateStationPops(float deltaTime) {
    const float safeDeltaTime = std::max(deltaTime, 0.0f);
    for (auto& pop : stationPops_) {
        pop.elapsed += safeDeltaTime;
    }
    std::erase_if(stationPops_, [this](const StationPop& pop) {
        return pop.elapsed >= stationPopDuration_;
    });
}

Vector3 GameComponents::MapViewComponent::GetStationPopScale(
    std::size_t x, std::size_t z) const {
    for (const auto& pop : stationPops_) {
        if (pop.gridX < 0 || pop.gridZ < 0 ||
            static_cast<std::size_t>(pop.gridX) != x ||
            static_cast<std::size_t>(pop.gridZ) != z) {
            continue;
        }

        // サルが飛び出した反動でいったん沈み、跳ね返って収まる。
        // 減衰する正弦波1周期ぶんで、前半が沈み込み、後半が伸び上がりになる。
        const float progress = std::clamp(
            pop.elapsed / stationPopDuration_, 0.0f, 1.0f);
        const float recoil =
            std::sin(progress * 2.0f * std::numbers::pi_v<float>) *
            (1.0f - progress) * stationPopSquash_;
        return { 1.0f + recoil * 0.6f, 1.0f - recoil, 1.0f + recoil * 0.6f };
    }
    return { 1.0f, 1.0f, 1.0f };
}

void GameComponents::MapViewComponent::MarkStationUsed(int32_t gridX, int32_t gridZ) {
    for (const auto& used : usedStations_) {
        if (used.gridX == gridX && used.gridZ == gridZ) {
            return;
        }
    }
    usedStations_.push_back({ gridX, gridZ, 0.0f });
}

const GameComponents::MapViewComponent::UsedStation*
GameComponents::MapViewComponent::FindUsedStation(std::size_t x, std::size_t z) const {
    for (const auto& used : usedStations_) {
        if (used.gridX < 0 || used.gridZ < 0) {
            continue;
        }
        if (static_cast<std::size_t>(used.gridX) == x &&
            static_cast<std::size_t>(used.gridZ) == z) {
            return &used;
        }
    }
    return nullptr;
}

bool GameComponents::MapViewComponent::IsStationUsed(std::size_t x, std::size_t z) const {
    return FindUsedStation(x, z) != nullptr;
}

void GameComponents::MapViewComponent::UpdateUsedStations(float deltaTime) {
    const float safeDeltaTime = std::max(deltaTime, 0.0f);
    for (auto& used : usedStations_) {
        used.elapsed += safeDeltaTime;
    }
}

void GameComponents::MapViewComponent::ForgetUsedStationsBefore(std::size_t startX) {
    // 判定を1マスぶんで切るとカメラの揺れで出入りしうるので、描画距離ぶん余裕を取る。
    // ここまで下がった駅はもう描かれないので、忘れても画に差は出ない。
    std::erase_if(usedStations_, [this, startX](const UsedStation& used) {
        if (used.gridX < 0) {
            return true;
        }
        const std::size_t gridX = static_cast<std::size_t>(used.gridX);
        return gridX + viewDistanceX_ < startX;
    });
}

float GameComponents::MapViewComponent::GetStationApproach(
    std::size_t x, float cameraGridX) const {
    // 通り過ぎた駅も同じだけ離れれば静まるよう、距離は絶対値で測る。
    if (stationWakeRange_ <= 0.0f) {
        return 0.0f;
    }
    const float cells = std::abs(static_cast<float>(x) - cameraGridX);
    return std::clamp(1.0f - cells / stationWakeRange_, 0.0f, 1.0f);
}

float GameComponents::MapViewComponent::GetStationWave(
    std::size_t x, std::size_t z, float approach, float phaseOffset) const {
    // 位相はマス座標から作る。全部が同位相だと駅ではなく地面ごと脈打って見えるし、
    // プールの要素番号を種にすると描画範囲が1マスずれた瞬間に位相が飛ぶ
    // （ModelRenderPoolComponent の entryByPosition_ と同じ罠）。
    const float cellPhase = Hash::Cell01(
        static_cast<std::int32_t>(x), static_cast<std::int32_t>(z))
        * 2.0f * std::numbers::pi_v<float>;

    // ポーズ中は駅も止めたいので、timeScale を適用した累積時間を使う。
    const float time = Time::TimeSinceStartup();
    constexpr float tau = 2.0f * std::numbers::pi_v<float>;

    // 近づくほど速い波へ重心を移す。どちらの波も鳴らしっぱなしで、混ぜる量だけを
    // 動かすので位相は連続したまま。周波数そのものを動かすと変えた瞬間に跳ねる。
    const float slowWeight = 1.0f - approach * 0.5f;
    const float fastWeight = approach;
    const float weightSum = slowWeight + fastWeight; // 1.0〜1.5。0 にはならない
    return (
        std::sin(time * stationIdleSpeed_ * tau + cellPhase + phaseOffset) * slowWeight +
        std::sin(time * stationWakeSpeed_ * tau + cellPhase * 1.7f + phaseOffset) * fastWeight
        ) / weightSum;
}

void GameComponents::MapViewComponent::ApplyStationIdle(
    std::size_t x, std::size_t z, float cameraGridX, Vector3& scale) const {
    if (stationIdleBreath_ <= 0.0f && stationWakeBreath_ <= 0.0f) {
        return;
    }
    // 使い終わった駅は止める。止まっていること自体が「もう取れない」の合図になる。
    if (IsStationUsed(x, z)) {
        return;
    }

    const float approach = GetStationApproach(x, cameraGridX);
    const float wave = GetStationWave(x, z, approach, 0.0f);
    const float breath = wave *
        (stationIdleBreath_ + (stationWakeBreath_ - stationIdleBreath_) * approach);

    // 縦に伸びたら横は縮める。見下ろしでは縦の変位が潰れるので、足元の広がりでも
    // 読ませる。連結ポップ（GetStationPopScale）と同じ 0.6 の連動比にしてあるので、
    // 掛け合わせても同じ体が伸び縮みしているように見える。
    scale.y *= 1.0f + breath;
    scale.x *= 1.0f - breath * 0.6f;
    scale.z *= 1.0f - breath * 0.6f;
}

std::optional<CoreEngine::Vector4> GameComponents::MapViewComponent::GetStationTint(
    std::size_t x, std::size_t z) const {
    if (stationUsedTint_ >= 1.0f) {
        return std::nullopt;
    }
    const UsedStation* used = FindUsedStation(x, z);
    if (!used) {
        return std::nullopt;
    }

    // 済んだ瞬間に色が切り替わると点滅に見えるので、最後のポップと同じ長さを掛けて
    // 落とす。経過時間は駅ごとに持っていて頭から鳴り直さないため、
    // 減速と連結で2回ポップしても明るさが戻ることはない。
    const float progress = std::clamp(
        used->elapsed / stationPopDuration_, 0.0f, 1.0f);
    const float tint = 1.0f + (stationUsedTint_ - 1.0f) * progress;
    // 明るい側は露出で白へ飽和して差が出ないので、暗い側だけで区別を付ける。
    // 青をわずかに残して、ただ暗いのではなく冷めた色に見せる。
    return Vector4{ tint, tint, std::min(1.0f, tint * 1.08f), 1.0f };
}

void GameComponents::MapViewComponent::DrawStationMonkey(
    std::size_t x, std::size_t z, float cameraGridX,
    float roofHeight, float modelScale) {
    if (!stationMonkeyRenderPool_ || stationMonkeyScale_ <= 0.0f) {
        return;
    }

    // 連結したらポップと同じ長さで列車側へ飛び降り、着く前に小さくなって消える。
    // まだ使っていない駅は 0 のまま、屋根の上で跳ね続ける。
    const UsedStation* used = FindUsedStation(x, z);
    const float leap = used
        ? std::clamp(used->elapsed / stationPopDuration_, 0.0f, 1.0f)
        : 0.0f;
    if (used && leap >= 1.0f) {
        return;
    }

    const float approach = GetStationApproach(x, cameraGridX);
    // 駅の呼吸とは拍をずらす。同じ拍だと駅とサルが1つの塊に見える。
    const float wave = GetStationWave(
        x, z, approach, std::numbers::pi_v<float> * 0.5f);
    // 絶対値にすると、屋根へ着地しては跳ね上がる弾みになる（屋根へめり込まない）。
    const float bounce = std::abs(wave);
    const float hop = bounce * stationMonkeyHop_ * gridSize_
        * (0.35f + 0.65f * approach);

    const float monkeyScale = modelScale * stationMonkeyScale_;
    Vector3 scale{ monkeyScale, monkeyScale, monkeyScale };
    // 着地の瞬間だけ軽く潰す。跳ねと同じ波から作るので拍がずれない。
    const float squash = (1.0f - bounce) * 0.12f;
    scale.y *= 1.0f - squash;
    scale.x *= 1.0f + squash * 0.5f;
    scale.z *= 1.0f + squash * 0.5f;

    float offsetY = hop;
    float offsetZ = 0.0f;
    if (leap > 0.0f) {
        // 列車が来るのは -Z 側。弧を描いて前下へ降り、着地する前に消える。
        offsetZ = -stationMonkeyLeap_ * gridSize_ * leap;
        offsetY += (std::sin(leap * std::numbers::pi_v<float>) * 0.35f
            - leap * 1.1f) * gridSize_;
        const float shrink = 1.0f - leap * leap;
        scale.x *= shrink;
        scale.y *= shrink;
        scale.z *= shrink;
    }

    // monkey.obj の正面は -Z。無回転のままでレール側（＝カメラ側）を向く
    // （GameScene が列車のサルへ掛けている回転と同じ前提）。
    stationMonkeyRenderPool_->Draw(
        { x * gridSize_, roofHeight + offsetY, z * gridSize_ + offsetZ },
        { 0.0f, 0.0f, 0.0f },
        scale);
}

void GameComponents::MapViewComponent::Start() {
    auto* owner = GetOwner();
    auto* engine = owner ? owner->GetEngineSystem() : nullptr;
    auto* fontManager = engine ? engine->GetService<FontManager>() : nullptr;
    if (fontManager) {
        MsdfFontDesc fontDesc;
        fontDesc.filePath = L"Engine/Assets/font/x8y12pxDenkiChip.ttf";
        fontDesc.systemFamilyNames = { L"Segoe UI" };
        fontDesc.charsetUtf8 = "0123456789m|";
        distanceMarkerFont_ = fontManager->Acquire(fontDesc);
    }
    if (!distanceMarkerFont_) {
        Logger::GetInstance().Warnf(
            LogCategory::Game, "MapView: 距離目盛り用のフォントを取得できませんでした");
    }

    // 駅で待つサルのプール。他のプールと違って駅の演出だけに使うもので、外から
    // 差し替える意味が無いのでここで生やす（距離目盛りの Text3DObject と同じ扱い）。
    // AddComponent はその場で Awake を呼ぶので、Start の時点で足しても中身は揃う。
    if (owner) {
        stationMonkeyPoolObject_ = owner->Spawn<GameObject>();
        if (stationMonkeyPoolObject_) {
            stationMonkeyPoolObject_->SetName("StationMonkeyPool");
            stationMonkeyPoolObject_->SetSerializeEnabled(false);
            stationMonkeyPoolObject_->AddComponent<TransformComponent>();
            stationMonkeyRenderPool_ =
                stationMonkeyPoolObject_->AddComponent<ModelRenderPoolComponent>(
                    "monkey.obj", kStationMonkeyPoolCapacity, true);
        }
    }
    if (!stationMonkeyRenderPool_) {
        Logger::GetInstance().Warnf(
            LogCategory::Game, "MapView: 駅で待つサルのプールを作れませんでした");
    }
}

void GameComponents::MapViewComponent::OnDestroy() {
    for (auto* marker : distanceMarkers_) {
        if (marker && !marker->IsMarkedForDestroy()) {
            marker->Destroy();
        }
    }
    distanceMarkers_.clear();
    distanceMarkerFont_ = nullptr;

    if (stationMonkeyPoolObject_ && !stationMonkeyPoolObject_->IsMarkedForDestroy()) {
        stationMonkeyPoolObject_->Destroy();
    }
    stationMonkeyPoolObject_ = nullptr;
    stationMonkeyRenderPool_ = nullptr;
}

void GameComponents::MapViewComponent::Update() {
    // マップジェネレーターとグラウンドレンダープールが有効か確認する
    if (mapGenerator_ == nullptr || groundRenderPool_ == nullptr || viewCamera_ == nullptr) {
        return;
    }

    UpdateStationPops(Time::DeltaTime());
    UpdateUsedStations(Time::DeltaTime());
    UpdateBananaTreeShakes(Time::DeltaTime());

    // カメラの注視位置を取得する
    const auto cameraFocusPosition = viewCamera_->GetTranslate();
    // 駅の待機演出は「あと何マスで着くか」で強さが変わる。丸めた値だと
    // 1マスごとに段が付いてしまうので、丸める前のマス座標も取っておく。
    const float cameraGridX = cameraFocusPosition.x / gridSize_;
    const float cameraFocusGridX = std::round(cameraGridX);
    mapViewCenterX_ = cameraFocusGridX > 0.0f
        ? static_cast<uint32_t>(cameraFocusGridX)
        : 0;

    // 描画する範囲を決定する
    size_t startX = (mapViewCenterX_ > viewDistanceX_) ? (mapViewCenterX_ - viewDistanceX_) : 0;
    size_t endX = mapViewCenterX_ + viewDistanceX_;
    ForgetUsedStationsBefore(startX);

    // カメラの先に必要な分だけ、X正方向へマップを延長する
    mapGenerator_->CreateToX(endX);

    Vector3 rotate{ 0.0f, 0.0f, 0.0f };
    const float modelScale = BlockModelLayout::GetScale(gridSize_);
    const Vector3 scale{ modelScale, modelScale, modelScale };
    const float groundHeight = BlockModelLayout::GetGroundHeight(gridSize_);
    const float surfaceHeight = BlockModelLayout::GetSurfaceHeight(gridSize_);

    // 地面ブロックの下へ吊るす柱（スカート）。地面と同じ位置・同じ色のまま、
    // 上下逆さにして底面からぶら下げる。ブロックの上面は動かさない。
    // 逆さにするのは ground.obj の草（緑）を柱の最下部へ回すため。詳細は
    // BlockModelLayout.h の「地面ブロックのスカート」を見ること。
    const bool drawGroundSkirt = groundSkirtRenderPool_ && groundSkirtHeight_ > 0.0f;
    const Vector3 groundSkirtRotate{ std::numbers::pi_v<float>, 0.0f, 0.0f };
    const Vector3 groundSkirtScale{
        modelScale,
        BlockModelLayout::GetGroundSkirtYScale(gridSize_, groundSkirtHeight_),
        modelScale };
    // 水だけは幅1・中心原点の仮モデル box.obj を、1マス幅の薄い水面にする。
    const Vector3 waterScale{ gridSize_, gridSize_ * 0.3f, gridSize_ };

    // マップチップの2D配列を取得する
    const auto& mapChips = mapGenerator_->GetMapChips();
    UpdateDistanceMarkers(startX, std::min(endX, mapChips.size()));
    // 描画範囲内のマップチップを描画する
    for (size_t x = startX; x < endX && x < mapChips.size(); ++x) {
        for (size_t z = 0; z < mapChips[x].size(); ++z) {
            const auto chipType = mapGenerator_->GetMapChip(x, z);
            // チップの種類に応じて描画する。
            // 水以外はすべて地面を敷く。空白マスも「何も無い穴」ではなく、
            // 地面の上に壊せない岩を立てた「敷けない床」として見せる。
            if (chipType != MapChipType::Water) {
                // グラウンドチップの表示。マスごとに色をわずかに散らしてマス目を読めるようにする。
                const Vector3 groundPosition{ x * gridSize_, groundHeight, z * gridSize_ };
                const Vector3 toCamera = groundPosition - cameraFocusPosition;
                const float cameraDistance = std::sqrt(
                    toCamera.x * toCamera.x + toCamera.y * toCamera.y + toCamera.z * toCamera.z);
                const Vector4 groundTint = CalcGroundTint(x, z, cameraDistance);
                groundRenderPool_->Draw(groundPosition, rotate, scale, groundTint);
                // 柱は1本に見せたいので、ブロックと同じ色ムラを掛ける
                if (drawGroundSkirt) {
                    groundSkirtRenderPool_->Draw(
                        groundPosition, groundSkirtRotate, groundSkirtScale, groundTint);
                }
            }

            // 水場チップの表示
            if (waterRenderPool_ && chipType == MapChipType::Water) {
                waterRenderPool_->Draw(
                    { x * gridSize_, 0.0f, z * gridSize_ },
                    rotate,
                    waterScale);
            }

            // 駅チップの表示。まだ使っていない駅は静かに呼吸し、列車が近づくほど
            // 強く速くなる。サルを送り出した直後だけ反動で沈み込み、そのあとは
            // 止まって少し暗くなる（動いている＝まだ取れる、の区別）。
            if(stationRenderPool_ && chipType == MapChipType::Station) {
                const Vector3 popScale = GetStationPopScale(x, z);
                Vector3 stationScale{
                    scale.x * popScale.x, scale.y * popScale.y, scale.z * popScale.z };
                ApplyStationIdle(x, z, cameraGridX, stationScale);
                stationRenderPool_->Draw(
                    { x * gridSize_, surfaceHeight, z * gridSize_ },
                    rotate,
                    stationScale,
                    GetStationTint(x, z));
                // 屋根の上で待つサル。屋根は呼吸で上下するので、乗せる高さは
                // 固定値ではなく今フレームの拡縮から出す（浮かせない）。
                DrawStationMonkey(x, z, cameraGridX,
                    surfaceHeight + BlockModelLayout::kStationModelHeight * stationScale.y,
                    modelScale);
            }

            // 岩チップの表示
            if (rockRenderPool_ && chipType == MapChipType::Resource) {
                rockRenderPool_->Draw({ x * gridSize_, surfaceHeight, z * gridSize_ }, rotate, scale);
            }

            // 空白マスの表示。地面は上で敷いてあるので、その上へ壊せない岩を立てる。
            if (hardRockRenderPool_ && chipType == MapChipType::Void) {
                hardRockRenderPool_->Draw(
                    { x * gridSize_, surfaceHeight, z * gridSize_ }, rotate, scale);
            }

            // バナナの木チップの表示。常に風で揺れ、収穫された直後だけサル側へしなる。
            // 風揺れは常時・小さく、しなりは一瞬・大きい。足し合わせても
            // 「取られた」瞬間が埋もれないよう、風揺れの角度はしなりの 1/5 に抑えてある。
            if (bananaTreeRenderPool_ && chipType == MapChipType::BananaTree) {
                Vector3 treeRotate = rotate;
                Vector3 treeScale = scale;
                ApplyBananaTreeSway(x, z, treeRotate, treeScale);
                ApplyBananaTreeShake(x, z, treeRotate, treeScale);
                // 傾けると根元の底面のフチが持ち上がって地面との間に隙間が出るので、
                // その分だけ沈めて底面をブロックの中へ隠す。
                bananaTreeRenderPool_->Draw(
                    { x * gridSize_,
                      surfaceHeight - bananaTreeSinkDepth_ * gridSize_,
                      z * gridSize_ },
                    treeRotate,
                    treeScale);
            }

            // 草は地面の上に重ねる装飾として描画する。
            if (grassRenderPool_ && chipType == MapChipType::Grass) {
                grassRenderPool_->Draw(
                    { x * gridSize_, surfaceHeight, z * gridSize_ },
                    rotate,
                    scale);
            }
        }
    }
}

CoreEngine::Vector4 GameComponents::MapViewComponent::CalcGroundTint(
    std::size_t x, std::size_t z, float cameraDistance) const {
    // 遠いマスは画面上で数ピクセルまで縮むので、ムラを残すとカメラが動くたびにちらつく。
    // 距離でコントラストを 0 まで落とし、地平線側は元の一色へ戻す。
    float fade = 1.0f;
    if (groundTintFadeRange_ > 0.0f) {
        fade = std::clamp(
            1.0f - (cameraDistance - groundTintFadeStart_) / groundTintFadeRange_,
            0.0f, 1.0f);
    }

    // -1..1 のマス固有の値。これ1つで明度と色味の両方を振る。
    // 毎フレーム同じ値でないと、プールの要素が別のマスへ移った瞬間に色がちらつく。
    const float cell01 = Hash::Cell01(
        static_cast<std::int32_t>(x), static_cast<std::int32_t>(z));
    const float amount = (cell01 * 2.0f - 1.0f) * fade;
    const float luminance = 1.0f + groundTintStrength_ * amount;
    // 明度だけだと白黒のムラに見えるので、青チャンネルだけ逆位相に振って
    // 「明るいマスは色が薄い / 暗いマスは色が濃い」という芝のムラらしさを出す。
    const float blue = luminance * (1.0f + groundTintHueSwing_ * amount);
    return { luminance, luminance, blue, 1.0f };
}

void GameComponents::MapViewComponent::UpdateDistanceMarkers(
    std::size_t startX, std::size_t endX) {
    if (!distanceMarkerFont_) {
        return;
    }

    // X=0を0m、1ワールド単位を1mとする。マスの大きさを変えても間隔は5m。
    const double startMeters = static_cast<double>(startX) * gridSize_;
    const double endMeters = static_cast<double>(endX) * gridSize_;
    const std::size_t firstMarker = std::max<std::size_t>(1,
        static_cast<std::size_t>(std::ceil(startMeters / kDistanceMarkerIntervalMeters)));
    std::size_t usedCount = 0;
    for (std::size_t markerIndex = firstMarker;
        static_cast<double>(markerIndex) * kDistanceMarkerIntervalMeters < endMeters;
        ++markerIndex) {
        if (usedCount == distanceMarkers_.size()) {
            auto* marker = GetOwner()->Spawn<Text3DObject>();
            if (!marker) {
                break;
            }
            marker->Initialize(distanceMarkerFont_, "", "DistanceMarker_" + std::to_string(usedCount));
            // 描画範囲に応じて再配置するため、個々の目盛りはシーンに保存しない。
            marker->SetSerializeEnabled(false);
            marker->SetAlign(TextAlignH::Center, TextAlignV::Top);
            marker->SetPivot({ 0.5f, 0.0f });
            marker->SetLineSpacing(0.9f);
            // 目盛りは地形の外へ寝かせてあるので、背景は雲（SkyFogFeature）だけになる。
            // 純白のままだと雲へ溶けるため、文字は薄い山吹へ、縁取りは黒へ寄せる。
            //
            // 色はリニアで、そのままシーンの HDR バッファへ書く。昼のゲームシーンでは
            // 露出が実効 4.5 倍ほど掛かるので、0.25 を超えた成分は何色を入れても白へ
            // 飽和する（1.0, 0.94, 0.66 は画面では純白と見分けが付かない）。
            // 目安: 0.45 → 247 ／ 0.36 → 242 ／ 0.05 → 155 ／ 0.012 → 67。
            // 下の値で画面上は (247, 242, 155) の淡い山吹、縁は 67 の黒に出る。
            //
            // 縁取りの上限は kMaxOutlineSd * pxRange / glyphPixelSize = 0.45 * 12 / 56
            // ≒ 0.096em。em はフォントサイズ 0.45m ＝ 1080p で約 22px なので、
            // 元の 0.025em では 1px を割っていて縁として見えていなかった。
            marker->SetColor({ 0.45f, 0.36f, 0.05f, 1.0f });
            marker->SetOutline({ 0.012f, 0.012f, 0.018f, 1.0f }, 0.07f);
            marker->SetBillboard(Text3DBillboard::None);
            marker->SetDepthMode(Text3DDepthMode::Test);
            distanceMarkers_.push_back(marker);
        }

        const std::size_t meters = markerIndex * kDistanceMarkerIntervalMeters;
        auto* marker = distanceMarkers_[usedCount++];
        marker->SetText("|\n" + std::to_string(meters) + "m");
        marker->SetFontSize(kDistanceMarkerFontSize);
        auto& transform = marker->GetComponent<TransformComponent>()->Get();
        // ゲームカメラ側（-Z）の地形端より外へ置く。文字面は床と平行で、上から読める向き。
        transform.translate = {
            static_cast<float>(meters),
            BlockModelLayout::GetSurfaceHeight(gridSize_) + 0.015f * gridSize_,
            -0.75f * gridSize_
        };
        transform.rotate = { std::numbers::pi_v<float> * 0.5f, 0.0f, 0.0f };
        transform.TransferMatrix();
        marker->SetActive(true);
    }

    // 戻ったり描画距離を縮めたりした場合は、余った目盛りを隠す。
    for (std::size_t i = usedCount; i < distanceMarkers_.size(); ++i) {
        distanceMarkers_[i]->SetActive(false);
    }
}
