#pragma once
#include <Windows.h>
#include <filesystem>
#include <string>

namespace CoreEngine
{
    /// @brief 起動中に出すローディング画面（GDI 描画）
    /// @details D3D12 デバイス作成より前から表示したいので、あえて GDI で描く。
    ///          メインウィンドウは起動シーケンス完了まで非表示にし、このウィンドウだけがメッセージを処理する。
    ///          背景にはプロジェクト設定の `splashImage` の画像を敷き、無ければエンジンの既定の画像を敷く。
    class SplashScreen {
    public:
        /// @brief 画面の作り
        enum class Style {
            Editor, ///< 画像の上にエンジンの名前・版・今のステップ・割合を出す（エディタ用）
            Game,   ///< 暗くした画像の上にゲームの題名・ヒント・読み込み中だけを出す（書き出したゲーム用）
        };

        /// @brief エンジンの既定の画像
        static constexpr const char* kDefaultImage = "Engine/Assets/Textures/Splash/DefaultSplash.jpg";

        /// @brief 背景に敷く画像のパス
        /// @return プロジェクトの指定 → エンジンの既定の順で、ファイルがある方。どちらも無ければ空
        static std::filesystem::path ResolveImagePath();

        SplashScreen() = default;
        ~SplashScreen();

        SplashScreen(const SplashScreen&) = delete;
        SplashScreen& operator=(const SplashScreen&) = delete;

        /// @brief スプラッシュを表示する
        /// @param hInstance インスタンスハンドル
        /// @param style     画面の作り
        /// @note プロジェクト名・題名・ヒントはプロジェクト設定から読む
        void Show(HINSTANCE hInstance, Style style);

        /// @brief 進捗と現在のステップ名を設定する
        /// @param progress 0.0〜1.0（前の値より小さければ前の値のまま）
        /// @param label    ステップ名（UTF-8）
        void SetStatus(float progress, const std::string& label);

        /// @brief 進捗だけを設定する（0.0〜1.0。前の値より小さければ前の値のまま）
        void SetProgress(float progress);

        /// @brief ステップ内の細かい進行内容を設定する（UTF-8。空文字で消える）
        void SetDetail(const std::string& detail);

        /// @brief 溜まったウィンドウメッセージを処理し、必要なら再描画する
        /// @param forceRedraw true なら間引きを無視して即再描画する
        /// @note hwnd 指定の PeekMessage でも、スレッド宛の「送信済みメッセージ」は
        ///       処理される。これが Windows のハング検出への応答になるので、
        ///       重いステップの内側からも定期的に呼ぶこと。
        void Pump(bool forceRedraw = false);

        /// @brief スプラッシュを閉じる
        void Close();

        bool IsVisible() const { return hwnd_ != nullptr; }

    private:
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

        /// @brief 背景（画像と、画面の作りに合わせた暗くする処理・マーク）を窓の大きさで 1 枚に組み立てる
        void BuildBackdrop();

        /// @brief メモリ DC に組み立ててから一度に転送する（ちらつき防止）
        void Render(HDC targetDC);

        /// @brief エディタ用の文字（名前・版・プロジェクト名・ステップ・細目）を描く
        void DrawEditorTexts(HDC dc) const;

        /// @brief ゲーム用の文字（題名・小見出し・ヒント・読み込み中）を描く
        void DrawGameTexts(HDC dc) const;

        /// @brief 下端の進捗の線を描く
        void DrawProgressBar(HDC dc) const;

        void Repaint();
        void DestroyResources();

        HWND hwnd_ = nullptr;
        HINSTANCE hInstance_ = nullptr;
        Style style_ = Style::Editor;

        HBITMAP backdrop_ = nullptr;

        HFONT headlineFont_ = nullptr;
        HFONT smallFont_ = nullptr;
        HFONT smallBoldFont_ = nullptr;
        HFONT statusFont_ = nullptr;
        HFONT detailFont_ = nullptr;

        std::wstring projectName_;
        std::wstring versionText_;
        std::wstring titleText_;
        std::wstring subtitleText_;
        std::wstring tipText_;
        std::wstring label_;
        std::wstring detail_;
        float progress_ = 0.0f;

        int32_t width_ = 0;
        int32_t height_ = 0;
        uint32_t dpi_ = 96;

        ULONGLONG lastPaintTick_ = 0;
    };
}
