#pragma once

#ifdef USE_IMGUI

#include "Editor/Camera/Module/CameraEditorContext.h"
#include "Math/Vector/Vector3.h"

struct ImDrawList;

namespace CoreEngine
{
    class Camera;

    /// @brief ビューポート上にカメラの居場所をアイコンで描く
    ///
    /// @details
    /// Unity / Unreal がカメラの位置に出しているアイコンと同じ役割のもの。あちらは
    /// 「常に画面を向くテクスチャ板（ビルボード）」を 3D として描いているが、このエンジンには
    /// ビルボードの仕組みが無いので、ワールド座標を画面座標へ射影して ImGui の
    /// 描画リストへ直接描く。
    ///
    /// この方式の利点はレンダラーへ手を入れずに済むことと、画面上の大きさが距離に
    /// よらず一定になること。遠くのキーも同じ大きさで見えるので、位置の把握に使える。
    ///
    /// 形は手続き的に描くのでテクスチャを持たない。素材のライセンスを気にせず、
    /// どの拡大率でも潰れない。
    class CameraIconOverlay {
    public:
        /// @brief アイコンの描画結果
        struct Hit {
            /// @brief アイコンの画面座標
            float screenX = 0.0f;
            float screenY = 0.0f;

            /// @brief カメラの後ろ・画面外で描かれなかった
            bool culled = true;
        };

        /// @brief ワールド座標をビューポート内の画面座標へ射影する
        /// @param world 変換元
        /// @param camera 覗いているカメラ
        /// @param viewport ビューポートの位置と大きさ
        /// @param outX 画面座標 X
        /// @param outY 画面座標 Y
        /// @return カメラの後ろにある場合は false（描いてはいけない）
        static bool WorldToScreen(const Vector3& world, const Camera& camera,
            const CameraEditorViewport& viewport, float& outX, float& outY);

        /// @brief カメラの形をした印を描く
        /// @param draw 描画リスト
        /// @param screenX,screenY 中心の画面座標
        /// @param forwardAngle 画面上での向き [ラジアン]（レンズが向く方向）
        /// @param scale 大きさ（1.0 で標準の約 22px）
        /// @param fill 本体の色
        /// @param outline 縁の色
        static void DrawCameraGlyph(ImDrawList* draw, float screenX, float screenY,
            float forwardAngle, float scale, unsigned int fill, unsigned int outline);

        /// @brief アイコンをクリック判定する半径 [px]
        static float GetHitRadius(float scale);

        /// @brief 画面上での向きを求める
        /// @details 視点とその少し前方をそれぞれ射影し、画面上のベクトルの角度を取る。
        ///          カメラをほぼ正面/真後ろから見ているときは向きが定まらないので 0 を返す。
        static float ComputeScreenAngle(const Vector3& position, const Vector3& forward,
            const Camera& camera, const CameraEditorViewport& viewport);
    };
}

#endif // USE_IMGUI
