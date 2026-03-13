#pragma once

#include <externals/DirectXTex/DirectXTex.h>
#include <string>

namespace CoreEngine
{
    class TextureImageProcessor
    {
    public:
        enum class FileType {
            DDS,
            HDR,
            WIC,
        };

        /// @brief ファイルパスからテクスチャのファイル形式を判別する
        /// @param filePath ファイルパス
        /// @return ファイル形式
        static FileType DetectFileType(const std::wstring& filePath);

        /// @brief ファイルパスからテクスチャを読み込む。DDS、HDR、WIC形式に対応。
        /// @param filePath ファイルパス
        /// @param image 読み込んだテクスチャデータを格納するScratchImage参照
        /// @return 成功したらS_OK、失敗したらエラーコード
        static HRESULT LoadTextureImage(const std::wstring& filePath, DirectX::ScratchImage& image);

        /// @brief ファイルパスからテクスチャのメタデータを読み込む。DDS、HDR、WIC形式に対応。
        /// @param filePath ファイルパス
        /// @param metadata 読み込んだテクスチャのメタデータを格納するTexMetadata参照
        /// @return 成功したらS_OK、失敗したらエラーコード
        static HRESULT LoadMetadata(const std::wstring& filePath, DirectX::TexMetadata& metadata);

        /// @brief テクスチャからミップマップチェーンを生成する。圧縮テクスチャや1x1テクスチャの場合は元の画像をそのまま返す。
        /// @param image ミップマップチェーンを生成したいテクスチャデータを格納するScratchImage参照。成功した場合は元の画像が解放される。
        /// @param mipImages 生成されたミップマップチェーンを格納するScratchImage参照。成功した場合はミップマップチェーンが格納される。
        /// @return 成功したらS_OK、失敗したらエラーコード
        static HRESULT BuildMipChain(DirectX::ScratchImage& image, DirectX::ScratchImage& mipImages);
    };
}
