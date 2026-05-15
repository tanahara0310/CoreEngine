#pragma once

#include <d3d12.h>
#include <string>

namespace CoreEngine
{
    class CustomShaderPipeline;

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

        /// @brief カスタムルートパラメータをバインドする（エンジン既定バインド完了後に呼ばれる）
        /// @param cmdList 現在のコマンドリスト
        /// @param pipeline 構築済みカスタムパイプライン（GetRootParamIndex() でスロット番号を取得できる）
        /// @note 追加の CBV/SRV を独自 RootSignature のスロットへバインドしたい場合にオーバーライドする。
        virtual void BindCustomResources(
            [[maybe_unused]] ID3D12GraphicsCommandList* cmdList,
            [[maybe_unused]] const CustomShaderPipeline* pipeline) const {}
    };

} // namespace CoreEngine
