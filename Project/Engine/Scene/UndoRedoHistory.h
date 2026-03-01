#pragma once

#ifdef _DEBUG

#include "Engine/Math/Vector/Vector3.h"
#include <vector>
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

    /// @brief Undo/Redo 履歴管理クラス（デバッグビルド専用）
    class UndoRedoHistory {
    public:
        /// @brief 最大保持ステップ数
        static constexpr int kMaxSteps = 50;

        /// @brief 操作を履歴に追加（before == after の場合は記録しない / Redo スタックをクリア）
        void Push(const TransformRecord& record);

        /// @brief 一つ前の状態に戻す
        /// @return 実行できた場合 true
        bool Undo(GameObjectManager* manager);

        /// @brief Undo を取り消す
        /// @return 実行できた場合 true
        bool Redo(GameObjectManager* manager);

        bool CanUndo() const { return !undoStack_.empty(); }
        bool CanRedo() const { return !redoStack_.empty(); }

        /// @brief 履歴をすべてクリア（シーン切り替え時など）
        void Clear();

        int GetUndoCount() const { return static_cast<int>(undoStack_.size()); }
        int GetRedoCount() const { return static_cast<int>(redoStack_.size()); }

    private:
        void ApplyState(GameObjectManager* manager, const std::string& name,
                        const Vector3& translate, const Vector3& rotate,
                        const Vector3& scale, bool active);

        std::vector<TransformRecord> undoStack_;
        std::vector<TransformRecord> redoStack_;
    };
}

#endif // _DEBUG
