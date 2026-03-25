#pragma once

#include "Utility/JsonManager/JsonManager.h"

namespace CoreEngine
{
    class ModelGameObject;
    class SpriteObject;

    /// @brief ゲームオブジェクトのJSON読み書きを担当するシリアライザ
    class SceneSerializer {
    public:
        /// @brief オブジェクトのデータ（Transform + active）を JSON に書き出す
        static void SaveObject(const ModelGameObject* obj, json& j);

        /// @brief JSON からオブジェクトのデータ（Transform + active）を復元する
        static void LoadObject(ModelGameObject* obj, const json& j);

        /// @brief スプライトオブジェクトのデータを JSON に書き出す
        static void SaveSpriteObject(const SpriteObject* obj, json& j);

        /// @brief JSON からスプライトオブジェクトのデータを復元する
        static void LoadSpriteObject(SpriteObject* obj, const json& j);
    };
}
