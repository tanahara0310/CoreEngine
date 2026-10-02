#pragma once

#include "Line.h"
#include "Math/Vector/Vector3.h"
#include <vector>
#include <memory>
#include <string>

namespace CoreEngine
{
// 前方宣言
class LineRendererPipeline;

/// @brief ライン描画を簡単に扱うためのマネージャークラス
class LineManager {
public:
    /// @brief インスタンスを取得（シングルトン）
    static LineManager& GetInstance();

    /// @brief 初期化
    void Initialize(LineRendererPipeline* lineRenderer);

    /// @brief ラインを描画（ワールド座標）
    /// @param depthTest false にするとモデルに隠れず常に手前へ描く（骨のデバッグ表示など）
    void DrawLine(const Vector3& start, const Vector3& end,
                  const Vector3& color = {1.0f, 1.0f, 1.0f},
                  float alpha = 1.0f,
                  bool depthTest = true);

    /// @brief クロスマーカーを描画（デバッグ用）
    /// @param depthTest false にするとモデルに隠れず常に手前へ描く
    void DrawCross(const Vector3& position, float size = 0.1f,
                   const Vector3& color = {1.0f, 0.0f, 0.0f},
                   float alpha = 1.0f,
                   bool depthTest = true);

    /// @brief ライン配列を生成するヘルパー関数群（バッチング用）
    static std::vector<Line> GenerateSphereLines(const Vector3& center, float radius,
                                                  const Vector3& color, float alpha, int segments = 16);
    static std::vector<Line> GenerateBoxLines(const Vector3& center, const Vector3& size,
                                               const Vector3& color, float alpha);
    /// @brief 向きを指定して箱の枠を作る（軸は正規化しておくこと）
    static std::vector<Line> GenerateBoxLines(const Vector3& center, const Vector3& size,
                                               const Vector3& axisX, const Vector3& axisY,
                                               const Vector3& axisZ,
                                               const Vector3& color, float alpha);
    static std::vector<Line> GenerateCircleLines(const Vector3& center, float radius,
                                                  const Vector3& normal, const Vector3& color, float alpha, int segments = 32);
    static std::vector<Line> GenerateConeLines(const Vector3& apex, const Vector3& direction,
                                                float height, float angle, const Vector3& color, float alpha, int segments = 16);
    static std::vector<Line> GenerateCylinderLines(const Vector3& center, float radius,
                                                    float height, const Vector3& direction, const Vector3& color, float alpha, int segments = 16);
    /// @brief カプセルの枠を作る（線分の両端に半球を付けた形）
    static std::vector<Line> GenerateCapsuleLines(const Vector3& start, const Vector3& end,
                                                   float radius, const Vector3& color, float alpha,
                                                   int segments = 16);


private:
    LineManager() = default;
    ~LineManager() = default;
    LineManager(const LineManager&) = delete;
    LineManager& operator=(const LineManager&) = delete;

    LineRendererPipeline* lineRenderer_ = nullptr;

};
}
