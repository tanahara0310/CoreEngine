#pragma once

#ifdef CORE_EDITOR

namespace CoreEngine
{
    class SceneDebugEditor;
}

namespace CoreEngine::Editor
{
    /// @brief シーンのファイルが外で変わったときの窓
    /// @details 保存していない変更があるときに外で変わったら、読み直すか続けるかを選ばせる。
    ///          保存が外の変更とぶつかったら、やめる・読み直す・上書きするを選ばせる。
    class SceneFileDialogs
    {
    public:
        /// @brief 窓を描く（毎フレーム呼ぶ）
        void Draw(SceneDebugEditor* editor);

    private:
        void DrawSaveConflicts(SceneDebugEditor& editor);
        void DrawExternalChanges(SceneDebugEditor& editor);
    };
}

#endif // CORE_EDITOR
