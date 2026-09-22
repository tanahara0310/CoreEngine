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

    /// @brief ラインを描画（Line オブジェクトから）
    void DrawLine(const Line& line);

    /// @brief 複数のラインを描画
    void DrawLines(const std::vector<Line>& lines);

    /// @brief グリッドを描画（XZ 平面）
    /// @param size グリッド 1 辺の長さ
    void DrawGrid(float size, int divisions, 
                  const Vector3& center = {0.0f, 0.0f, 0.0f},
                  const Vector3& color = {0.5f, 0.5f, 0.5f}, 
                  float alpha = 1.0f);

    /// @brief ワイヤーフレームボックスを描画
    /// @param size 各軸の半分の長さ
    void DrawWireBox(const Vector3& center, const Vector3& size,
                     const Vector3& color = {1.0f, 1.0f, 0.0f},
                     float alpha = 1.0f);

    /// @brief 軸を描画（デバッグ用）
    void DrawAxis(const Vector3& origin = {0.0f, 0.0f, 0.0f}, 
                  float length = 1.0f, 
                  float alpha = 1.0f);

    /// @brief 円を描画（XZ 平面）
    void DrawCircle(const Vector3& center, float radius, int segments = 32,
                    const Vector3& color = {1.0f, 1.0f, 1.0f},
                    float alpha = 1.0f);

    /// @brief 球をワイヤーフレームで描画
    void DrawWireSphere(const Vector3& center, float radius, int segments = 16,
                        const Vector3& color = {1.0f, 1.0f, 1.0f},
                        float alpha = 1.0f);

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

    /// @brief すべてのラインをクリア
    void ClearAll();


private:
    LineManager() = default;
    ~LineManager() = default;
    LineManager(const LineManager&) = delete;
    LineManager& operator=(const LineManager&) = delete;

    LineRendererPipeline* lineRenderer_ = nullptr;

};
}
