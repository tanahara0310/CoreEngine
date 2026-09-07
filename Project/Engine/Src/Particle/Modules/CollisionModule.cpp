#include "pch.h"
#include "CollisionModule.h"
#include "../ParticleSystem.h" // Particle構造体のために必要

#include <algorithm>

namespace CoreEngine
{

CollisionModule::CollisionModule() {
    // 既定は無効。有効かつ planeHeight = 0 が既定だと、床を意識していない
    // 既存のパーティクルが軒並み y = 0 の見えない面に乗ってしまう。
    enabled_ = false;
}

void CollisionModule::ApplyCollision(Particle& particle, float deltaTime)
{
    if (!enabled_) {
        return;
    }

    // 粒のスケールに合わせて、接地位置と転がり半径を伸縮させる
    const float scale = collisionData_.useParticleScale
        ? (std::max)(particle.transform.scale.y, 0.0f)
        : 1.0f;

    const float floorY = collisionData_.planeHeight + collisionData_.contactOffset * scale;
    if (particle.transform.translate.y > floorY) {
        return; // 床の上を飛んでいる
    }

    // めり込んだ分を床の上へ戻す
    particle.transform.translate.y = floorY;

    // 落下を跳ね返す。遅いものは跳ねずに着地させる
    // （反発を掛け続けると、止まりきらずに床の上で震える）
    if (particle.velocity.y < 0.0f) {
        const float impactSpeed = -particle.velocity.y;
        particle.velocity.y = (impactSpeed > collisionData_.restSpeed)
            ? impactSpeed * collisionData_.bounce
            : 0.0f;
    }

    // 接地中の摩擦。横速度を毎秒 friction の割合で削る
    const float decay = (std::max)(0.0f, 1.0f - collisionData_.friction * deltaTime);
    particle.velocity.x *= decay;
    particle.velocity.z *= decay;

    if (!collisionData_.roll) {
        return;
    }

    // 進行方向へ転がす。接地した球が滑らずに転がるときの角速度は ω = (上 × 速度) / 半径。
    // 横速度から毎フレーム作り直すので、摩擦で止まれば回転も一緒に止まる。
    const float radius = (std::max)(collisionData_.rollRadius * scale, 0.001f);
    const Vector3 horizontalVelocity{ particle.velocity.x, 0.0f, particle.velocity.z };
    particle.rotationSpeed =
        Cross(Vector3{ 0.0f, 1.0f, 0.0f }, horizontalVelocity) * (1.0f / radius);
}

#ifdef USE_IMGUI
bool CollisionModule::ShowImGui() {
    bool changed = false;

    UI::Hint("高さを決めた水平な床に粒を乗せます（床は1枚・無限に広い平面）");

    changed |= UI::DragFloat("床の高さ", collisionData_.planeHeight, 0.05f, -100.0f, 100.0f);
    UI::SameLine();
    UI::HelpMarker("床のワールドY座標。\n粒はここより下へ行けなくなります。");

    changed |= UI::DragFloat("接地オフセット", collisionData_.contactOffset, 0.01f, 0.0f, 5.0f);
    UI::SameLine();
    UI::HelpMarker("粒の原点から下面までの距離。\n床にめり込んで見えるときに増やします。");

    changed |= UI::SliderFloat("反発", collisionData_.bounce, 0.0f, 1.0f, "%.2f");
    UI::SameLine();
    UI::HelpMarker("0 で跳ねずに着地、1 で勢いを保ったまま跳ね返ります。");

    changed |= UI::DragFloat("摩擦（1/秒）", collisionData_.friction, 0.1f, 0.0f, 20.0f);
    UI::SameLine();
    UI::HelpMarker("接地中に横向きの速度を削る強さ。\n0 だと滑り続けます。");

    changed |= UI::DragFloat("着地とみなす落下速度", collisionData_.restSpeed, 0.05f, 0.0f, 10.0f);
    UI::SameLine();
    UI::HelpMarker("これ以下の速さで床に当たった粒は跳ねずに止まります。\n小さくしすぎると床の上で細かく震えます。");

    changed |= UI::Widgets::ToggleSwitch("接地中に転がす", &collisionData_.roll);
    UI::Tooltip("進行方向へ転がる回転を与えます（回転モジュールの回転速度を上書きします）");

    {
        UI::Scope::DisabledScope ds(!collisionData_.roll);
        changed |= UI::DragFloat("転がり半径", collisionData_.rollRadius, 0.01f, 0.01f, 5.0f);
        UI::SameLine();
        UI::HelpMarker("小さいほど速く回ります。\n粒の見た目の半径に合わせるのが目安です。");
    }

    changed |= UI::Widgets::ToggleSwitch("粒のスケールを掛ける", &collisionData_.useParticleScale);
    UI::Tooltip("接地オフセットと転がり半径に、粒ごとのスケールYを掛けます");

    return changed;
}
#endif

}
