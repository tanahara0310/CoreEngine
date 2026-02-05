#pragma once

#include "Glyph.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <unordered_map>
#include <memory>

// 前方宣言
namespace CoreEngine {
    class DirectXCommon;
    class DescriptorManager;
}

/// @brief 単一フォントの管理クラス

namespace CoreEngine
{
class Font {
public:
    Font() = default;
    ~Font();



    /// @brief フォントの初期化
    /// @param ftLibrary FreeTypeライブラリハンドル
    /// @param fontFilePath フォントファイルパス
    /// @param fontSize フォントサイズ（ピクセル）
    /// @param dxCommon DirectXCommon
    /// @return 成功した場合true
    bool Initialize(FT_Library ftLibrary, const std::string& fontFilePath, uint32_t fontSize, CoreEngine::DirectXCommon* dxCommon);

    /// @brief 文字コードからグリフを取得（キャッシュあり）
    /// @param charCode 文字コード（UTF-32）
    /// @return グリフデータ（存在しない場合はnullptr）
    const Glyph* GetGlyph(uint32_t charCode);

    /// @brief フォントサイズを取得
    /// @return フォントサイズ
    uint32_t GetFontSize() const { return fontSize_; }

    /// @brief 行間を取得
    /// @return 行間（ピクセル）
    int32_t GetLineHeight() const { return lineHeight_; }

    /// @brief アセンダーを取得（ベースラインから最も高い文字の上端までの距離）
    /// @return アセンダー（ピクセル）
    int32_t GetAscender() const { return ascender_; }

private:
    /// @brief 文字をレンダリングしてテクスチャとして生成
    /// @param charCode 文字コード
    /// @return 生成されたグリフ（失敗時はnullptr）
    std::unique_ptr<Glyph> RenderGlyph(uint32_t charCode);

    /// @brief グリフビットマップからテクスチャリソースを作成
    /// @param bitmap FreeTypeビットマップ
    /// @param glyph 出力先のグリフ
    /// @return 成功した場合true
    bool CreateGlyphTexture(const FT_Bitmap& bitmap, Glyph* glyph);

private:
    FT_Face face_ = nullptr;
    CoreEngine::DirectXCommon* dxCommon_ = nullptr;
    CoreEngine::DescriptorManager* descriptorManager_ = nullptr;

    std::string fontFilePath_;
    uint32_t fontSize_ = 0;
    int32_t lineHeight_ = 0;
    int32_t ascender_ = 0;

    std::unordered_map<uint32_t, std::unique_ptr<Glyph>> glyphCache_;
};
}
