#pragma once

#ifdef CORE_EDITOR

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace CoreEngine
{
    class EditorSettingsSubsystem;
    class SceneDebugEditor;
    class SceneManager;
}

namespace CoreEngine::Editor
{
    /// @brief 「すべてのシーンと設定を保存し直す」の窓
    /// @details 保存データのあるシーンを順に開いて保存し、設定のファイルを今の書き方で書き直す。
    class SceneResaveDialog
    {
    public:
        /// @brief 窓を開く（始める前に確かめる）
        void Open();

        /// @brief 確かめずに始め、終わったらアプリを閉じる
        void StartAndQuit();

        /// @brief 窓を描き、保存し直しを進める（毎フレーム呼ぶ）
        void Draw(SceneManager* scenes, SceneDebugEditor* editor, EditorSettingsSubsystem* settings);

        /// @brief 保存し直している途中か
        bool IsRunning() const { return state_ == State::Running; }

    private:
        /// @brief 窓の状態
        enum class State { Closed, Ready, Running, Done };

        /// @brief シーンの一覧を作り、今のシーンを戻り先に控えて始める
        void Start(SceneManager& scenes);

        /// @brief シーンを開き、開き終わるのを待つ
        /// @param save 開き終わったら保存するか
        void OpenScene(SceneManager& scenes, const std::string& name, bool save);

        /// @brief 1 フレームぶん進める
        void Step(SceneManager& scenes, SceneDebugEditor* editor, EditorSettingsSubsystem* settings);

        void DrawReady(SceneManager* scenes, SceneDebugEditor* editor);
        void DrawRunning();
        void DrawDone();

        /// @brief 右寄せでボタンを並べる位置へ寄せる
        static void AlignButtons(float width);

        State state_ = State::Closed;
        bool requestOpen_ = false;
        bool startWhenReady_ = false;      ///< 確かめずに始めるのを待っている
        bool quitWhenDone_ = false;

        std::vector<std::string> scenes_;  ///< 保存し直すシーン
        std::size_t next_ = 0;             ///< 次に開くシーンの番号
        std::string returnTo_;             ///< 終わったら戻るシーン
        std::string waitingFor_;           ///< 開き終わるのを待っているシーン
        bool saveWhenOpened_ = false;      ///< 開き終わったら保存するか
        int settledFrames_ = 0;            ///< 開き終わってから数えたフレーム
        std::chrono::steady_clock::time_point waitStarted_{};

        std::size_t savedCount_ = 0;       ///< 保存し直したシーンの数
        std::size_t settingsCount_ = 0;    ///< 書き直した設定のファイルの数
        std::vector<std::string> failed_;  ///< 開けなかったシーン
    };
}

#endif // CORE_EDITOR
