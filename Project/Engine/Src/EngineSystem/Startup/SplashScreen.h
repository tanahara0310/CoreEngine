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
        /// @param hInstance   インスタンスハンドル
        /// @param projectName 右上に出すプロジェクト名（UTF-8）
        void Show(HINSTANCE hInstance, const std::string& projectName);

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

        /// @brief 背景（画像・文字の下を暗くするグラデーション・マーク）を窓の大きさで 1 枚に組み立てる
        void BuildBackdrop();

        /// @brief メモリ DC に組み立ててから一度に転送する（ちらつき防止）
        void Render(HDC targetDC);
        void Repaint();
        void DestroyResources();

        HWND hwnd_ = nullptr;
        HINSTANCE hInstance_ = nullptr;

        HBITMAP backdrop_ = nullptr;

        HFONT nameFont_ = nullptr;
        HFONT versionFont_ = nullptr;
        HFONT projectFont_ = nullptr;
        HFONT labelFont_ = nullptr;
        HFONT detailFont_ = nullptr;

        std::wstring projectName_;
        std::wstring versionText_;
        std::wstring label_;
        std::wstring detail_;
        float progress_ = 0.0f;

        int32_t width_ = 0;
        int32_t height_ = 0;
        uint32_t dpi_ = 96;

        ULONGLONG lastPaintTick_ = 0;
    };
}
