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

        /// @brief 画面サイズ定数バッファのレイアウト
        /// @details HLSL 側の `cbuffer ScreenParams : register(b1)` と一致させること。
        ///          以前は同じ定義が 15 エフェクトにコピーされており、バッファの生成・
        ///          更新コードもそれぞれ持っていた。共通の入れ物なので基底が 1 つだけ持つ。
        struct ScreenSizeConstants {
            uint32_t screenWidth  = 1280;
            uint32_t screenHeight = 720;
            float    pad[2]       = { 0.0f, 0.0f };
        };

    protected:
        /// @brief CS シェーダーのファイルパスを返す（派生クラスで必ずオーバーライドする）
        virtual std::wstring GetComputeShaderPath() const = 0;

        /// @brief 定数バッファ生成フック（Initialize の最後に呼ばれる）
        /// @note 画面サイズ用バッファは基底が先に用意するので、派生は自分固有のものだけ作ればよい
        virtual void OnCreateConstantBuffers() {}

        /// @brief 画面サイズ定数バッファへ今回のディスパッチ解像度を書き込む
        /// @param width  出力幅
        /// @param height 出力高さ
        void UpdateScreenSizeConstants(uint32_t width, uint32_t height);

        /// @brief 画面サイズ定数バッファの GPU アドレス（ルートへ渡す用）
        D3D12_GPU_VIRTUAL_ADDRESS GetScreenSizeCbAddress() const;

        Microsoft::WRL::ComPtr<IDxcBlob> computeShaderBlob_;     ///< CS用シェーダーブロブ
        Microsoft::WRL::ComPtr<ID3D12PipelineState> computePso_; ///< CS用PSO

    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> screenSizeCB_;
        ScreenSizeConstants* mappedScreenSize_ = nullptr;
    };

} // namespace CoreEngine
