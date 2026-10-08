#pragma once
#include "Graphics/Render/BaseRenderer.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Math/MathCore.h"
#include <d3d12.h>
#include <wrl.h>
#include <memory>
#include <vector>


namespace CoreEngine
{

    /// @brief スプライト描画用レンダラー
    class SpriteRenderer : public BaseRenderer {
    public:
        /// @brief トランスフォーム行列
        struct TransformationMatrix {
            Matrix4x4 WVP;
            Matrix4x4 world;
        };

        static constexpr Cb::Field kTransformationMatrixFields[] = {
            CB_FIELD(TransformationMatrix, WVP), CB_FIELD(TransformationMatrix, world),
        };
        CB_VERIFY_LAYOUT(TransformationMatrix, kTransformationMatrixFields);

        /// @brief 最大スプライト数
        static constexpr size_t kMaxSpriteCount = 1024;

        /// @brief 定数バッファ内での変換行列 1 個ぶんの間隔（CBV の配置に合わせて 256 バイト単位）
        static constexpr size_t kTransformStride =
            (sizeof(TransformationMatrix) + D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1)
            & ~static_cast<size_t>(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1);

        /// @brief per-frame リソースのリング段数
        /// @details FrameSync のスロット数上限に合わせる。添字は実行時の
        ///          GraphicsCore::Frame().FrameIndex() を使うこと（ここで 2 を直書きすると
        ///          設定の frameCount と食い違う）。
        static constexpr UINT kFrameCount = kMaxFramesInFlight;

        // IRendererインターフェースの実装
        void BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) override;
        void EndPass() override;
        RenderPassType GetRenderPassType() const override { return RenderPassType::Sprite; }
        void SetCamera(const Camera* camera) override;

        /// @brief 初期化（GraphicsCore 付き）
        /// @param dxCommon GraphicsCore
        void Initialize(GraphicsCore* dxCommon);

        /// @brief ルートシグネチャを取得
        ID3D12RootSignature* GetRootSignature() const { return rootSignatureMg_->GetRootSignature(); }

        /// @brief 利用可能な定数バッファのインデックスを取得
        /// @return バッファインデックス
        size_t GetAvailableConstantBuffer();

        /// @brief WVP行列を計算
        /// @param position 位置
        /// @param scale スケール
        /// @param rotation 回転
        /// @return WVP行列
        Matrix4x4 CalculateWVPMatrix(const Vector3& position, const Vector3& scale, const Vector3& rotation) const;

        /// @brief WVP 行列を計算（カメラ使用版）
        Matrix4x4 CalculateWVPMatrix(const Vector3& position, const Vector3& scale, const Vector3& rotation, const Camera* camera) const;

        /// @brief GraphicsCoreを取得
        GraphicsCore* GetGraphicsCore() { return dxCommon_; }


        /// @brief 今のフレームの index 番目の変換行列の書き込み先を取得
        /// @param index GetAvailableConstantBuffer が返したインデックス
        TransformationMatrix* GetTransformData(size_t index) {
            return reinterpret_cast<TransformationMatrix*>(transformMapped_[currentFrameIndex_] + index * kTransformStride);
        }

        /// @brief 今のフレームの index 番目の変換行列の GPU アドレスを取得
        /// @param index GetAvailableConstantBuffer が返したインデックス
        D3D12_GPU_VIRTUAL_ADDRESS GetTransformGpuAddress(size_t index) const {
            return transformBuffers_[currentFrameIndex_]->GetGPUVirtualAddress() + index * kTransformStride;
        }

        /// @brief シェーダーリソース名からルートパラメータインデックスを取得
        int GetRootParamIndex(const std::string& resourceName) const;

    private:
        // BaseRenderer から継承したサブシステムを使用（rootSignatureMg_, psoMg_, shaderCompiler_, reflectionBuilder_ は削除）

        GraphicsCore* dxCommon_ = nullptr;

        // 変換行列の定数バッファ（フレームごとに 1 本。kMaxSpriteCount 個を kTransformStride 間隔で並べる）
        Microsoft::WRL::ComPtr<ID3D12Resource> transformBuffers_[kFrameCount];
        uint8_t* transformMapped_[kFrameCount] = {};

        // 現在のバッファインデックスとフレームインデックス
        size_t currentBufferIndex_ = 0;
        UINT currentFrameIndex_ = 0;

        // シェーダーリフレクションデータ
        std::unique_ptr<ShaderReflectionData> reflectionData_;

        /// @brief パイプラインのみを初期化（Initialize(GraphicsCore*) から呼び出す）
        void InitializePipeline(ID3D12Device* device);

        /// @brief IRenderer::Initialize(ID3D12Device*) のオーバーライド（直接呼び出し禁止）
        void Initialize(ID3D12Device* device) override;
    };
}
