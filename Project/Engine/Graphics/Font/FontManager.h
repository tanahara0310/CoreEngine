#pragma once

#include <ft2build.h>
#include FT_FREETYPE_H
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>

// 前方宣言
namespace CoreEngine {
    class DirectXCommon;
    class Font;
}

/// @brief フォント管理クラス（シングルトン）

namespace CoreEngine
{
class FontManager {
public:
    /// @brief シングルトンインスタンスを取得
    static FontManager& GetInstance();

    FontManager(const FontManager&) = delete;
    FontManager& operator=(const FontManager&) = delete;
    FontManager(FontManager&&) = delete;
    FontManager& operator=(FontManager&&) = delete;

    /// @brief 初期化
    /// @param dxCommon DirectXCommon
    /// @return 成功した場合true
    bool Initialize(CoreEngine::DirectXCommon* dxCommon);

    /// @brief 終了処理
    void Finalize();

    /// @brief フォントを読み込む
    /// @param fontFilePath フォントファイルパス
    /// @param fontSize フォントサイズ（ピクセル）
    /// @return フォントへのポインタ（失敗時はnullptr）
    CoreEngine::Font* LoadFont(const std::string& fontFilePath, uint32_t fontSize);

    /// @brief デフォルトフォントを設定
    /// @param fontFilePath フォントファイルパス
    /// @param fontSize フォントサイズ
    void SetDefaultFont(const std::string& fontFilePath, uint32_t fontSize);

    /// @brief デフォルトフォントを取得
    /// @return デフォルトフォント
    CoreEngine::Font* GetDefaultFont();


private:
    FontManager() = default;
    ~FontManager() = default;

    /// @brief フォントキャッシュのキーを生成
    /// @param fontFilePath フォントファイルパス
    /// @param fontSize フォントサイズ
    /// @return キー文字列
    std::string GenerateFontKey(const std::string& fontFilePath, uint32_t fontSize) const;

private:
    FT_Library ftLibrary_ = nullptr;
    CoreEngine::DirectXCommon* dxCommon_ = nullptr;
    bool isInitialized_ = false;

    std::unordered_map<std::string, std::unique_ptr<CoreEngine::Font>> fontCache_;
    std::mutex cacheMutex_;

    std::string defaultFontKey_;
};
}
