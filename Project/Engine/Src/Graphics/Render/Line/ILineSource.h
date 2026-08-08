#pragma once

namespace CoreEngine
{
class LineRendererPipeline;
class Camera;

/// @brief Line パス実行時にラインを供給する側のインターフェース。
/// @details グリッドやコライダーワイヤなど「毎フレーム生成する線」を、GameObject に
///          せずに描くための登録口。`LineRendererPipeline::RegisterLineSource()` で登録する。
class ILineSource {
public:
    virtual ~ILineSource() = default;

    /// @brief Line パスのフラッシュ直前に呼ばれる。pipeline へ AddLines すること。
    /// @param camera パスを描いているビューのカメラ（ビューごとに呼ばれる）
    virtual void SubmitLines(LineRendererPipeline& pipeline, const Camera* camera) = 0;
};
}
