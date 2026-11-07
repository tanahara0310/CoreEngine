#pragma once

#include <d3d12.h>
#include <wrl.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "Graphics/RHI/Command/FrameSync.h"
#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Graphics/RayTracing/RTInstanceMask.h"
#include "Graphics/RayTracing/RayTracingHitData.h"
#include "Graphics/RayTracing/VertexAnimationDeformer.h"
#include "Graphics/Render/Model/VertexAnimation.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector3.h"

namespace CoreEngine
{
    class DescriptorAllocator;
    class GameObjectManager;
    class GraphicsCore;
    class Model;
    class ModelResource;

    /// @brief レイトレーシングのシーン（TLAS に載せる物と、当たった面の材質の表）
    /// @details 毎フレーム Build で、登録された出どころに物を足させ、動く形の BLAS をまとめて作り、
    ///          TLAS を組み、材質の表（RTHitInstance・RTHitSubMesh）を送る。
    ///          表の行は TLAS のインスタンスと同じ並びで、InstanceID() で引ける。
    ///          出どころは RT_SCENE_SOURCE_REGISTER で .cpp から足す。
    class RTScene {
    public:
        /// @brief TLAS に載せる物 1 つ
        struct Instance {
            const ModelResource* resource = nullptr;          ///< 形とサブメッシュ（BLAS を作り終えていること）
            const Model* model = nullptr;                     ///< 材質の持ち主（nullptr ならリソースの既定の材質）
            Matrix4x4 world{};                                ///< ワールド行列
            UINT mask = 0;                                    ///< RTInstanceMask の OR（0 なら載せない）
            D3D12_GPU_DESCRIPTOR_HANDLE baseColorOverride{};  ///< 全サブメッシュのベースカラーを差し替えるテクスチャ（無ければ ptr = 0）
        };

        /// @brief 毎フレーム頂点が変わる形（スキニング・揺れる植物・頂点を動かすマテリアル）
        struct DynamicShape {
            const void* owner = nullptr;              ///< 持ち主の識別子（持ち主ごとに BLAS を 1 本持つ）
            ID3D12Resource* vertices = nullptr;       ///< 今フレームの頂点（VertexData の並び。nullptr なら deform の出力だけを使う）
            uint32_t vertexBufferIndex = UINT32_MAX;  ///< vertices の ByteAddressBuffer SRV のヒープ内インデックス
            bool changed = true;                      ///< 前フレームから vertices が動いたか
            std::optional<VertexAnimationDeformer::Request> deform; ///< 掛ける変形（出力ができたら vertices の代わりに使う）
        };

        /// @brief 1 フレームぶんの入力
        struct FrameInput {
            GameObjectManager* objects = nullptr;          ///< 今のシーンの物（シーンが無ければ nullptr）
            ID3D12GraphicsCommandList* cmdList = nullptr;  ///< 変形・BLAS・TLAS を積む先
            uint64_t frame = 0;                            ///< 今フレームの番号
            bool hasCamera = false;                        ///< cameraPosition が有効か
            Vector3 cameraPosition{};                      ///< ゲームのカメラの位置
            VertexAnimationDeformer* deformer = nullptr;   ///< 頂点を動かす CS（nullptr なら変形しない）
        };

        /// @brief Add が載せなかったときの行の番号
        static constexpr size_t kNoRow = SIZE_MAX;

        /// @brief 出どころが物を足す口（Build の間だけ使える）
        class Builder {
        public:
            /// @brief 今フレームの入力
            const FrameInput& GetFrame() const { return frame_; }
            /// @brief 今フレームの頂点アニメーションの値（風・波の強さと時間）
            const VertexAnimationParams& GetVertexAnimationParams() const { return animParams_; }

            /// @brief 物を静止形の BLAS で足す
            /// @return 行の番号（SetDynamicShape に渡す）。mask が 0 か BLAS が無ければ載せずに kNoRow
            size_t Add(const Instance& instance);

            /// @brief 足した物の形を、毎フレーム作る BLAS に差し替える（作れなければ静止形のまま）
            void SetDynamicShape(size_t row, DynamicShape shape);

            /// @brief 形の決まった物が少し前まで動いていたかを、持ち主ごとに行列を覚えて返す（初めて見た物は false）
            bool TrackSolidMotion(const void* owner, const Matrix4x4& world);

            /// @brief 持ち主の動く形の BLAS を今持っているか
            bool HasDynamicShape(const void* owner) const;

        private:
            friend class RTScene;
            Builder(RTScene& scene, AccelerationStructureManager& asMgr, const FrameInput& frame,
                const VertexAnimationParams& animParams)
                : scene_(scene), asMgr_(asMgr), frame_(frame), animParams_(animParams) {}

            RTScene& scene_;
            AccelerationStructureManager& asMgr_;
            const FrameInput& frame_;
            const VertexAnimationParams& animParams_;
        };

        /// @brief 物を足す関数（Build のたびに、登録した順に呼ばれる）
        using SourceFunction = void (*)(Builder& builder);

        /// @brief 出どころを足す（RT_SCENE_SOURCE_REGISTER から呼ぶ）
        /// @param name デバッグ表示に出す名前（文字列リテラル）
        static void RegisterSource(const char* name, SourceFunction function);

        /// @brief 静的初期化で出どころを足す
        struct SourceRegistrar {
            SourceRegistrar(const char* name, SourceFunction function) { RegisterSource(name, function); }
        };

        /// @brief 出どころ 1 つの、直近の Build で載せた物の数
        struct SourceStats {
            const char* name = "";
            uint32_t instanceCount = 0;
        };

        /// @brief 表を張る先を控える
        void Initialize(ID3D12Device* device, DescriptorAllocator* descriptorAllocator);

        /// @brief 1 フレームぶんの物を集めて TLAS を組み、材質の表を送る（フレームに 1 回）
        /// @details 物が 0 個のフレームとシーンが無いフレームも空の TLAS を組む。
        ///          最後に、今フレーム使われなかった動く形の BLAS・頂点・行列の記録を捨てる。
        void Build(const FrameInput& input, GraphicsCore& graphics, AccelerationStructureManager& asMgr);

        /// @brief 今フレームの表が送られているか
        bool HasHitShadingTables() const { return hitTablesValid_; }
        /// @brief 今フレームのインスタンス表（StructuredBuffer<RTHitInstance>）のヒープ内インデックス
        uint32_t GetHitInstanceTableIndex() const;
        /// @brief 今フレームのサブメッシュ表（StructuredBuffer<RTHitSubMesh>）のヒープ内インデックス
        uint32_t GetHitSubMeshTableIndex() const;

        /// @brief 直近の Build の、kStaticSolid の物（BLAS と行列）から求めた値
        /// @details 止まっている形の決まった物が増えた・減った・置き直されたときに値が変わる
        uint64_t GetStaticSolidSignature() const { return staticSolidSignature_; }

        /// @brief 出どころごとの、直近の Build で載せた物の数（登録した順）
        const std::vector<SourceStats>& GetSourceStats() const { return sourceStats_; }

    private:
        /// @brief 動く形にする行
        struct PendingDynamicShape {
            size_t row = 0;
            DynamicShape shape;
        };

        /// @brief 表 1 本分（フレームインフライトぶんのリング）
        struct HitTableRing {
            std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kMaxFramesInFlight> buffers{};
            std::array<DescriptorHandle, kMaxFramesInFlight> srvs{};
            std::array<UINT, kMaxFramesInFlight> capacities{};
        };

        /// @brief 形の決まった物 1 つぶんの行列の記録
        struct SolidMotionEntry {
            Matrix4x4 world{};
            uint64_t lastMovedFrame = 0;
            uint64_t lastSeenFrame = 0;
            bool moved = false; ///< 一度でも行列が変わったか
        };

        /// @brief 行列が変わってからこのフレーム数は動いている物として扱う
        static constexpr uint64_t kSolidMotionKeepFrames = 90;

        /// @brief 動く形の頂点をまとめて変形し、その BLAS をまとめて作り、行の BLAS と頂点を差し替える
        void BuildDynamicShapes(const FrameInput& input, const VertexAnimationParams& animParams,
            AccelerationStructureManager& asMgr);

        /// @brief kStaticSolid の行の BLAS と行列から値を求める（FNV-1a）
        uint64_t ComputeStaticSolidSignature() const;

        /// @brief 表を今のリングスロットへ書く
        void UploadHitTables(GraphicsCore& graphics, const AccelerationStructureManager& asMgr);

        /// @brief 表の 1 本を今のリングスロットへ書き、SRV を張る
        bool UploadHitTable(GraphicsCore& graphics, HitTableRing& ring, const void* data, UINT elementCount,
            UINT elementStride, const char* debugName);

        bool TrackSolidMotion(const void* owner, const Matrix4x4& world, uint64_t frame);
        void RetireUnusedSolidMotion(uint64_t frame);

        Microsoft::WRL::ComPtr<ID3D12Device> device_;
        DescriptorAllocator* descriptorAllocator_ = nullptr;

        // 今フレームの行（TLAS のインスタンス・表のインスタンス・形の持ち主は同じ並び）
        std::vector<AccelerationStructureManager::InstanceDesc> tlasInstances_;
        std::vector<const ModelResource*> rowResources_;
        std::vector<RTHitInstance> hitInstances_;
        std::vector<RTHitSubMesh> hitSubMeshes_;
        std::vector<PendingDynamicShape> dynamicShapes_;
        std::vector<SourceStats> sourceStats_;

        HitTableRing hitInstanceTables_;
        HitTableRing hitSubMeshTables_;
        uint32_t ringIndex_ = 0;
        bool hitTablesValid_ = false;

        std::unordered_map<const void*, SolidMotionEntry> solidMotion_;
        uint64_t staticSolidSignature_ = 0;
    };
}

#define RT_SCENE_DETAIL_CONCAT_INNER(a, b) a##b
#define RT_SCENE_DETAIL_CONCAT(a, b) RT_SCENE_DETAIL_CONCAT_INNER(a, b)

/// @brief レイトレーシングのシーンへ物を足す関数を登録する（.cpp の名前空間の外に書く）
/// @param NameLiteral デバッグ表示に出す名前
/// @param Function `void (CoreEngine::RTScene::Builder&)` の関数
/// @details 同じ .cpp の中では書いた順に呼ばれる。
#define RT_SCENE_SOURCE_REGISTER(NameLiteral, Function)                                  \
    namespace {                                                                          \
        const ::CoreEngine::RTScene::SourceRegistrar RT_SCENE_DETAIL_CONCAT(             \
            rtSceneSourceRegistrar_, __LINE__){ NameLiteral, &Function };                \
    }
