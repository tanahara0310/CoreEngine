#pragma once
#include <wrl.h>
#include <dxcapi.h>
#include <d3d12.h>
#include <string>

#include "PostEffectBase.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"


namespace CoreEngine {

    /// @brief コンピュートパイプライン（CS）用ポストエフェクト基底クラス
    class PostEffectComputeBase : public PostEffectBase {
    public:
        /// @brief CS パイプラインによる初期化
        void Initialize(DirectXCommon* dxCommon) override;

        PostEffectExecutionType GetExecutionType() const override {
            return PostEffectExecutionType::Compute;
        }

    protected:
        /// @brief CS シェーダーのファイルパスを返す（派生クラスで必ずオーバーライドする）
        virtual std::wstring GetComputeShaderPath() const = 0;

        /// @brief 定数バッファ生成フック（Initialize の最後に呼ばれる）
        virtual void OnCreateConstantBuffers() {}

        Microsoft::WRL::ComPtr<IDxcBlob> computeShaderBlob_;     ///< CS用シェーダーブロブ
        Microsoft::WRL::ComPtr<ID3D12PipelineState> computePso_; ///< CS用PSO
    };

} // namespace CoreEngine
