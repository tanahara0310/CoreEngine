#pragma once

#include "Graphics/RHI/Descriptor/UniqueDescriptor.h"
#include "Graphics/RHI/Resource/GpuResource.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/ShaderBindingContract.h"
#include "Math/Matrix/Matrix4x4.h"

#include <d3d12.h>
#include <wrl.h>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <unordered_map>

namespace CoreEngine
{
    class GraphicsCore;
    class Model;
    class RootSignatureManager;
    class ShaderCompiler;
    class ShaderReflectionBuilder;
    class ShaderReflectionData;
    struct VertexAnimationParams;

    /// @brief VertexAnimationDeform.CS.hlsl の契約
    namespace VertexAnimationDeformBind
    {
        enum Slot : size_t {
            gMaterial,
            gVertexAnim,
            gDeform,
            gSourceVertices,
            gIndices,
            gOutputVertices,
            Count
        };

        inline constexpr ShaderBindingDecl kDecls[] = {
            { "gMaterial",        ShaderBindingType::CBV, BindingUsage::Required },  // b0（サブメッシュのマテリアル）
            { "gVertexAnim",      ShaderBindingType::CBV, BindingUsage::Required },  // b9（ルート定数）
            { "gDeform",          ShaderBindingType::CBV, BindingUsage::Required },  // b1（ルート定数）
            { "gSourceVertices",  ShaderBindingType::SRV, BindingUsage::Required },  // t0
            { "gIndices",         ShaderBindingType::SRV, BindingUsage::Required },  // t1
            { "gOutputVertices",  ShaderBindingType::UAV, BindingUsage::Required },  // u0
        };

        static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
    }

    /// @brief 頂点アニメーション（植物の揺れ・海草）をかけた形を、レイトレーシング用に毎フレーム作る
    /// @details 描画の頂点シェーダーと同じ式（Shaders/Include/Object/VertexAnimation.hlsli）を同じマテリアル定数・
    ///          同じ時間で CS 実行し、持ち主（コンポーネント）ごとの頂点バッファ（VertexData の並び）へ書く。
    ///          AccelerationStructureManager::BuildOrUpdateDynamicBLAS がこれを頂点にして BLAS を更新し、
    ///          水面の映り込みのヒットシェーディングも同じ頂点を読むので、影・水面の反射・コースティクスに
    ///          映る形が画面の揺れと一致する。
    /// @note CS は初めて使うときに組む（揺れるモデルを置かないシーンではコンパイルもしない）。
    class VertexAnimationDeformer {
    public:
        /// @brief ルート定数 gDeform（VertexAnimationDeform.CS.hlsl の DeformConstants と一致させる）
        struct DeformConstants {
            Matrix4x4 world;                  ///< ワールド行列（個体ごとの位相）
            Matrix4x4 worldInverseTranspose;  ///< 風向きをオブジェクト空間へ移す
            uint32_t indexStart = 0;          ///< サブメッシュのインデックスの開始位置（LOD0）
            uint32_t indexCount = 0;          ///< インデックス数
            uint32_t vertexCount = 0;         ///< 頂点数
            uint32_t padding = 0;
        };

        /// @brief Deform の結果
        struct Output {
            ID3D12Resource* vertices = nullptr;       ///< 揺れた頂点（VertexData の並び。NON_PIXEL_SHADER_RESOURCE 状態）
            uint32_t vertexBufferIndex = UINT32_MAX;  ///< その ByteAddressBuffer SRV のヒープ内インデックス
        };

        VertexAnimationDeformer();
        ~VertexAnimationDeformer();

        VertexAnimationDeformer(const VertexAnimationDeformer&) = delete;
        VertexAnimationDeformer& operator=(const VertexAnimationDeformer&) = delete;

        /// @brief 使う GraphicsCore を控える（CS は初回の Deform で組む）
        void Initialize(GraphicsCore* graphicsCore);

        /// @brief 頂点アニメーションをかけた頂点を書く
        /// @param cmdList 積み先（加速構造の構築と同じリスト）
        /// @param owner   持ち主の識別子（コンポーネントのアドレス。持ち主ごとに頂点バッファを持つ）
        /// @param model   変形するモデル（マテリアルの頂点アニメーションの種類・倍率を読む）
        /// @param world   ワールド行列（個体ごとの位相と風向き）
        /// @param params  時間・風（描画と同じ VertexAnimationParams::Build() の値）
        /// @param frame   今フレームの番号（使われなくなったバッファの回収用）
        /// @return 揺れた頂点と、その SRV。失敗なら vertices = nullptr
        Output Deform(ID3D12GraphicsCommandList* cmdList, const void* owner, const Model& model,
            const Matrix4x4& world, const VertexAnimationParams& params, uint64_t frame);

        /// @brief 今フレーム使われなかった頂点バッファを捨てる（GPU が使い終わってから解放される）
        void RetireUnused(uint64_t frame);

        /// @brief 持っている頂点バッファの数
        size_t GetBufferCount() const { return entries_.size(); }

    private:
        /// @brief CS・ルートシグネチャ・PSO を組む（1 回だけ試す）
        bool EnsurePipeline();

        /// @brief 持ち主 1 つぶんの頂点バッファ
        struct Entry {
            GpuResource vertices;
            UniqueDescriptor rawSrv;  ///< ヒットシェーディング用の ByteAddressBuffer SRV
            uint32_t vertexCount = 0;
            uint64_t lastUsedFrame = 0;
        };

        /// @brief 頂点バッファと SRV を、GPU が使い終わってから手放す
        void ReleaseEntry(Entry& entry);

        /// @brief このフレーム数使われなかったバッファを捨てる
        static constexpr uint64_t kKeepFrames = 2;

        GraphicsCore* graphicsCore_ = nullptr;

        std::unique_ptr<ShaderCompiler> shaderCompiler_;
        std::unique_ptr<ShaderReflectionBuilder> reflectionBuilder_;
        std::unique_ptr<RootSignatureManager> rootSignatureMg_;
        std::unique_ptr<ShaderReflectionData> reflectionData_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_;
        BindingTable bindings_;
        bool pipelineTried_ = false;
        bool pipelineReady_ = false;

        std::unordered_map<const void*, Entry> entries_;
    };

    static constexpr Cb::Field kDeformConstantsFields[] = {
        CB_FIELD(VertexAnimationDeformer::DeformConstants, world),
        CB_FIELD(VertexAnimationDeformer::DeformConstants, worldInverseTranspose),
        CB_FIELD(VertexAnimationDeformer::DeformConstants, indexStart),
        CB_FIELD(VertexAnimationDeformer::DeformConstants, indexCount),
        CB_FIELD(VertexAnimationDeformer::DeformConstants, vertexCount),
        CB_FIELD(VertexAnimationDeformer::DeformConstants, padding),
    };
    CB_VERIFY_LAYOUT(VertexAnimationDeformer::DeformConstants, kDeformConstantsFields);
}
