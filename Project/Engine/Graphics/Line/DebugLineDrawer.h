#pragma once

#include "Line.h"
#include "Math/Vector/Vector3.h"
#include <vector>

// 前方宣言
namespace CoreEngine {
    class LineRendererPipeline;
    class ICamera;
}

/// @brief デバッグ用ライン描画ユーティリティクラス
/// @details 便利なデバッグ描画メソッド群を提供

namespace CoreEngine
{
class DebugLineDrawer {
public:
    /// @brief 単一ラインを即座に描画
    /// @param pipeline LineRendererPipeline
    /// @param camera カメラ
    /// @param line 描画するライン
    static void DrawLine(CoreEngine::LineRendererPipeline* pipeline, const CoreEngine::ICamera* camera, const Line& line);

    /// @brief 複数のラインを即座に描画
    /// @param pipeline LineRendererPipeline
    /// @param camera カメラ
    /// @param lines 描画するライン配列
    static void DrawLines(CoreEngine::LineRendererPipeline* pipeline, const CoreEngine::ICamera* camera, const std::vector<Line>& lines);

    /// @brief 球体をラインで描画
    /// @param pipeline LineRendererPipeline
    /// @param camera カメラ
    /// @param center 球体の中心座標
    /// @param radius 球体の半径
    /// @param color ラインの色
    /// @param alpha ラインの透明度
    /// @param segments 分割数（デフォルト16）
    static void DrawSphere(CoreEngine::LineRendererPipeline* pipeline, const CoreEngine::ICamera* camera, 
        const Vector3& center, float radius, 
        const Vector3& color = {1.0f, 1.0f, 1.0f}, float alpha = 1.0f, int segments = 16);

    /// @brief ボックスをラインで描画
    /// @param pipeline LineRendererPipeline
    /// @param camera カメラ
    /// @param center ボックスの中心座標
    /// @param size ボックスのサイズ
    /// @param color ラインの色
    /// @param alpha ラインの透明度
    static void DrawBox(CoreEngine::LineRendererPipeline* pipeline, const CoreEngine::ICamera* camera, 
        const Vector3& center, const Vector3& size,
        const Vector3& color = {1.0f, 1.0f, 1.0f}, float alpha = 1.0f);

    /// @brief 円をラインで描画
    /// @param pipeline LineRendererPipeline
    /// @param camera カメラ
    /// @param center 円の中心座標
    /// @param radius 円の半径
    /// @param normal 円の法線方向
    /// @param color ラインの色
    /// @param alpha ラインの透明度
    /// @param segments 分割数（デフォルト32）
    static void DrawCircle(CoreEngine::LineRendererPipeline* pipeline, const CoreEngine::ICamera* camera, 
        const Vector3& center, float radius, const Vector3& normal,
        const Vector3& color = {1.0f, 1.0f, 1.0f}, float alpha = 1.0f, int segments = 32);

    /// @brief コーンをラインで描画
    /// @param pipeline LineRendererPipeline
    /// @param camera カメラ
    /// @param apex コーンの頂点
    /// @param direction コーンの方向
    /// @param height コーンの高さ
    /// @param angle コーンの角度（度）
    /// @param color ラインの色
    /// @param alpha ラインの透明度
    /// @param segments 分割数（デフォルト16）
    static void DrawCone(CoreEngine::LineRendererPipeline* pipeline, const CoreEngine::ICamera* camera, 
        const Vector3& apex, const Vector3& direction, float height, float angle, 
        const Vector3& color = {1.0f, 1.0f, 1.0f}, float alpha = 1.0f, int segments = 16);

    /// @brief 円柱をラインで描画
    /// @param pipeline LineRendererPipeline
    /// @param camera カメラ
    /// @param center 円柱の中心座標
    /// @param radius 円柱の半径
    /// @param height 円柱の高さ
    /// @param direction 円柱の方向
    /// @param color ラインの色
    /// @param alpha ラインの透明度
    /// @param segments 分割数（デフォルト16）
    static void DrawCylinder(CoreEngine::LineRendererPipeline* pipeline, const CoreEngine::ICamera* camera, 
        const Vector3& center, float radius, float height, const Vector3& direction,
        const Vector3& color = {1.0f, 1.0f, 1.0f}, float alpha = 1.0f, int segments = 16);

    /// @brief 軸（XYZ）を描画
    /// @param pipeline LineRendererPipeline
    /// @param camera カメラ
    /// @param origin 原点座標
    /// @param length 軸の長さ
    /// @param alpha 透明度
    static void DrawAxes(CoreEngine::LineRendererPipeline* pipeline, const CoreEngine::ICamera* camera, 
        const Vector3& origin = {0.0f, 0.0f, 0.0f}, float length = 1.0f, float alpha = 1.0f);

    /// @brief グリッド（地面）を描画
    /// @param pipeline LineRendererPipeline
    /// @param camera カメラ
    /// @param size グリッドのサイズ
    /// @param divisions 分割数
    /// @param color ラインの色
    /// @param alpha 透明度
    static void DrawGrid(CoreEngine::LineRendererPipeline* pipeline, const CoreEngine::ICamera* camera, 
        float size = 10.0f, int divisions = 10, 
        const Vector3& color = {0.5f, 0.5f, 0.5f}, float alpha = 1.0f);

    /// @brief ラインを生成するヘルパー関数群（バッチング用）
    static std::vector<Line> GenerateSphereLines(const Vector3& center, float radius, 
        const Vector3& color, float alpha, int segments = 16);
    static std::vector<Line> GenerateBoxLines(const Vector3& center, const Vector3& size, 
        const Vector3& color, float alpha);
    static std::vector<Line> GenerateCircleLines(const Vector3& center, float radius, 
        const Vector3& normal, const Vector3& color, float alpha, int segments = 32);
    static std::vector<Line> GenerateConeLines(const Vector3& apex, const Vector3& direction, 
        float height, float angle, const Vector3& color, float alpha, int segments = 16);
    static std::vector<Line> GenerateCylinderLines(const Vector3& center, float radius, 
        float height, const Vector3& direction, const Vector3& color, float alpha, int segments = 16);
};
}
