#pragma once

#include "Graphics/Texture/TextureManager.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace CoreEngine
{
    /// @brief マテリアルの値 1 つ（1〜4 個の数。配列は要素ごとに `名前[番号]` で持つ）
    using MaterialNumbers = std::vector<float>;

    /// @brief マテリアルが指すテクスチャ 1 枚
    struct MaterialTexture
    {
        std::string path;    ///< `Application/Assets/…` のパス
        bool linear = false; ///< 色でなくデータとして読む（sRGB からリニアへ直さない）

        bool operator==(const MaterialTexture&) const = default;
    };

    /// @brief マテリアル（.mat）の中身。スプライト描画と UI 画像が共有する
    /// @details 持つのはシェーダのパスと値とテクスチャだけ。値の並べ方はシェーダの定数バッファが決める
    ///          （`ShaderMaterialPipeline` がリフレクションで読む）。
    ///          値を変えると同じマテリアルを指す全員の見た目が変わる。
    class ShaderMaterial
    {
    public:
        /// @brief .mat を読む
        /// @param assetPath `Application/Assets/…` のパス
        /// @return 読めなければ false（中身は空のまま）
        bool Load(const std::string& assetPath);

        /// @brief 今の中身を .mat へ書く
        bool Save();

        /// @brief .mat からシェーダのパスだけを読む（テクスチャは読み込まない）
        /// @return 読めなければ false
        static bool ReadShaderPaths(const std::filesystem::path& fullPath, std::string& shaderPath,
            std::string& vertexShaderPath);

        /// @brief ファイルの中身へ戻す（再生中に変えた値を捨てる）
        bool Revert();

        /// @brief 読んだ .mat のパス（`Application/Assets/…`）
        const std::string& GetAssetPath() const { return assetPath_; }

        /// @brief .mat のフルパス
        std::filesystem::path GetFullPath() const;

        // ===== シェーダ =====

        /// @brief ピクセルシェーダのパス（`Application/Assets/…` か `Engine/Assets/…`）
        const std::string& GetShaderPath() const { return shaderPath_; }
        void SetShaderPath(std::string path);

        /// @brief 頂点シェーダのパス（空なら標準の頂点シェーダ）
        const std::string& GetVertexShaderPath() const { return vertexShaderPath_; }
        void SetVertexShaderPath(std::string path);

        // ===== 値 =====

        /// @brief 値を決める
        /// @param name 定数バッファの項目名。配列の要素は `名前[番号]`
        void SetNumbers(std::string_view name, std::span<const float> numbers);

        /// @brief 値を引く（無ければ nullptr）
        const MaterialNumbers* FindNumbers(std::string_view name) const;

        /// @brief 値を消す（シェーダの既定の 0 に戻る）
        void RemoveNumbers(std::string_view name);

        /// @brief 値の一覧（名前の順）
        const std::map<std::string, MaterialNumbers, std::less<>>& GetValues() const { return values_; }

        // ===== テクスチャ =====

        /// @brief テクスチャを決める（空のパスなら外す）
        /// @param name シェーダのテクスチャの名前（`gTexture` 以外）
        void SetTexture(std::string_view name, const MaterialTexture& texture);

        /// @brief テクスチャを引く（無ければ nullptr）
        const MaterialTexture* FindTexture(std::string_view name) const;

        /// @brief テクスチャの一覧（名前の順）
        const std::map<std::string, MaterialTexture, std::less<>>& GetTextures() const { return textures_; }

        /// @brief 読み込んだテクスチャを引く（指していなければ nullptr）
        const TextureManager::LoadedTexture* FindLoadedTexture(std::string_view name) const;

        /// @brief 値かテクスチャかシェーダのパスが変わるたびに増える番号
        uint64_t GetRevision() const { return revision_; }

        /// @brief 読んだときの .mat の更新時刻
        std::filesystem::file_time_type GetLoadedWriteTime() const { return loadedWriteTime_; }

    private:
        /// @brief 中身を空にする
        void Clear();

        std::string assetPath_;
        std::string shaderPath_;
        std::string vertexShaderPath_;
        std::map<std::string, MaterialNumbers, std::less<>> values_;
        std::map<std::string, MaterialTexture, std::less<>> textures_;
        std::map<std::string, TextureManager::LoadedTexture, std::less<>> loadedTextures_;
        uint64_t revision_ = 1;
        std::filesystem::file_time_type loadedWriteTime_{};
    };

    /// @brief `名前[番号]` を名前と番号に分ける
    /// @return 配列の要素の形でなければ false（name に全体を入れ、index は 0）
    bool SplitMaterialElementName(std::string_view key, std::string_view& name, uint32_t& index);

    /// @brief マテリアルのテクスチャを読み込む（テクスチャの管理がまだ無ければ空を返す）
    TextureManager::LoadedTexture LoadMaterialTexture(const MaterialTexture& texture);
}
