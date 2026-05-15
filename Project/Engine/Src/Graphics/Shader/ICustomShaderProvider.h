#pragma once

#include <string>

namespace CoreEngine
{
    /// @brief アプリ側からカスタムシェーダーのパスを提供するインターフェース
    class ICustomShaderProvider {
    public:
        virtual ~ICustomShaderProvider() = default;

        /// @brief カスタム頂点シェーダーのファイル名を返す（空文字列 = 既定シェーダーを使用）
        /// @note ファイル名のみ指定。ディレクトリは AssetDatabase が自動解決する。
        virtual std::wstring GetVertexShaderPath() const { return {}; }

        /// @brief カスタムピクセルシェーダーのファイル名を返す（空文字列 = 既定シェーダーを使用）
        /// @note ファイル名のみ指定。ディレクトリは AssetDatabase が自動解決する。
        virtual std::wstring GetPixelShaderPath() const { return {}; }

        /// @brief コンピュートシェーダーのファイル名を返す（空文字列 = 使用しない）
        /// @note CS は描画パイプラインとは独立。OnUpdate() 内で Dispatch すること。
        /// @note ファイル名のみ指定。ディレクトリは AssetDatabase が自動解決する。
        virtual std::wstring GetComputeShaderPath() const { return {}; }
    };

} // namespace CoreEngine
