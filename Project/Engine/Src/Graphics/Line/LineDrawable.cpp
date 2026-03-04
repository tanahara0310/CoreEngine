#include "LineDrawable.h"
#include "Graphics/Render/Line/LineRendererPipeline.h"
#include "Camera/ICamera.h"
#include "Math/MathCore.h"


namespace CoreEngine
{
void LineDrawable::Initialize(CoreEngine::LineRendererPipeline* rendererPipeline) {
    rendererPipeline_ = rendererPipeline;
    lines_.clear();
}

void LineDrawable::Update() {
    // ライン管理クラスなので、特に更新処理はない
}

void LineDrawable::Draw(const CoreEngine::ICamera* camera) {
    if (!camera || !rendererPipeline_ || lines_.empty()) {
        return;
    }

    // LineRendererPipelineのバッチシステムを使用
    // AddLinesでバッチに追加するだけで、実際の描画はEndPassで行われる
    rendererPipeline_->AddLines(lines_);
}

void LineDrawable::AddLine(const Line& line) {
    lines_.push_back(line);
}

void LineDrawable::AddLines(const std::vector<Line>& lines) {
    lines_.insert(lines_.end(), lines.begin(), lines.end());
}

void LineDrawable::Clear() {
    lines_.clear();
}
}
