#include "GridRenderer.h"
#include "Engine/Graphics/Render/Line/LineRendererPipeline.h"
#include "Engine/Graphics/Render/RenderManager.h"
#include "Engine/EngineSystem/EngineSystem.h"
#include "Engine/Camera/ICamera.h"
#include "MathCore.h"
#include <cmath>

#ifdef _DEBUG
#include <imgui.h>
#endif


namespace CoreEngine
{
void GridRenderer::Initialize()
{
    SetActive(true);
}

void GridRenderer::Update()
{
    // グリッドは静的なので特に更新処理なし
}

void GridRenderer::Draw(const ICamera* camera)
{
    if (!visible_ || !camera) {
        return;
    }

    // カメラ位置を取得
    Vector3 cameraPos = camera->GetPosition();

    // グリッドラインを生成
    auto lines = GenerateGridLines(cameraPos);

    if (lines.empty()) {
        return;
    }

    // LineRendererPipelineを取得
    auto engineSystem = GetEngineSystem();
    if (!engineSystem) return;

    auto renderManager = engineSystem->GetComponent<RenderManager>();
    if (!renderManager) return;

    auto pipeline = static_cast<LineRendererPipeline*>(
        renderManager->GetRenderer(RenderPassType::Line));

    if (!pipeline) return;

    // バッチに追加（パス内で自動的に描画される）
    pipeline->AddLines(lines);
}

std::vector<Line> GridRenderer::GenerateGridLines(const Vector3& cameraPosition)
{
    std::vector<Line> lines;

    // カメラ位置を中心にグリッドを生成（グリッド間隔にスナップ）
    const float snapX = std::floor(cameraPosition.x / spacing_) * spacing_;
    const float snapZ = std::floor(cameraPosition.z / spacing_) * spacing_;

    // グリッドの範囲を計算
    const int halfCount = static_cast<int>(gridSize_ / spacing_);
    const float startX = snapX - halfCount * spacing_;
    const float endX = snapX + halfCount * spacing_;
    const float startZ = snapZ - halfCount * spacing_;
    const float endZ = snapZ + halfCount * spacing_;
    
    // Y軸の高さ範囲
    const float yMin = -gridSize_ * 0.5f;
    const float yMax = gridSize_ * 0.5f;

    // === 原点を通る軸ライン（X/Y/Z） ===
    // 軸ラインは常に完全不透明
    // X軸（赤）: Z方向に伸びる
    lines.push_back({ { 0.0f, 0.0f, startZ }, { 0.0f, 0.0f, endZ }, xAxisColor_, kAxisAlpha });
    
    // Y軸（青）: 垂直方向
    lines.push_back({ { 0.0f, yMin, 0.0f }, { 0.0f, yMax, 0.0f }, yAxisColor_, kAxisAlpha });
    
    // Z軸（緑）: X方向に伸びる
    lines.push_back({ { startX, 0.0f, 0.0f }, { endX, 0.0f, 0.0f }, zAxisColor_, kAxisAlpha });

    // === グリッドライン生成（距離ベースフェード対応） ===
    const float majorBlendFactor = 0.3f; // 太いラインの色ブレンド率
    
    // フェード計算用のパラメータ
    const float fadeRange = fadeEndDistance_ - fadeStartDistance_;
    
    for (int i = -halfCount; i <= halfCount; ++i) {
        const float x = snapX + i * spacing_;
        const float z = snapZ + i * spacing_;
        
        const int xLineIndex = static_cast<int>(std::round(x / spacing_));
        const int zLineIndex = static_cast<int>(std::round(z / spacing_));
        
        // X軸方向のライン（Z軸に平行）
        if (xLineIndex != 0) { // 原点は既に描画済み
            // カメラからの距離を計算（X座標の差分）
            const float distanceX = std::abs(x - cameraPosition.x);
            
            // 距離に基づくフェード率を計算（0.0 = 完全不透明、1.0 = 完全透明）
            float fadeFactor = 0.0f;
            if (distanceX > fadeStartDistance_) {
                fadeFactor = (distanceX - fadeStartDistance_) / fadeRange;
                fadeFactor = std::clamp(fadeFactor, 0.0f, 1.0f);
            }
            
            const bool isMajorX = (xLineIndex % majorLineInterval_ == 0);
            const Vector3 colorX = isMajorX 
                ? Vector3{
                    xAxisColor_.x * (1.0f - majorBlendFactor) + normalColor_.x * majorBlendFactor,
                    xAxisColor_.y * (1.0f - majorBlendFactor) + normalColor_.y * majorBlendFactor,
                    xAxisColor_.z * (1.0f - majorBlendFactor) + normalColor_.z * majorBlendFactor
                }
                : normalColor_;
            
            // α値にフェードを適用
            const float baseAlpha = isMajorX ? kMajorAlpha : kNormalAlpha;
            const float alphaX = baseAlpha * (1.0f - fadeFactor);
            
            // 完全に透明になったラインはスキップ
            if (alphaX > 0.01f) {
                lines.push_back({ { x, 0.0f, startZ }, { x, 0.0f, endZ }, colorX, alphaX });
            }
        }
        
        // Z軸方向のライン（X軸に平行）
        if (zLineIndex != 0) { // 原点は既に描画済み
            // カメラからの距離を計算（Z座標の差分）
            const float distanceZ = std::abs(z - cameraPosition.z);
            
            // 距離に基づくフェード率を計算
            float fadeFactor = 0.0f;
            if (distanceZ > fadeStartDistance_) {
                fadeFactor = (distanceZ - fadeStartDistance_) / fadeRange;
                fadeFactor = std::clamp(fadeFactor, 0.0f, 1.0f);
            }
            
            const bool isMajorZ = (zLineIndex % majorLineInterval_ == 0);
            const Vector3 colorZ = isMajorZ
                ? Vector3{
                    zAxisColor_.x * (1.0f - majorBlendFactor) + normalColor_.x * majorBlendFactor,
                    zAxisColor_.y * (1.0f - majorBlendFactor) + normalColor_.y * majorBlendFactor,
                    zAxisColor_.z * (1.0f - majorBlendFactor) + normalColor_.z * majorBlendFactor
                }
                : normalColor_;
            
            // α値にフェードを適用
            const float baseAlpha = isMajorZ ? kMajorAlpha : kNormalAlpha;
            const float alphaZ = baseAlpha * (1.0f - fadeFactor);
            
            // 完全に透明になったラインはスキップ
            if (alphaZ > 0.01f) {
                lines.push_back({ { startX, 0.0f, z }, { endX, 0.0f, z }, colorZ, alphaZ });
            }
        }
    }

    return lines;
}

#ifdef _DEBUG
bool GridRenderer::DrawImGui()
{
    bool changed = false;

    if (ImGui::TreeNode("GridRenderer##GridSettings")) {
        // 表示/非表示
        if (ImGui::Checkbox("表示", &visible_)) {
            changed = true;
        }

        // グリッドサイズ
        if (ImGui::DragFloat("グリッドサイズ", &gridSize_, 1.0f, 10.0f, 500.0f)) {
            changed = true;
        }

        // グリッド間隔
        if (ImGui::DragFloat("間隔", &spacing_, 0.1f, 0.1f, 10.0f)) {
            changed = true;
        }

        // 太いラインの間隔
        if (ImGui::DragInt("太いライン間隔", &majorLineInterval_, 1, 2, 20)) {
            changed = true;
        }

        ImGui::Separator();
        ImGui::Text("フェード設定");

        // フェード開始距離
        if (ImGui::DragFloat("フェード開始距離", &fadeStartDistance_, 1.0f, 0.0f, fadeEndDistance_)) {
            changed = true;
        }

        // フェード終了距離
        if (ImGui::DragFloat("フェード終了距離", &fadeEndDistance_, 1.0f, fadeStartDistance_, gridSize_)) {
            changed = true;
        }

        ImGui::Separator();

        // 軸の色設定
        if (ImGui::ColorEdit3("X軸色（赤）", &xAxisColor_.x)) {
            changed = true;
        }

        if (ImGui::ColorEdit3("Y軸色（青）", &yAxisColor_.x)) {
            changed = true;
        }

        if (ImGui::ColorEdit3("Z軸色（緑）", &zAxisColor_.x)) {
            changed = true;
        }

        if (ImGui::ColorEdit3("グリッド色", &normalColor_.x)) {
            changed = true;
        }

        ImGui::TreePop();
    }

    return changed;
}
#endif
}
