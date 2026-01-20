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
    /// @param lineRenderer LineRendererPipeline
    void Initialize(LineRendererPipeline* lineRenderer);

    /// @brief ラインを描画（ワールド座標）
    /// @param start 始点のワールド座標
    /// @param end 終点のワールド座標
    /// @param color 色（RGB、デフォルトは白）
    /// @param alpha 透明度（デフォルトは1.0）
    void DrawLine(const Vector3& start, const Vector3& end, 
                  const Vector3& color = {1.0f, 1.0f, 1.0f}, 
                  float alpha = 1.0f);

    /// @brief ラインを描画（Lineオブジェクトから）
    /// @param line ライン
    void DrawLine(const Line& line);

    /// @brief 複数のラインを描画
    /// @param lines ライン配列
    void DrawLines(const std::vector<Line>& lines);

    /// @brief グリッドを描画（XZ平面）
    /// @param size グリッドのサイズ（1辺の長さ）
    /// @param divisions 分割数
    /// @param center グリッドの中心座標
    /// @param color グリッドの色
    /// @param alpha 透明度
    void DrawGrid(float size, int divisions, 
                  const Vector3& center = {0.0f, 0.0f, 0.0f},
                  const Vector3& color = {0.5f, 0.5f, 0.5f}, 
                  float alpha = 1.0f);

    /// @brief ワイヤーフレームボックスを描画
    /// @param center 中心座標
    /// @param size サイズ（各軸の半分の長さ）
    /// @param color 色
    /// @param alpha 透明度
    void DrawWireBox(const Vector3& center, const Vector3& size,
                     const Vector3& color = {1.0f, 1.0f, 0.0f},
                     float alpha = 1.0f);

    /// @brief 軸を描画（デバッグ用）
    /// @param origin 原点
    /// @param length 軸の長さ
    /// @param alpha 透明度
    void DrawAxis(const Vector3& origin = {0.0f, 0.0f, 0.0f}, 
                  float length = 1.0f, 
                  float alpha = 1.0f);

    /// @brief 円を描画（XZ平面）
    /// @param center 中心座標
    /// @param radius 半径
    /// @param segments セグメント数
    /// @param color 色
    /// @param alpha 透明度
    void DrawCircle(const Vector3& center, float radius, int segments = 32,
                    const Vector3& color = {1.0f, 1.0f, 1.0f},
                    float alpha = 1.0f);

    /// @brief 球をワイヤーフレームで描画
    /// @param center 中心座標
    /// @param radius 半径
    /// @param segments セグメント数
    /// @param color 色
    /// @param alpha 透明度
    void DrawWireSphere(const Vector3& center, float radius, int segments = 16,
                        const Vector3& color = {1.0f, 1.0f, 1.0f},
                        float alpha = 1.0f);

    /// @brief クロスマーカーを描画（デバッグ用）
    /// @param position 位置
    /// @param size サイズ
    /// @param color 色
    /// @param alpha 透明度
    void DrawCross(const Vector3& position, float size = 0.1f,
                   const Vector3& color = {1.0f, 0.0f, 0.0f},
                   float alpha = 1.0f);

    /// @brief すべてのラインをクリア
    void ClearAll();

#ifdef _DEBUG
    /// @brief ImGuiデバッグUI描画
    void DrawImGui();

    /// @brief デバッグ用グリッドの有効/無効
    void SetDebugGridEnabled(bool enabled) { debugGridEnabled_ = enabled; }
    bool IsDebugGridEnabled() const { return debugGridEnabled_; }

    /// @brief デバッグ描画を実行（BeginFrame後、Update中に呼び出す）
    void DrawDebugShapes();
#endif

private:
    LineManager() = default;
    ~LineManager() = default;
    LineManager(const LineManager&) = delete;
    LineManager& operator=(const LineManager&) = delete;

    LineRendererPipeline* lineRenderer_ = nullptr;

#ifdef _DEBUG
    // デバッグ描画設定
    struct DebugGridSettings {
        bool enabled = false;
        float size = 20.0f;
        int divisions = 10;
        Vector3 center = {0.0f, -1.0f, 0.0f};
        Vector3 color = {0.5f, 0.5f, 0.5f};
        float alpha = 0.5f;
    };

    struct DebugAxisSettings {
        bool enabled = false;
        Vector3 origin = {0.0f, 0.0f, 0.0f};
        float length = 5.0f;
        float alpha = 1.0f;
    };

    struct DebugBoxSettings {
        bool enabled = false;
        Vector3 center = {3.0f, 1.0f, 0.0f};
        Vector3 size = {2.0f, 2.0f, 2.0f};
        Vector3 color = {1.0f, 1.0f, 0.0f};
        float alpha = 1.0f;
    };

    struct DebugCircleSettings {
        bool enabled = false;
        Vector3 center = {-5.0f, 0.0f, 0.0f};
        float radius = 2.0f;
        int segments = 32;
        Vector3 color = {1.0f, 1.0f, 1.0f};
        float alpha = 1.0f;
    };

    struct DebugSphereSettings {
        bool enabled = false;
        Vector3 center = {-5.0f, 3.0f, -5.0f};
        float radius = 1.5f;
        int segments = 16;
        Vector3 color = {0.0f, 1.0f, 1.0f};
        float alpha = 1.0f;
    };

    struct DebugCrossSettings {
        bool enabled = false;
        Vector3 position = {5.0f, 2.0f, 5.0f};
        float size = 0.5f;
        Vector3 color = {1.0f, 0.0f, 0.0f};
        float alpha = 1.0f;
    };

    DebugGridSettings debugGrid_;
    DebugAxisSettings debugAxis_;
    DebugBoxSettings debugBox_;
    DebugCircleSettings debugCircle_;
    DebugSphereSettings debugSphere_;
    DebugCrossSettings debugCross_;

    bool debugGridEnabled_ = false;
#endif
};
}
