#pragma once

#include <string>
#include <vector>

namespace CoreEngine
{
    /// @brief プロジェクトのポストエフェクト 1 つ（`postEffects` の 1 要素）
    struct ProjectPostEffectSettings
    {
        std::string name;     ///< 名前（スクリプトの PostEffect:: で指す）
        std::string material; ///< コンピュートシェーダと値を持つマテリアル（.mat）
        std::string stage;    ///< 差し込む段（"SceneHDR" ／ "PostTonemap" ／ "Overlay"）
        bool enabled = true;  ///< シーンを開いたときに有効にするか
    };

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

        /// @brief プロジェクトの名前（`name` が無ければプロジェクトのフォルダ名）
        std::string GetProjectName() const;

        /// @brief 起動時に開くシーンの名前（決まっていなければ空）
        const std::string& GetInitialSceneName() const { return initialSceneName_; }

        /// @brief 起動時に開くシーンを決めて保存する
        /// @return 保存できたら true
        bool SetInitialSceneName(std::string sceneName);

        /// @brief 起動時のローディング画面に敷く画像（`Application/…` か `Engine/…` の綴り。空ならエンジンの既定）
        const std::string& GetSplashImage() const { return splashImage_; }

        /// @brief ローディング画面の画像を決めて保存する（空でエンジンの既定へ戻す）
        /// @return 保存できたら true
        bool SetSplashImage(std::string path);

        /// @brief ゲーム用のローディング画面に大きく出す題名（空ならプロジェクト名を出す）
        const std::string& GetSplashTitle() const { return splashTitle_; }

        /// @brief ゲーム用のローディング画面の題名を決めて保存する
        bool SetSplashTitle(std::string title);

        /// @brief ゲーム用のローディング画面で題名の下に小さく出す 1 行（空なら出さない）
        const std::string& GetSplashSubtitle() const { return splashSubtitle_; }

        /// @brief ゲーム用のローディング画面の小見出しを決めて保存する
        bool SetSplashSubtitle(std::string subtitle);

        /// @brief ゲーム用のローディング画面に出すヒント（起動のたびに 1 つ選ぶ。空なら出さない）
        const std::vector<std::string>& GetSplashTips() const { return splashTips_; }

        /// @brief ゲーム用のローディング画面のヒントを決めて保存する
        bool SetSplashTips(std::vector<std::string> tips);

        /// @brief シーンを切り替えるときのロード画面を描くコンピュートシェーダ（`Application/…` の綴り。空ならエンジンの既定）
        const std::string& GetLoadingScreenShader() const { return loadingScreenShader_; }
        /// @brief ロード画面のシェーダを決めて保存する（空でエンジンの既定へ戻す）
        bool SetLoadingScreenShader(std::string path);
        /// @brief ロード画面のシェーダへ渡す画像（並び順に gImage0〜gImage7 へ入る）
        const std::vector<std::string>& GetLoadingScreenImages() const { return loadingScreenImages_; }
        /// @brief ロード画面の画像を決めて保存する
        bool SetLoadingScreenImages(std::vector<std::string> images);

        /// @brief プロジェクトのポストエフェクトの並び（掛ける順）
        const std::vector<ProjectPostEffectSettings>& GetPostEffects() const { return postEffects_; }

        /// @brief ファイルへ書き出す（ファイルにある他の項目は残す）
        bool Save() const;

        /// @brief ファイルから読み直す
        void Reload();

    private:
        ProjectSettings() { Reload(); }
        ~ProjectSettings() = default;
        ProjectSettings(const ProjectSettings&) = delete;
        ProjectSettings& operator=(const ProjectSettings&) = delete;

        std::string name_;
        std::string initialSceneName_;
        std::string splashImage_;
        std::string splashTitle_;
        std::string splashSubtitle_;
        std::vector<std::string> splashTips_;
        std::string loadingScreenShader_;
        std::vector<std::string> loadingScreenImages_;
        std::vector<ProjectPostEffectSettings> postEffects_;
    };
}
