#pragma once

#ifdef USE_IMGUI

#include "Math/Vector/Vector3.h"
#include <functional>
#include <string>

namespace CoreEngine
{
    class GameObjectManager;

    /// @brief 1操作分の変更記録
    struct TransformRecord {
        std::string objectName;

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
        std::string objectName;    ///< 生成されたオブジェクト名
        std::string modelPath;     ///< 生成したオブジェクトのモデルファイル
        Vector3     translate;     ///< 生成時のトランスフォーム
        Vector3     rotate;
        Vector3     scale = { 1.0f, 1.0f, 1.0f };
    };

    /// @brief シーン操作を `EditorCommandStack` へ積むための入口
    /// @details 履歴そのものはエディタ共通の単一スタックが持つ。ここは
    ///          「オブジェクト名で引き当てて値を戻す」手順をコマンドに包む役。
    class UndoRedoHistory {
    public:
        /// @brief 対象オブジェクトを引き当てるマネージャを登録する
        /// @note 積んだ操作はこのマネージャを使って Undo される
        void SetGameObjectManager(GameObjectManager* manager) { manager_ = manager; }

        /// @brief トランスフォーム操作を履歴に追加（before == after の場合は記録しない）
        void Push(const TransformRecord& record);

        /// @brief スポーン操作を履歴に追加
        void Push(const ObjectSpawnRecord& record);

        /// @brief 一つ前の状態に戻す
        /// @return 実行できた場合 true
        bool Undo(GameObjectManager* manager);

        /// @brief Undo を取り消す
        /// @return 実行できた場合 true
        bool Redo(GameObjectManager* manager);

        bool CanUndo() const;
        bool CanRedo() const;

        /// @brief 履歴をすべてクリア（シーン切り替え時など）
        void Clear();

        /// @brief Undo/Redo でオブジェクトが削除される直前に呼ばれるコールバックを設定する
        /// @note ObjectSelector のダングリングポインタ防止のために使用する
        /// @param cb 削除されるオブジェクト名を受け取るコールバック
        void SetOnBeforeDestroyCallback(std::function<void(const std::string& objectName)> cb)
        {
            onBeforeDestroy_ = std::move(cb);
        }

        int GetUndoCount() const;
        int GetRedoCount() const;

    private:
        /// @brief 名前で引き当てたオブジェクトへ値を書き戻す
        void ApplyTransform(const std::string& name,
                            const Vector3& translate, const Vector3& rotate,
                            const Vector3& scale, bool active) const;

        /// @brief スポーンを取り消す（オブジェクトを削除する）
        void DestroySpawned(const ObjectSpawnRecord& record) const;

        /// @brief スポーンをやり直す（同じオブジェクトを作り直す）
        void RespawnObject(const ObjectSpawnRecord& record) const;

        GameObjectManager* manager_ = nullptr;

        /// @brief オブジェクト削除前コールバック（ObjectSelector 選択解除用）
        std::function<void(const std::string&)> onBeforeDestroy_;
    };
}

#endif // USE_IMGUI
