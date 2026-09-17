#pragma once

#ifdef USE_IMGUI

#include "GameObject/ObjectId.h"
#include "Math/Vector/Vector3.h"
#include <string>

namespace CoreEngine
{
    class GameObjectManager;

    /// @brief 1操作分の変更記録
    struct TransformRecord {
        ObjectId    objectId{};    ///< 動かしたオブジェクト
        std::string objectName;    ///< 履歴に出す名前

        Vector3 translateBefore;
        Vector3 rotateBefore;
        Vector3 scaleBefore;
        bool    activeBefore = true;

        Vector3 translateAfter;
        Vector3 rotateAfter;
        Vector3 scaleAfter;
        bool    activeAfter = true;
    };

    /// @brief オブジェクトのスポーン（生成）操作の記録
    /// @note Undo でオブジェクトを削除し、Redo で同じオブジェクトを再生成する
    struct ObjectSpawnRecord {
        ObjectId    objectId{};    ///< 生成されたオブジェクト
        std::string objectName;    ///< 生成されたオブジェクト名
        std::string serializeKey;  ///< 生成されたオブジェクトの保存キー
        std::string modelPath;     ///< 生成したオブジェクトのモデルファイル
        Vector3     translate;     ///< 生成時のトランスフォーム
        Vector3     rotate;
        Vector3     scale = { 1.0f, 1.0f, 1.0f };
    };

    /// @brief シーン操作を `EditorCommandStack` へ積むための入口
    /// @details 履歴そのものはエディタ共通の単一スタックが持つ。積んだ操作は、
    ///          今開いているシーンから ID でオブジェクトを引いて値を戻す。
    class UndoRedoHistory {
    public:
        /// @brief トランスフォーム操作を履歴に追加（before == after の場合は記録しない）
        void Push(const TransformRecord& record);

        /// @brief スポーン操作を履歴に追加
        void Push(const ObjectSpawnRecord& record);

        /// @brief 一つ前の状態に戻す
        /// @return 実行できた場合 true
        bool Undo();

        /// @brief Undo を取り消す
        /// @return 実行できた場合 true
        bool Redo();

        bool CanUndo() const;
        bool CanRedo() const;

        /// @brief 履歴をすべてクリア（シーン切り替え時など）
        void Clear();

        int GetUndoCount() const;
        int GetRedoCount() const;
    };
}

#endif // USE_IMGUI
