#pragma once

#include <string>

namespace CoreEngine
{
    /// @brief CVar で表せないプロジェクト設定
    /// @details 置き場は `Application/Config/EngineSettings/Project.json`。
    ///          CVar は bool / 数値 / ベクトルしか持てないので、シーン名のような
    ///          文字列はここが受け持つ。
    /// @note 初回の参照で読み込む。起動タスクを組み立てる時点（ログの初期化より前）から
    ///       読めるように、読み込みはログを使わない。
    class ProjectSettings
    {
    public:
        static ProjectSettings& Get();

        /// @brief 起動時に開くシーンの名前（決まっていなければ空）
        const std::string& GetInitialSceneName() const { return initialSceneName_; }

        /// @brief 起動時に開くシーンを決めて保存する
        /// @return 保存できたら true
        bool SetInitialSceneName(std::string sceneName);

        /// @brief ファイルへ書き出す
        bool Save() const;

        /// @brief ファイルから読み直す
        void Reload();

    private:
        ProjectSettings() { Reload(); }
        ~ProjectSettings() = default;
        ProjectSettings(const ProjectSettings&) = delete;
        ProjectSettings& operator=(const ProjectSettings&) = delete;

        std::string initialSceneName_;
    };
}
