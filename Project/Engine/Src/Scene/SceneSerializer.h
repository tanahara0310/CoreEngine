#pragma once

#include "Utility/JsonManager/JsonManager.h"

namespace CoreEngine
{
    class GameObject;
    class SpriteObject;

    /// @brief ゲームオブジェクトのJSON読み書きを担当するシリアライザ
    /// @note GameObject.h に json の依存を持ち込まないよう分離
    class SceneSerializer {
    public:
        /// @brief オブジェクトのデータ（Transform + active）を JSON に書き出す
        /// @param obj 対象オブジェクト（const）
        /// @param j 書き込み先 JSON オブジェクト
        static void SaveObject(const GameObject* obj, json& j);

        /// @brief JSON からオブジェクトのデータ（Transform + active）を復元する
        /// @param obj 復元対象オブジェクト
        /// @param j 読み込み元 JSON オブジェクト
        static void LoadObject(GameObject* obj, const json& j);

        /// @brief スプライトオブジェクトのデータを JSON に書き出す
        /// @param obj 対象スプライト（const）
        /// @param j 書き込み先 JSON オブジェクト
        static void SaveSpriteObject(const SpriteObject* obj, json& j);

        /// @brief JSON からスプライトオブジェクトのデータを復元する
        /// @param obj 復元対象スプライト
        /// @param j 読み込み元 JSON オブジェクト
        static void LoadSpriteObject(SpriteObject* obj, const json& j);
    };
}
