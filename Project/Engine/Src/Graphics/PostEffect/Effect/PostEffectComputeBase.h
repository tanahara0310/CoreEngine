#pragma once
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
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
        void Initialize(GraphicsCore* dxCommon) override;

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

        static constexpr Cb::Field kScreenSizeConstantsFields[] = {
            CB_FIELD(ScreenSizeConstants, screenWidth), CB_FIELD(ScreenSizeConstants, screenHeight),
            CB_FIELD(ScreenSizeConstants, pad),
        };
        CB_VERIFY_LAYOUT(ScreenSizeConstants, kScreenSizeConstantsFields);

    protected:
        /// @brief CS シェーダーのファイルパスを返す（派生クラスで必ずオーバーライドする）
        virtual std::wstring GetComputeShaderPath() const = 0;

        /// @brief 定数バッファ生成フック（Initialize の最後に呼ばれる）
        /// @note 画面サイズ用バッファは基底が先に用意するので、派生は自分固有のものだけ作ればよい
        virtual void OnCreateConstantBuffers() {}

        /// @brief 画面サイズ定数を今フレームの UploadRing へ確保する
        /// @param width  出力幅
        /// @param height 出力高さ
        /// @note Dispatch のたびに新しい領域を取るので、1 フレーム中に解像度の違う
        ///       ディスパッチを複数回行っても互いを踏まない（1 本の共有バッファだった頃は
        ///       最後の解像度が全ディスパッチに適用されていた）。
        void UpdateScreenSizeConstants(uint32_t width, uint32_t height);

        /// @brief 直前の UpdateScreenSizeConstants が確保した GPU アドレス（ルートへ渡す用）
        D3D12_GPU_VIRTUAL_ADDRESS GetScreenSizeCbAddress() const { return screenSizeCbAddress_; }

        Microsoft::WRL::ComPtr<IDxcBlob> computeShaderBlob_;     ///< CS用シェーダーブロブ
        Microsoft::WRL::ComPtr<ID3D12PipelineState> computePso_; ///< CS用PSO

    private:
        /// 今フレームぶんの画面サイズ定数（UploadRing 上）。フレームを跨いで持ち越さない
        D3D12_GPU_VIRTUAL_ADDRESS screenSizeCbAddress_ = 0;
    };

} // namespace CoreEngine
