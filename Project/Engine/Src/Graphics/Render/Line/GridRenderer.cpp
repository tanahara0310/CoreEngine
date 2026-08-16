#include "pch.h"
#include "GridRenderer.h"
#include "Graphics/Render/Line/LineRendererPipeline.h"
#include "EngineSystem/EngineSystem.h"
#include "Camera/Camera.h"
#include "Math/MathCore.h"
#include <algorithm>
#include <cmath>

#ifdef USE_IMGUI
#include "EngineSystem/Subsystem/DebugSubsystem.h"
#include "Utility/Debug/GameDebugUI.h"
#include "Editor/ImGui/ImGuiAll.h"
#endif


namespace CoreEngine
{
namespace {
#ifdef USE_IMGUI
    /// 設定パネルの編集対象（シーンの寿命に縛られるポインタをラムダに持たせないための
    /// ファイルスコープ変数。CollisionMatrixPanel と同じ流儀）
    GridRenderer* s_activeGrid = nullptr;
#endif
}

void GridRenderer::SetBaseSpacing(float spacing)
{
    baseSpacing_ = std::max(spacing, 1e-3f);
}

void GridRenderer::SetHeightAtBaseSpacing(float height)
{
    heightAtBaseSpacing_ = std::max(height, 1e-3f);
}

void GridRenderer::SubmitLines(LineRendererPipeline& pipeline, const Camera* camera)
{
    if (!visible_ || !camera) {
        return;
    }

    // グリッドはビューごとに「そのビューのカメラ」を中心に生成する
    auto lines = GenerateGridLines(camera->GetPosition());
    if (!lines.empty()) {
        pipeline.AddLines(lines);
    }
}

namespace {
    /// @brief カメラを中心とする半径 radius の円で線を切り、縁へ向かって α を落としながら積む
    /// @param alongZ    true なら Z 方向に伸びる線（X が fixedCoord で固定）
    /// @param fixedCoord 線が固定される座標（alongZ なら X、そうでなければ Z）
    /// @details Line は 1 本に 1 つの α しか持てないため、滑らかなフェードには分割が要る。
    ///          セグメントの端点は必ず円周上に来るので、縁は自然に α 0 へ落ちる。
    ///          円で切ることで「板の縁」が直線として見えなくなり、無限に見える。
    void AppendFadedChord(std::vector<Line>& out, const Vector3& cameraPosition,
                          bool alongZ, float fixedCoord, float radius, float alpha,
                          const Vector3& color, float planeY,
                          int segments, float fadeStartRatio, float minAlpha)
    {
        if (alpha <= minAlpha || radius <= 0.0f) {
            return;
        }

        // 線とカメラの垂直距離。これが半径を超えるなら円の外なので描かない
        const float perpendicular = alongZ ? (fixedCoord - cameraPosition.x)
                                           : (fixedCoord - cameraPosition.z);
        const float halfChordSq = radius * radius - perpendicular * perpendicular;
        if (halfChordSq <= 0.0f) {
            return;
        }
        const float halfChord = std::sqrt(halfChordSq);

        // 線が伸びる方向のカメラ座標（弦の中点）
        const float center = alongZ ? cameraPosition.z : cameraPosition.x;
        const float segmentLength = (2.0f * halfChord) / static_cast<float>(segments);
        const float fadeSpan = std::max(1.0f - fadeStartRatio, 1e-3f);

        for (int s = 0; s < segments; ++s) {
            const float t0 = -halfChord + segmentLength * static_cast<float>(s);
            const float t1 = t0 + segmentLength;
            const float tMid = 0.5f * (t0 + t1);

            // セグメント中点のカメラからの距離でフェードを決める
            const float distance = std::sqrt(perpendicular * perpendicular + tMid * tMid);
            const float ratio = distance / radius;
            const float fade = 1.0f - MathCore::SmoothStep((ratio - fadeStartRatio) / fadeSpan);
            const float segmentAlpha = alpha * fade;
            if (segmentAlpha <= minAlpha) {
                continue;
            }

            const Vector3 a = alongZ ? Vector3{ fixedCoord, planeY, center + t0 }
                                     : Vector3{ center + t0, planeY, fixedCoord };
            const Vector3 b = alongZ ? Vector3{ fixedCoord, planeY, center + t1 }
                                     : Vector3{ center + t1, planeY, fixedCoord };
            out.push_back({ a, b, color, segmentAlpha });
        }
    }
}

std::vector<Line> GridRenderer::GenerateGridLines(const Vector3& cameraPosition) const
{
    std::vector<Line> lines;
    lines.reserve(static_cast<size_t>(kLevelCount) * (2 * kHalfLines + 1) * 2 * kFadeSegments);

    // ===== カメラ高度から「最細の段」を十進で決める =====
    // 高度が 10 倍になるごとに 1 段粗くなる。段の小数部 f がクロスフェード率。
    const float height = std::max(std::abs(cameraPosition.y - kGridPlaneYOffset), 1e-3f);
    // baseSpacing_ より細かい段は出さないので 0 でクランプする
    // （クランプ後の値から floor / 小数部を取るので f も自動的に 0 になる）
    const float level = std::max(std::log10(height / heightAtBaseSpacing_), 0.0f);
    const float levelFloor = std::floor(level);
    const float levelFrac = level - levelFloor;

    const float finestSpacing = baseSpacing_ * std::pow(10.0f, levelFloor);

    // 段の α。最細は f で消え、最粗は f で現れる。段が繰り上がる瞬間に
    // 「消えた段」と「現れた段」がちょうど入れ替わるので、見た目は連続になる。
    //
    // 段ごとに濃さを変えることはしない（変えると繰り上がりで階調が飛ぶ）。
    // 10 の倍数の位置は 1 つ粗い段が同じ場所へ重ねて描くため、階層感は
    // 重なりから自動的に生まれる（100 の倍数なら 3 段が重なってさらに濃くなる）。
    const float levelAlphas[kLevelCount] = { 1.0f - levelFrac, 1.0f, levelFrac };

    const float finestRadius = finestSpacing * static_cast<float>(kHalfLines);

    float spacing = finestSpacing;
    float coarsestRadius = finestRadius;
    for (int i = 0; i < kLevelCount; ++i) {
        GridLevel gridLevel;
        gridLevel.spacing = spacing;
        gridLevel.radius = spacing * static_cast<float>(kHalfLines);
        gridLevel.alpha = levelAlphas[i] * brightness_;
        AppendLevel(lines, cameraPosition, gridLevel);

        coarsestRadius = gridLevel.radius;
        spacing *= 10.0f;
    }

    // 軸は最粗の半径まで伸ばす（一番遠くまで見えていてほしい基準線のため）。
    // 垂直な Y 軸だけは最細の段に合わせる（最粗だと足元に巨大な青い柱が立つ）。
    AppendAxes(lines, cameraPosition, coarsestRadius, finestRadius * 0.5f);

    return lines;
}

void GridRenderer::AppendLevel(std::vector<Line>& out, const Vector3& cameraPosition,
                               const GridLevel& level) const
{
    if (level.alpha <= kMinVisibleAlpha) {
        return;
    }

    // カメラ直下を格子間隔にスナップした点を中心にする。スナップにより線の位置は
    // ワールドへ固定され、カメラが動いても模様が滑らない。
    const float centerX = std::round(cameraPosition.x / level.spacing) * level.spacing;
    const float centerZ = std::round(cameraPosition.z / level.spacing) * level.spacing;

    for (int i = -kHalfLines; i <= kHalfLines; ++i) {
        const float offset = static_cast<float>(i) * level.spacing;

        // 原点を通る線は軸ライン（AppendAxes）が専用色で描くのでここでは飛ばす。
        // スナップ後の相対位置ではなくワールド絶対座標で判定すること。
        const float x = centerX + offset;
        if (std::abs(x) > level.spacing * 0.5f) {
            AppendFadedChord(out, cameraPosition, /*alongZ=*/true, x, level.radius,
                             level.alpha, normalColor_, kGridPlaneYOffset,
                             kFadeSegments, kFadeStartRatio, kMinVisibleAlpha);
        }

        const float z = centerZ + offset;
        if (std::abs(z) > level.spacing * 0.5f) {
            AppendFadedChord(out, cameraPosition, /*alongZ=*/false, z, level.radius,
                             level.alpha, normalColor_, kGridPlaneYOffset,
                             kFadeSegments, kFadeStartRatio, kMinVisibleAlpha);
        }
    }
}

void GridRenderer::AppendAxes(std::vector<Line>& out, const Vector3& cameraPosition,
                              float radius, float yAxisHalfHeight) const
{
    const float axisAlpha = kAxisAlpha * brightness_;

    // X 軸（赤）は z=0 の上を X 方向へ、Z 軸（緑）は x=0 の上を Z 方向へ伸びる。
    // 円の外側にあるかどうかは AppendFadedChord が垂直距離で判定する。
    AppendFadedChord(out, cameraPosition, /*alongZ=*/false, 0.0f, radius,
                     axisAlpha, xAxisColor_, kGridPlaneYOffset,
                     kFadeSegments, kFadeStartRatio, kMinVisibleAlpha);
    AppendFadedChord(out, cameraPosition, /*alongZ=*/true, 0.0f, radius,
                     axisAlpha, zAxisColor_, kGridPlaneYOffset,
                     kFadeSegments, kFadeStartRatio, kMinVisibleAlpha);

    // Y 軸（青・垂直）はワールド原点の目印。カメラが原点から離れるほど薄くする
    const float dx = cameraPosition.x;
    const float dz = cameraPosition.z;
    const float originDistance = std::sqrt(dx * dx + dz * dz);
    const float originFade = 1.0f - MathCore::SmoothStep(originDistance / std::max(radius, 1e-3f));
    const float yAlpha = axisAlpha * originFade;
    if (yAlpha > kMinVisibleAlpha) {
        out.push_back({ { 0.0f, kGridPlaneYOffset - yAxisHalfHeight, 0.0f },
                        { 0.0f, kGridPlaneYOffset + yAxisHalfHeight, 0.0f },
                        yAxisColor_, yAlpha });
    }
}

#ifdef USE_IMGUI
void GridRenderer::EnsureSettingsPanelRegistered(EngineSystem* engine)
{
    static bool registered = false;
    if (registered || !engine) {
        return;
    }

    auto* debug = engine->GetDebugSubsystem();
    auto* gameDebugUI = debug ? debug->GetGameDebugUI() : nullptr;
    if (!gameDebugUI) {
        return;
    }

    // ドロワーは何もキャプチャしない（ファイルスコープの s_activeGrid を読むだけ）
    gameDebugUI->RegisterEnginePanel("Grid", [] {
        if (s_activeGrid) {
            s_activeGrid->DrawSettingsImGui();
        } else {
            ImGui::TextDisabled("(グリッドがありません)");
        }
    });

    registered = true;
}

void GridRenderer::SetActiveForSettingsPanel(GridRenderer* grid)
{
    s_activeGrid = grid;
}

bool GridRenderer::DrawSettingsImGui()
{
    bool changed = false;

    UI::SectionHeader("表示");
    if (UI::Widgets::ToggleSwitch("表示", &visible_)) {
        changed = true;
    }

    UI::SectionHeader("スケール");
    ImGui::TextDisabled("カメラ高度に応じて 10 倍ずつ粗い格子へ自動で切り替わります");
    if (UI::DragFloat("最細の間隔 [m]", baseSpacing_, 0.05f, 0.01f, 100.0f)) {
        baseSpacing_ = std::max(baseSpacing_, 1e-3f);
        changed = true;
    }
    if (UI::DragFloat("切替の基準高度 [m]", heightAtBaseSpacing_, 0.5f, 0.1f, 1000.0f)) {
        heightAtBaseSpacing_ = std::max(heightAtBaseSpacing_, 1e-3f);
        changed = true;
    }
    if (UI::DragFloat("濃さ", brightness_, 0.01f, 0.0f, 1.0f)) {
        changed = true;
    }

    UI::SectionHeader("カラー");
    if (UI::ColorEdit3("X 軸色", xAxisColor_)) {
        changed = true;
    }
    if (UI::ColorEdit3("Y 軸色", yAxisColor_)) {
        changed = true;
    }
    if (UI::ColorEdit3("Z 軸色", zAxisColor_)) {
        changed = true;
    }
    if (UI::ColorEdit3("グリッド色", normalColor_)) {
        changed = true;
    }

    return changed;
}
#endif // USE_IMGUI
}
