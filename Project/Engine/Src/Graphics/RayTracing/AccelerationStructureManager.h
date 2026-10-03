#pragma once

#include <d3d12.h>
#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include "Graphics/RHI/Command/FrameSync.h" // kMaxFramesInFlight（インスタンスバッファのリング段数）
#include "Graphics/RayTracing/RayTracingHitData.h"
#include <wrl.h>
#include <array>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include "Math/Matrix/Matrix4x4.h"

namespace CoreEngine
{
    class DescriptorAllocator;
    class ModelResource;
    class DeferredReleaseQueue;

    /// @brief TLAS インスタンスの種類の印（InstanceMask のビット）
    /// @details レイは当てたい種類の印の OR を TraceRay の InstanceInclusionMask に渡す（全部なら kAll）
    namespace RayTracingInstanceMask {
        inline constexpr UINT kSolid = 0x01;      ///< 形の決まった物（地形・岩・建物など、下の 3 つ以外のメッシュ）
        inline constexpr UINT kVegetation = 0x02; ///< 風や波で揺れる材質（植物・海草）を持つメッシュ
        inline constexpr UINT kParticle = 0x04;   ///< モデルの粒
        inline constexpr UINT kCreature = 0x08;   ///< 泳ぐ生き物（魚の動きの材質を持つメッシュ）
        inline constexpr UINT kAll = 0xFF;
    }

    /// @brief BLAS/TLAS を管理するクラス（DXR レイトレーシング用）
    /// @note ID3D12Device5 が必須。非対応 GPU では Initialize() が false を返す
    class AccelerationStructureManager {
    public:
        /// @brief BLAS 構築に必要なメッシュ情報
        struct BLASDesc {
            ID3D12Resource* vertexBuffer = nullptr;
            UINT vertexCount = 0;
            UINT vertexStride = 0;
            DXGI_FORMAT vertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
            UINT vertexPositionOffset = 0;
            ID3D12Resource* indexBuffer = nullptr;
            UINT indexCount = 0;
            DXGI_FORMAT indexFormat = DXGI_FORMAT_R32_UINT;
        };

        /// @brief TLAS インスタンス情報
        struct InstanceDesc {
            UINT blasIndex = 0;
            /// @brief 0 以外なら blasIndex の代わりにこの BLAS を使う（BuildOrUpdateDynamicBLAS の戻り値）
            D3D12_GPU_VIRTUAL_ADDRESS blasAddress = 0;
            float transform[3][4] = {};   ///< DXR 準拠の行優先 3×4 アフィン行列
            UINT instanceMask = 0xFF;

            /// @brief Matrix4x4 からDXR用 3×4 行列に変換してセットする
            /// @note DXR の D3D12_RAYTRACING_INSTANCE_DESC は行優先 3×4 で
            ///       平行移動が [row][3] に来る形式を要求する。
            ///       エンジンの Matrix4x4 は平行移動が m[3][col] にあるため転置が必要。
            void SetTransform(const Matrix4x4& mat) {
                for (int row = 0; row < 3; ++row) {
                    for (int col = 0; col < 4; ++col) {
                        transform[row][col] = mat.m[col][row];
                    }
                }
            }
        };

        /// @brief 初期化（DXR サポート確認を含む）
        /// @return DXR 非対応の場合 false
        bool Initialize(ID3D12Device* device, DescriptorAllocator* descriptorAllocator);

        /// @brief BLAS を構築して登録する
        /// @return BLAS インデックス（BuildTLAS の InstanceDesc::blasIndex で使う）
        UINT BuildBLAS(ID3D12GraphicsCommandList* cmdList, const BLASDesc& desc);

        /// @brief 全インスタンスから TLAS を構築する（毎フレーム呼び出し可能）
        void BuildTLAS(ID3D12GraphicsCommandList* cmdList,
            const std::vector<InstanceDesc>& instances);

        /// @brief ModelResource から BLAS を構築し、インデックスを ModelResource に設定する
        /// @return 成功した場合 true（既に構築済みの場合も true）
        bool BuildBLASFromModelResource(ID3D12GraphicsCommandList* cmdList, ModelResource* resource);

        /// @brief 形が毎フレーム変わるメッシュの BLAS を構築・更新する（揺れる植物・スキニングモデル）
        /// @param cmdList 積み先
        /// @param owner   持ち主の識別子（コンポーネントのアドレスなど。持ち主ごとに 1 本持つ）
        /// @param desc    頂点とインデックス。頂点バッファは NON_PIXEL_SHADER_RESOURCE を含む状態にしておくこと
        /// @param frame   今フレームの番号（使われなくなった BLAS の回収に使う）
        /// @param geometryChanged 前フレームから頂点が動いたか（false なら作り直さずに前の BLAS を使う）
        /// @return BLAS の GPU 仮想アドレス（InstanceDesc::blasAddress へ入れる）。作れなければ 0
        /// @details 初回と形（頂点数・インデックス数・バッファ）が変わったときは ALLOW_UPDATE 付きで構築し、
        ///          以後は前回の結果をその場で更新（refit）する。更新を重ねると木構造が緩んで
        ///          トレースが遅くなるので、kDynamicRebuildInterval 回に 1 回は構築し直す（持ち主ごとにずらす）。
        D3D12_GPU_VIRTUAL_ADDRESS BuildOrUpdateDynamicBLAS(ID3D12GraphicsCommandList* cmdList,
            const void* owner, const BLASDesc& desc, uint64_t frame, bool geometryChanged = true);

        /// @brief 今フレーム使われなかった動的 BLAS を退避リストへ回す（BuildTLAS の後に呼ぶ）
        void RetireUnusedDynamicBLAS(uint64_t frame);

        /// @brief 動的 BLAS の数
        UINT GetDynamicBLASCount() const { return static_cast<UINT>(dynamicBlas_.size()); }

        /// @brief TLAS の SRV GPU ハンドルを取得
        D3D12_GPU_DESCRIPTOR_HANDLE GetTLASSRVHandle() const {
            return tlasSRVDescriptors_[tlasInstanceRingIndex_].gpuHandle;
        }

        /// @brief DXR がサポートされているか
        bool IsSupported() const { return isSupported_; }

        /// @brief 退避リソースを遅延解放キューへ引き渡す
        /// @details 解放を予約するだけなのでフレームがストールしない
        /// @param queue 引き渡し先
        /// @param fenceValue この値まで GPU が進めば解放してよい
        /// @warning **フレームを Signal した後**（GraphicsCore::EndFrame の後）に呼ぶこと。
        ///          先に呼ぶと、退避リソースを参照している今フレームの作業より前の
        ///          フェンス値が入ってしまい、GPU 使用中に解放されうる。
        void MoveRetiredResourcesTo(DeferredReleaseQueue& queue, std::uint64_t fenceValue);

        /// @brief 退避リソースがあるか
        bool HasRetiredResources() const { return !retiredResources_.empty(); }

        /// @brief 登録済み BLAS 数を取得
        UINT GetBLASCount() const { return static_cast<UINT>(blasList_.size()); }

        // ──────────────────────────────────────────────────────────
        // ヒットシェーディング（当たった三角形の頂点と材質をシェーダーから引く）
        // ──────────────────────────────────────────────────────────

        /// @brief BLAS の頂点バッファ（ByteAddressBuffer）のヒープ内インデックス（無ければ UINT32_MAX）
        uint32_t GetBLASVertexBufferIndex(UINT blasIndex) const;
        /// @brief BLAS の索引バッファ（ByteAddressBuffer）のヒープ内インデックス（無ければ UINT32_MAX）
        uint32_t GetBLASIndexBufferIndex(UINT blasIndex) const;

        /// @brief TLAS のインスタンスと同じ並びの表を送る（BuildTLAS の直後に、同じインスタンスの並びで呼ぶ）
        /// @param instances TLAS のインスタンスと 1:1 の行
        /// @param subMeshes instances の firstSubMesh / subMeshCount が指すサブメッシュの行
        void UploadHitShadingTables(
            const std::vector<RTHitInstance>& instances, const std::vector<RTHitSubMesh>& subMeshes);

        /// @brief 今フレームの表が送られているか
        bool HasHitShadingTables() const { return hitTablesValid_; }
        /// @brief 今フレームのインスタンス表（StructuredBuffer）のヒープ内インデックス
        uint32_t GetHitInstanceTableIndex() const;
        /// @brief 今フレームのサブメッシュ表（StructuredBuffer）のヒープ内インデックス
        uint32_t GetHitSubMeshTableIndex() const;

        // ──────────────────────────────────────────────────────────
        // デバッグ表示用の統計（Stage 0: RayTracingDebugPanel が参照する）
        // ──────────────────────────────────────────────────────────

        /// @brief D3D12_RAYTRACING_TIER の生値（非対応時は 0）
        UINT GetRaytracingTier() const { return raytracingTier_; }

        /// @brief 直近の BuildTLAS に渡したインスタンス数
        UINT GetTLASInstanceCount() const { return tlasInstanceCount_; }

        /// @brief 全 BLAS の結果バッファ合計バイト数
        UINT64 GetBLASTotalBytes() const;

        /// @brief TLAS 結果バッファのバイト数（未構築なら 0）
        UINT64 GetTLASResultBytes() const;

        /// @brief AS 構築用スクラッチバッファの合計バイト数（BLAS 用 + TLAS 用）
        UINT64 GetScratchTotalBytes() const;

        /// @brief 解放待ちの退避リソース数（リーク調査用）
        size_t GetRetiredResourceCount() const { return retiredResources_.size(); }

    private:
        void EnsureScratchBuffer(UINT64 requiredSize);

        /// @brief BLASDesc から三角形ジオメトリの記述子を作る（BuildBLAS と動的 BLAS で共通）
        static D3D12_RAYTRACING_GEOMETRY_DESC MakeGeometryDesc(const BLASDesc& desc);

        /// @brief 結果バッファ（DEFAULT・UAV・加速構造ステート）を作る
        Microsoft::WRL::ComPtr<ID3D12Resource> CreateASBuffer(UINT64 size) const;

        Microsoft::WRL::ComPtr<ID3D12Device5> device5_;
        DescriptorAllocator* descriptorAllocator_ = nullptr;

        /// @brief BLAS 1 本分の GPU リソース
        struct BLASEntry {
            Microsoft::WRL::ComPtr<ID3D12Resource> result;
            DescriptorHandle vertexBufferSrv;   ///< 頂点バッファの ByteAddressBuffer SRV
            DescriptorHandle indexBufferSrv;    ///< 索引バッファの ByteAddressBuffer SRV
        };
        std::vector<BLASEntry> blasList_;

        /// @brief ヒットシェーディングの表 1 本分（フレームインフライトぶんのリング）
        struct HitTableRing {
            std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kMaxFramesInFlight> buffers{};
            std::array<DescriptorHandle, kMaxFramesInFlight> srvs{};
            std::array<UINT, kMaxFramesInFlight> capacities{};
        };
        HitTableRing hitInstanceTables_;
        HitTableRing hitSubMeshTables_;
        bool hitTablesValid_ = false;

        /// @brief 表の 1 本を今のリングスロットへ書き、SRV を張る
        bool UploadHitTable(HitTableRing& ring, const void* data, UINT elementCount, UINT elementStride,
            const char* debugName);

        // TLAS リソース
        /// @brief TLAS 結果バッファと、その SRV（フレームインフライトぶんのリング）
        /// @details 1 枚＋1 ディスクリプタで回すと、インスタンスが増えてバッファを
        ///          張り直したフレームで破綻する。張り直すと GPU 仮想アドレスが変わるため
        ///          SRV を書き直す必要があるが、ディスクリプタヒープは GPU が
        ///          「コマンドを実行する時点」で読む。CPU は kMaxFramesInFlight フレームまで
        ///          先行できるので、まだ実行されていない前フレームの DispatchRays が
        ///          新しい（まだビルドしていない）バッファを指す SRV を読んでしまい、
        ///          そのフレームだけ影が丸ごと消える。マップを前進してインスタンス数が
        ///          過去最大を更新するたびに起きるので、「新しいブロックへ進むと影がちらつき、
        ///          戻るとちらつかない」という症状になる。
        ///          フレームごとに別のバッファと別のディスクリプタを使えば、
        ///          実行待ちのフレームが参照している SRV を書き換えることが無くなる。
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kMaxFramesInFlight> tlasResults_{};
        std::array<DescriptorHandle, kMaxFramesInFlight> tlasSRVDescriptors_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> tlasScratch_;

        /// @brief TLAS インスタンス記述子バッファ（フレームインフライトぶんのリング）
        /// @details UPLOAD ヒープなので GPU はコマンド実行時に直接ここを読む。
        ///          1 枚を使い回すと、CPU が次フレームぶんを書いている最中に
        ///          GPU がまだ前フレームの TLAS ビルドで同じ番地を読んでいる。
        ///          フレームごとに別の番地へ書けばその競合が起きない。
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kMaxFramesInFlight>
            tlasInstanceDescBuffers_{};

        /// @brief 次に書き込むリングスロット（BuildTLAS 呼び出しごとに 1 つ進む）
        /// @details インスタンス記述子バッファ・結果バッファ・SRV の 3 つで共有する。
        uint32_t tlasInstanceRingIndex_ = 0;

        /// @brief 動的 BLAS 1 本分（持ち主ごと）
        struct DynamicBLASEntry {
            Microsoft::WRL::ComPtr<ID3D12Resource> result;
            UINT64 resultSize = 0;
            // 構築したときの形（変わったら更新でなく構築し直す）
            D3D12_GPU_VIRTUAL_ADDRESS vertexAddress = 0;
            UINT vertexCount = 0;
            UINT vertexStride = 0;
            D3D12_GPU_VIRTUAL_ADDRESS indexAddress = 0;
            UINT indexCount = 0;
            uint32_t updatesSinceBuild = 0;  ///< 最後に構築し直してからの更新回数
            uint64_t lastUsedFrame = 0;
        };
        std::unordered_map<const void*, DynamicBLASEntry> dynamicBlas_;

        /// @brief 動的 BLAS をこの回数更新したら構築し直す
        static constexpr uint32_t kDynamicRebuildInterval = 120;
        /// @brief このフレーム数使われなかった動的 BLAS を捨てる
        static constexpr uint64_t kDynamicKeepFrames = 2;

        // BLAS 構築用スクラッチ（再利用）
        Microsoft::WRL::ComPtr<ID3D12Resource> blasScratch_;
        UINT64 blasScratchSize_ = 0;

        // コマンドリスト実行完了まで旧リソースを保持する退避リスト
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> retiredResources_;

        // デバッグ表示用の統計
        UINT raytracingTier_ = 0;       ///< D3D12_RAYTRACING_TIER の生値
        UINT tlasInstanceCount_ = 0;    ///< 直近の BuildTLAS のインスタンス数

        bool isSupported_ = false;
    };
}
