#pragma once

#ifdef CORE_EDITOR

#include "Camera/CameraSceneStateIO.h"
#include "Editor/Command/EditorCommandStack.h"
#include "GameObject/ObjectId.h"
#include "Reflection/PropertyDescriptor.h"
#include "Utility/JsonManager/JsonManager.h"

#include <string>

namespace CoreEngine
{
    struct AssetInfo;
    class CameraManager;
    class GameObject;
    class GameObjectManager;
}

namespace CoreEngine::Editor
{
    /// @brief プレハブだけを開いて直すモード
    /// @details シーンにプレハブの中身を 1 体置き、ほかのオブジェクトを描かず選べなくして、その 1 体だけを直す。
    ///          置いた 1 体はシーンに保存しない。直した値は編集が止まるたびにプレハブへ書き戻す（自動保存）。
    ///          開いている間の Undo は別の履歴に積み、閉じるとシーンの履歴へ戻して「プレハブを編集」を 1 件足す。
    class PrefabMode
    {
    public:
        /// @brief 開いているか
        bool IsOpen() const noexcept { return objectId_.IsValid(); }

        /// @brief プレハブを開く
        /// @return 開いたら、置いた 1 体（開けなければ nullptr）
        GameObject* Open(GameObjectManager& manager, CameraManager* cameras, const AssetInfo& prefab);

        /// @brief 書き戻してから閉じ、シーンの表示・カメラ・履歴を開く前へ戻す
        void Close(GameObjectManager& manager, CameraManager* cameras);

        /// @brief 編集が止まっていて中身が変わっていれば、プレハブへ書き戻す
        /// @return 置いた 1 体が無くなっていれば false（呼ぶ側が閉じる）
        bool Update(GameObjectManager& manager);

        /// @brief 置いた 1 体（開いていなければ nullptr）
        GameObject* GetObject(const GameObjectManager& manager) const;

        /// @brief 開いているプレハブのファイル名（例 "Box.prefab"）
        const std::string& GetFileName() const noexcept { return fileName_; }

    private:
        /// @brief 置いた 1 体の今の中身をプレハブへ書き戻す（変わっていなければ書かない）
        void Flush(GameObjectManager& manager, const GameObject& object);

        /// 置いた 1 体の ID
        ObjectId objectId_{};

        /// 開いているプレハブ
        Reflection::AssetRefValue prefab_{};
        std::string fileName_;

        /// 開いたときのプレハブの中身と、最後に書き戻した中身
        json opened_;
        json written_;

        /// 開いてから 1 回でも書き戻したか
        bool edited_ = false;

        /// 開く前のシーンの履歴
        EditorCommandStack::History sceneHistory_;

        /// 開く前のカメラ
        CameraSceneState cameraState_{};
        bool hasCameraState_ = false;
        bool usedSceneCamera_ = false;
    };
}

#endif // CORE_EDITOR
