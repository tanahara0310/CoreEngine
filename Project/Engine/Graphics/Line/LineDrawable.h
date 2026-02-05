#pragma once

#include "Line.h"
#include "Engine/ObjectCommon/GameObject.h"
#include <vector>

// 前方宣言
namespace CoreEngine {
    class LineRendererPipeline;
    class ICamera;
}

/// @brief ライン描画用GameObjectクラス
/// @details シーン内で永続的にラインを描画したい場合に使用

namespace CoreEngine
{
class LineDrawable : public GameObject {
public:
    LineDrawable() = default;
    ~LineDrawable() override = default;

    /// @brief 初期化
    /// @param rendererPipeline LineRendererPipelineへのポインタ
    void Initialize(CoreEngine::LineRendererPipeline* rendererPipeline);

    // GameObjectインターフェースの実装
    void Update() override;
    void Draw(const CoreEngine::ICamera* camera) override;
    RenderPassType GetRenderPassType() const override { return RenderPassType::Line; }

#ifdef _DEBUG
    bool DrawImGuiExtended() override { return false; }
    const char* GetObjectName() const override { return "LineDrawable"; }
#endif

    /// @brief ラインを追加
    /// @param line 追加するライン
    void AddLine(const Line& line);

    /// @brief 複数のラインを追加
    /// @param lines 追加するライン配列
    void AddLines(const std::vector<Line>& lines);

    /// @brief 全てのラインをクリア
    void Clear();

    /// @brief ライン数を取得
    /// @return 現在のライン数
    size_t GetLineCount() const { return lines_.size(); }

    /// @brief ラインの配列を取得
    /// @return ライン配列への参照
    const std::vector<Line>& GetLines() const { return lines_; }

private:
    CoreEngine::LineRendererPipeline* rendererPipeline_ = nullptr;
    std::vector<Line> lines_;
};
}
