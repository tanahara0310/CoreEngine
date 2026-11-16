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
        ///          全エフェクトで共通の入れ物なので、基底が 1 つだけ持つ。
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

        /// @brief エフェクト固有のリソース（追加のパイプライン・テクスチャなど）を作るフック（Initialize の最後に呼ばれる）
        /// @note 毎フレーム変わる定数はここで作らず、記録時に UploadConstants で置く
        virtual void OnCreateResources() {}

        /// @brief 画面サイズ定数を今フレームの UploadRing へ確保する
        /// @param width  出力幅
        /// @param height 出力高さ
        /// @note Dispatch のたびに新しい領域を取るので、1 フレーム中に解像度の違う
        ///       ディスパッチを複数回行っても互いを踏まない
        void UpdateScreenSizeConstants(uint32_t width, uint32_t height);

        /// @brief 直前の UpdateScreenSizeConstants が確保した GPU アドレス（ルートへ渡す用）
        D3D12_GPU_VIRTUAL_ADDRESS GetScreenSizeCbAddress() const { return screenSizeCbAddress_; }

        /// @brief 入力 1 枚・出力 1 枚・固有の定数・画面サイズを差し、画面全体を 8×8 のグループで Dispatch する
        /// @details 差す先は "gTexture"・"gOutput"・paramsName・"ScreenParams"。シェーダーに無い名前は差さない
        /// @param inputSrvHandle  入力テクスチャの SRV
        /// @param outputUavHandle 出力テクスチャの UAV
        /// @param width           出力幅
        /// @param height          出力高さ
        /// @param paramsName      固有の定数バッファのシェーダー側の名前
        /// @param params          固有の定数（今フレームの UploadRing へ置いてから差す）
        template <class TParams>
        void DispatchScreenPass(
            D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
            uint32_t width,
            uint32_t height,
            const char* paramsName,
            const TParams& params)
        {
            DispatchScreenPassWithAddress(
                inputSrvHandle, outputUavHandle, width, height, paramsName, UploadConstants(params));
        }

        /// @brief 固有の定数を持たないエフェクト用。"gTexture"・"gOutput"・"ScreenParams" だけを差して Dispatch する
        void DispatchScreenPass(
            D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
            uint32_t width,
            uint32_t height);

        /// @brief DispatchScreenPass の本体。固有の定数は置き終えた GPU アドレスで受け取る
        /// @param paramsName    固有の定数バッファのシェーダー側の名前（nullptr なら差さない）
        /// @param paramsAddress paramsName へ差す GPU アドレス
        void DispatchScreenPassWithAddress(
            D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
            uint32_t width,
            uint32_t height,
            const char* paramsName,
            D3D12_GPU_VIRTUAL_ADDRESS paramsAddress);

        Microsoft::WRL::ComPtr<ID3D12PipelineState> computePso_; ///< CS用PSO

    private:
        /// 今フレームぶんの画面サイズ定数（UploadRing 上）。フレームを跨いで持ち越さない
        D3D12_GPU_VIRTUAL_ADDRESS screenSizeCbAddress_ = 0;
    };

} // namespace CoreEngine
