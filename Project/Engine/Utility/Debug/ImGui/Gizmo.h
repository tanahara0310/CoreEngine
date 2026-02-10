#pragma once

#include <imgui.h>
#include <ImGuizmo.h>
#include "Engine/Math/Matrix/Matrix4x4.h"

namespace CoreEngine
{
    class GameObject;
    class SpriteObject;
    class ICamera;

    /// @brief ImGuizmo操作クラス
    class Gizmo {
    public:
        /// @brief ギズモの操作モード
        enum class Mode {
            Translate,  // 移動
            Rotate,     // 回転
            Scale       // スケール
        };

        /// @brief フレーム開始時の準備
        /// @param viewportPos ビューポートの位置
        /// @param viewportSize ビューポートのサイズ
        static void Prepare(const ImVec2& viewportPos, const ImVec2& viewportSize);

        /// @brief ギズモを描画し、オブジェクトのトランスフォームを操作（3D用）
        /// @param object 操作対象のオブジェクト
        /// @param camera カメラ
        /// @param mode 操作モード
        /// @return トランスフォームが変更された場合true
        static bool Manipulate(GameObject* object, const ICamera* camera, Mode mode = Mode::Translate);

        /// @brief スプライト用ギズモを描画し、2Dトランスフォームを操作
        /// @param sprite 操作対象のスプライトオブジェクト
        /// @param camera 2Dカメラ
        /// @param mode 操作モード
        /// @return トランスフォームが変更された場合true
        static bool Manipulate2D(SpriteObject* sprite, const ICamera* camera, Mode mode = Mode::Translate);

        /// @brief ギズモが現在操作中かどうか
        /// @return 操作中ならtrue
        static bool IsUsing();

        /// @brief ギズモがホバー中かどうか
        /// @return ホバー中ならtrue
        static bool IsOver();

    private:
        static ImVec2 viewportPos_;
        static ImVec2 viewportSize_;
    };
}
