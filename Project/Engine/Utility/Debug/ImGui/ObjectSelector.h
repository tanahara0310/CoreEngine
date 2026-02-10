#pragma once

#include <memory>
#include "Engine/Math/Vector/Vector2.h"
#include "Engine/Math/Vector/Vector3.h"
#include "Engine/Math/Matrix/Matrix4x4.h"
#include "Gizmo.h"

namespace CoreEngine
{
    class GameObject;
    class SpriteObject;
    class ICamera;
    class GameObjectManager;

    /// @brief シーン内のオブジェクト選択と操作を管理するクラス
    class ObjectSelector {
    public:
        /// @brief 初期化
        void Initialize();

        /// @brief 更新処理（マウスクリックによるオブジェクト選択）
        /// @param gameObjectManager ゲームオブジェクトマネージャー
        /// @param camera カメラ
        /// @param mousePos マウス座標（ビューポート座標系）
        /// @param isViewportHovered ビューポートがホバー状態か
        void Update(GameObjectManager* gameObjectManager, const ICamera* camera, 
                    const Vector2& mousePos, bool isViewportHovered);

        /// @brief 2Dオブジェクト用の更新処理（スプライト選択）
        /// @param gameObjectManager ゲームオブジェクトマネージャー
        /// @param camera 2Dカメラ
        /// @param mousePos マウス座標（ビューポート座標系）
        /// @param isViewportHovered ビューポートがホバー状態か
        void Update2D(GameObjectManager* gameObjectManager, const ICamera* camera,
                      const Vector2& mousePos, bool isViewportHovered);

        /// @brief 選択されたオブジェクトにギズモを描画
        /// @param camera カメラ
        void DrawGizmo(const ICamera* camera);

        /// @brief 選択されたスプライトにギズモを描画（2D用）
        /// @param camera 2Dカメラ
        void DrawGizmo2D(const ICamera* camera);

        /// @brief 選択中のオブジェクトを取得
        /// @return 選択中のオブジェクト（nullptrの場合は未選択）
        GameObject* GetSelectedObject() const { return selectedObject_; }

        /// @brief 選択中のスプライトを取得
        /// @return 選択中のスプライト（nullptrの場合は未選択）
        SpriteObject* GetSelectedSprite() const { return selectedSprite_; }

        /// @brief オブジェクトを選択
        /// @param object 選択するオブジェクト
        void SelectObject(GameObject* object) { selectedObject_ = object; selectedSprite_ = nullptr; }

        /// @brief スプライトを選択
        /// @param sprite 選択するスプライト
        void SelectSprite(SpriteObject* sprite) { selectedSprite_ = sprite; selectedObject_ = nullptr; }

        /// @brief 選択を解除
        void ClearSelection() { selectedObject_ = nullptr; selectedSprite_ = nullptr; }

        /// @brief ギズモモードを設定
        /// @param mode ギズモモード
        void SetGizmoMode(Gizmo::Mode mode) { gizmoMode_ = mode; }

        /// @brief ギズモモードを取得
        /// @return 現在のギズモモード
        Gizmo::Mode GetGizmoMode() const { return gizmoMode_; }

    private:
        /// @brief マウス位置からレイを飛ばしてオブジェクトを検出
        /// @param gameObjectManager ゲームオブジェクトマネージャー
        /// @param camera カメラ
        /// @param mousePos マウス座標（ビューポート座標系）
        /// @return 検出されたオブジェクト（nullptrの場合は検出失敗）
        GameObject* RaycastObject(GameObjectManager* gameObjectManager, 
                                  const ICamera* camera, const Vector2& mousePos);

        /// @brief マウス位置からスプライトを検出（2D用）
        /// @param gameObjectManager ゲームオブジェクトマネージャー
        /// @param camera 2Dカメラ
        /// @param mousePos マウス座標（ビューポート座標系）
        /// @return 検出されたスプライト（nullptrの場合は検出失敗）
        SpriteObject* RaycastSprite(GameObjectManager* gameObjectManager,
                                     const ICamera* camera, const Vector2& mousePos);

        /// @brief スクリーン座標をワールド座標に変換（2D用）
        /// @param mousePos マウス座標（0.0〜1.0の正規化座標）
        /// @param camera 2Dカメラ
        /// @return ワールド座標
        Vector2 ScreenToWorld2D(const Vector2& mousePos, const ICamera* camera);

        /// @brief レイとスフィアの交差判定
        /// @param rayOrigin レイの始点
        /// @param rayDirection レイの方向（正規化済み）
        /// @param sphereCenter スフィアの中心座標
        /// @param sphereRadius スフィアの半径
        /// @param distance 交差点までの距離（出力）
        /// @return 交差している場合true
        bool RayIntersectsSphere(const Vector3& rayOrigin, const Vector3& rayDirection,
                                 const Vector3& sphereCenter, float sphereRadius, float& distance);

        /// @brief スクリーン座標からワールド空間のレイを生成
        /// @param mousePos マウス座標（0.0〜1.0の正規化座標）
        /// @param camera カメラ
        /// @param rayOrigin レイの始点（出力）
        /// @param rayDirection レイの方向（出力、正規化済み）
        void ScreenToWorldRay(const Vector2& mousePos, const ICamera* camera,
                              Vector3& rayOrigin, Vector3& rayDirection);

        /// @brief レイと三角形の交差判定（Möller–Trumbore アルゴリズム）
        /// @param rayOrigin レイの始点
        /// @param rayDirection レイの方向（正規化済み）
        /// @param v0 三角形の頂点0
        /// @param v1 三角形の頂点1
        /// @param v2 三角形の頂点2
        /// @param distance 交差点までの距離（出力）
        /// @return 交差している場合true
        bool RayIntersectsTriangle(const Vector3& rayOrigin, const Vector3& rayDirection,
                                   const Vector3& v0, const Vector3& v1, const Vector3& v2,
                                   float& distance);

        /// @brief レイとメッシュの交差判定
        /// @param rayOrigin レイの始点
        /// @param rayDirection レイの方向（正規化済み）
        /// @param object 判定対象のゲームオブジェクト
        /// @param distance 交差点までの距離（出力）
        /// @return 交差している場合true
        bool RayIntersectsMesh(const Vector3& rayOrigin, const Vector3& rayDirection,
                               GameObject* object, float& distance);

        /// @brief 点を行列で変換
        /// @param point 変換する点
        /// @param matrix 変換行列
        /// @return 変換後の点
        Vector3 TransformPoint(const Vector3& point, const Matrix4x4& matrix);

    private:
        GameObject* selectedObject_ = nullptr;         // 選択中の3Dオブジェクト
        SpriteObject* selectedSprite_ = nullptr;       // 選択中のスプライト
        Gizmo::Mode gizmoMode_ = Gizmo::Mode::Translate;  // ギズモモード
    };
}
