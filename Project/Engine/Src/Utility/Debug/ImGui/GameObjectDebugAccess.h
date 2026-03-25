#pragma once

#include "ObjectCommon/GameObject.h"
#include "ObjectCommon/Model/ModelGameObject.h"
#include "ObjectCommon/Sprite/SpriteObject.h"

namespace CoreEngine {
namespace DebugAccess {

    /// @brief デバッグ操作用に GameObject から編集可能トランスフォームを取得するための参照セット
    struct TransformAccess {
        Vector3* translate = nullptr; ///< 位置ベクトルへの参照
        Vector3* rotate = nullptr;    ///< 回転ベクトルへの参照
        Vector3* scale = nullptr;     ///< スケールベクトルへの参照
    };

    /// @brief GameObject が保持する編集可能トランスフォームへの参照を取得する
    /// @param obj 参照取得対象のゲームオブジェクト
    /// @param out 取得結果を格納する参照セット（成功時に各ポインタが有効値に設定される）
    /// @return SpriteObject または ModelGameObject として解決できた場合 true
    /// @note SpriteObject は SpriteTransform、ModelGameObject は WorldTransform を返す。
    inline bool TryGetTransformAccess(GameObject* obj, TransformAccess& out) {
        out = {};
        if (!obj) {
            return false;
        }

        if (auto* spriteObj = dynamic_cast<SpriteObject*>(obj)) {
            out.translate = &spriteObj->GetSpriteTransform().translate;
            out.rotate = &spriteObj->GetSpriteTransform().rotate;
            out.scale = &spriteObj->GetSpriteTransform().scale;
            return true;
        }

        if (auto* modelObj = dynamic_cast<ModelGameObject*>(obj)) {
            out.translate = &modelObj->GetTransform().translate;
            out.rotate = &modelObj->GetTransform().rotate;
            out.scale = &modelObj->GetTransform().scale;
            return true;
        }

        return false;
    }

    /// @brief GameObject を ModelGameObject として取得する
    /// @param obj 判定対象のゲームオブジェクト
    /// @return ModelGameObject の場合はそのポインタ、該当しない場合は nullptr
    inline ModelGameObject* AsModelObject(GameObject* obj) {
        return dynamic_cast<ModelGameObject*>(obj);
    }

} // namespace DebugAccess
} // namespace CoreEngine
