#pragma once

#include "Editor/ImGui/ImGuiAll.h"
#include <ImGuizmo.h>
#include "Math/Matrix/Matrix4x4.h"
#include "UI/UIElement.h"

namespace CoreEngine
{
    class GameObject;
    class SpriteObject;
    class Camera;

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
        static bool Manipulate(GameObject* object, const Camera* camera, Mode mode = Mode::Translate);

        /// @brief スプライト用ギズモを描画し、2Dトランスフォームを操作
        /// @param sprite 操作対象のスプライトオブジェクト
        /// @param camera 2Dカメラ
        /// @param mode 操作モード
        /// @return トランスフォームが変更された場合true
        static bool Manipulate2D(SpriteObject* sprite, const Camera* camera, Mode mode = Mode::Translate);

        /// @brief UI 要素用ギズモを描画し、レイアウトを操作する
        /// @param layout 操作対象のレイアウト。変更はこの引数へ直接書き戻す
        /// @param referenceSize UI の基準解像度（UIRenderer::GetScreenSize）
        /// @param mode 操作モード
        /// @return 変更された場合 true
        /// @details
        ///  UI はカメラに依存しないので、ビューを単位行列、
        ///  射影を「基準解像度 → NDC」の正射影として渡す。
        ///  これは UIRenderer が実際の描画で使うのと同じ変換なので、
        ///  ギズモは画面に出ている UI とぴったり重なる。
        ///
        ///  スケールは `UILayout::size` に対応させてある。
        ///  スプライトの scale と同じ感覚で矩形を伸ばせるようにするため。
        static bool ManipulateUI(UILayout& layout, const Vector2& referenceSize,
            Mode mode = Mode::Translate);

        /// @brief ワールド座標 1 点を移動ギズモで操作する
        /// @param position 操作対象。変更はここへ直接書き戻す
        /// @param camera 視点カメラ
        /// @return 動かされたら true
        /// @details GameObject を持たない編集対象（カメラのキーフレーム位置、注視点など）を
        ///          ビューポート上で直接掴めるようにするためのもの。
        static bool ManipulatePoint(Vector3& position, const Camera* camera);

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
