#pragma once

#include <d3d12.h>
#include <memory>
#include <string>
#include <unordered_map>

namespace CoreEngine
{
    class CustomShaderPipeline;
    class ICustomShaderProvider;
    class ShaderCompiler;
    class ShaderReflectionBuilder;

    /// @brief CustomShaderPipeline を「シェーダーパス＋PSO設定」のキーで共有するキャッシュ
    /// @details 同じカスタムシェーダーを複数オブジェクトが使う場合に、
    ///          コンパイル・リフレクション・RootSignature・PSO 構築を1回に抑える。
    ///          返り値は shared_ptr のため、Clear() してもコンポーネント側が
    ///          参照している使用中のパイプラインは生存する。
    class CustomShaderPipelineCache {
    public:
        /// @brief キャッシュにあれば共有パイプラインを返し、無ければ構築して登録する
        /// @return 共有パイプライン（失敗時は nullptr。失敗はキャッシュせず次回再試行する）
        std::shared_ptr<CustomShaderPipeline> GetOrBuild(
            ID3D12Device* device,
            ShaderCompiler& compiler,
            ShaderReflectionBuilder& reflectionBuilder,
            const ICustomShaderProvider& provider);

        /// @brief キャッシュ参照を全て破棄する（使用中のものは利用側の参照で生存）
        void Clear();

    private:
        /// @brief プロバイダの PSO 構築に影響する設定からキャッシュキーを作る
        static std::string MakeKey(const ICustomShaderProvider& provider);

        std::unordered_map<std::string, std::shared_ptr<CustomShaderPipeline>> cache_;
    };

} // namespace CoreEngine
