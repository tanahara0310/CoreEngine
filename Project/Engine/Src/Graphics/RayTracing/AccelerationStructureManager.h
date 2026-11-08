#pragma once

#include <d3d12.h>
#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include "Graphics/RHI/Descriptor/UniqueDescriptor.h"
#include "Graphics/RHI/Command/FrameSync.h" // kMaxFramesInFlight（インスタンスバッファのリング段数）
#include <wrl.h>
#include <array>
#include <span>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include "Math/Matrix/Matrix4x4.h"

namespace CoreEngine
{
    class DescriptorAllocator;
    class ModelResource;
    class DeferredReleaseQueue;

    /// @brief BLAS/TLAS を管理するクラス（DXR レイトレーシング用）
    /// @note ID3D12Device5 が必須。非対応 GPU では Initialize() が false を返す
    class AccelerationStructureManager {
    public:
        /// @brief サブメッシュを持たないモデルの全体を表す GeometryRange::subMeshIndex
        static constexpr uint32_t kWholeMesh = UINT32_MAX;

        /// @brief BLAS の形 1 つ（サブメッシュ 1 つ）が使う索引の範囲
        /// @details 並びは HLSL の GeometryIndex() と、RTScene のサブメッシュ表の並び
        struct GeometryRange {
            UINT startIndex = 0;                 ///< 索引バッファ内の開始位置
            UINT indexCount = 0;                 ///< 索引の数（3 以上）
            uint32_t subMeshIndex = kWholeMesh;  ///< サブメッシュの番号（kWholeMesh = サブメッシュを持たないモデルの全体）

            bool operator==(const GeometryRange&) const = default;
        };

        /// @brief モデルの BLAS の形の並びを作る（索引が 3 未満のサブメッシュは除く）
        /// @param lod 使う LOD の段（サブメッシュが持つ段数を超えるときは最後の段）
        /// @param out 形の並び（モデルに三角形が無ければ空）
        static void CollectGeometryRanges(const ModelResource& resource, uint32_t lod, std::vector<GeometryRange>& out);

        /// @brief BLAS 構築に必要なメッシュ情報
        struct BLASDesc {
            ID3D12Resource* vertexBuffer = nullptr;
            UINT vertexCount = 0;
            UINT vertexStride = 0;
            DXGI_FORMAT vertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
            UINT vertexPositionOffset = 0;
            ID3D12Resource* indexBuffer = nullptr;
            DXGI_FORMAT indexFormat = DXGI_FORMAT_R32_UINT;
            std::span<const GeometryRange> geometries;  ///< 形の並び（1 つ以上）
        };

        /// @brief モデルの静止形の BLAS
        struct StaticBLAS {
            D3D12_GPU_VIRTUAL_ADDRESS address = 0;
            uint32_t vertexBufferIndex = UINT32_MAX;  ///< 頂点バッファ（ByteAddressBuffer）のヒープ内インデックス
            uint32_t indexBufferIndex = UINT32_MAX;   ///< 索引バッファ（ByteAddressBuffer）のヒープ内インデックス
        };

        /// @brief TLAS インスタンス情報
        struct InstanceDesc {
            /// @brief 撃つ BLAS（AcquireStaticBLAS か BuildOrUpdateDynamicBLAS の戻り値）
            D3D12_GPU_VIRTUAL_ADDRESS blasAddress = 0;
            float transform[3][4] = {};   ///< DXR 準拠の行優先 3×4 アフィン行列
            UINT instanceMask = 0xFF;     ///< RTInstanceMask の OR
            /// @brief FORCE_OPAQUE なら、どの形もアルファで抜かずに当てる
            D3D12_RAYTRACING_INSTANCE_FLAGS flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;

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
        /// @note シェーダーは RayQuery を使うので、レイトレーシングティア 1.1 未満は非対応として扱う
        bool Initialize(ID3D12Device* device, DescriptorAllocator* descriptorAllocator);

        /// @brief 全インスタンスから TLAS を構築する（毎フレーム呼び出し可能）
        void BuildTLAS(ID3D12GraphicsCommandList* cmdList,
            const std::vector<InstanceDesc>& instances);

        /// @brief モデルの静止形の BLAS を返す（無いときと、形・バッファが前と変わったときは積んで作る）
        /// @param cmdList    構築を積む先
        /// @param resource   形の持ち主
        /// @param geometries 形の並び（CollectGeometryRanges の結果。1 つ以上）
        /// @param frame      今フレームの番号（使われなくなった BLAS の解放に使う）
        /// @return 作れなければ nullptr（次に Acquire するまで有効）
        const StaticBLAS* AcquireStaticBLAS(ID3D12GraphicsCommandList* cmdList, const ModelResource& resource,
            std::span<const GeometryRange> geometries, uint64_t frame);

        /// @brief kStaticKeepFrames フレーム使われなかった静止形の BLAS を退避リストへ回す（BuildTLAS の後に呼ぶ）
        void RetireUnusedStaticBLAS(uint64_t frame);

        /// @brief 動的 BLAS の構築・更新を積み始める（EndDynamicBLASBatch と組で、フレームに 1 回）
        /// @param cmdList 積み先
        /// @return 積めるなら true
        /// @details 組の中の構築・更新は 1 本ずつ待たずに続けて積む。スクラッチは 1 本のバッファを
        ///          構築ごとに切り分けて使い、足りなくなったときだけ UAV バリアで前の構築を待ってから先頭へ戻る。
        bool BeginDynamicBLASBatch(ID3D12GraphicsCommandList* cmdList);

        /// @brief 形が毎フレーム変わるメッシュの BLAS を構築・更新する（揺れる植物・スキニングモデル）
        /// @param owner   持ち主の識別子（コンポーネントのアドレスなど。持ち主ごとに 1 本持つ）
        /// @param desc    頂点・インデックス・形の並び。頂点バッファは NON_PIXEL_SHADER_RESOURCE を含む状態にしておくこと
        /// @param frame   今フレームの番号（使われなくなった BLAS の回収に使う）
        /// @param geometryChanged 前フレームから頂点が動いたか（false なら作り直さずに前の BLAS を使う）
        /// @return BLAS の GPU 仮想アドレス（InstanceDesc::blasAddress へ入れる）。作れなければ 0
        /// @details BeginDynamicBLASBatch と EndDynamicBLASBatch の間で呼ぶ。
        ///          初回と形（頂点数・形の並び・バッファ）が変わったときは ALLOW_UPDATE 付きで構築し、
        ///          以後は前回の結果をその場で更新（refit）する。kDynamicRebuildInterval 回に 1 回は
        ///          構築し直す（持ち主ごとにずらす）。
        D3D12_GPU_VIRTUAL_ADDRESS BuildOrUpdateDynamicBLAS(const void* owner, const BLASDesc& desc, uint64_t frame,
            bool geometryChanged = true);

        /// @brief 積んだ構築・更新の書き込みが TLAS の構築より前に終わるよう、UAV バリアを 1 回張って組を閉じる
        void EndDynamicBLASBatch();

        /// @brief 持ち主の動的 BLAS を今持っているか
        bool HasDynamicBLAS(const void* owner) const { return dynamicBlas_.find(owner) != dynamicBlas_.end(); }

        /// @brief 今フレーム使われなかった動的 BLAS を退避リストへ回す（BuildTLAS の後に呼ぶ）
        void RetireUnusedDynamicBLAS(uint64_t frame);

        /// @brief 動的 BLAS の数
        UINT GetDynamicBLASCount() const { return static_cast<UINT>(dynamicBlas_.size()); }

        /// @brief TLAS の SRV GPU ハンドルを取得
        D3D12_GPU_DESCRIPTOR_HANDLE GetTLASSRVHandle() const {
            return tlasSRVDescriptors_[tlasInstanceRingIndex_].gpuHandle;
        }

        /// @brief 直近の BuildTLAS が組み終えていて GetTLASSRVHandle() へ撃てるか（物が 0 個の空の TLAS も含む）
        bool IsTLASReady() const { return tlasReady_; }

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

        /// @brief 今持っている静止形の BLAS の数
        UINT GetBLASCount() const { return static_cast<UINT>(staticBlas_.size()); }

        // ──────────────────────────────────────────────────────────
        // デバッグ表示用の統計（レイトレーシングのデバッグ窓が参照する）
        // ──────────────────────────────────────────────────────────

        /// @brief D3D12_RAYTRACING_TIER の生値（非対応時は 0）
        UINT GetRaytracingTier() const { return raytracingTier_; }

        /// @brief 直近の BuildTLAS に渡したインスタンス数
        UINT GetTLASInstanceCount() const { return tlasInstanceCount_; }

        /// @brief 静止形の BLAS の結果バッファ合計バイト数
        UINT64 GetBLASTotalBytes() const;

        /// @brief 使われなくなって解放した静止形の BLAS の数（起動からの合計）
        uint64_t GetReleasedBLASCount() const { return releasedStaticBlasCount_; }

        /// @brief TLAS 結果バッファのバイト数（未構築なら 0）
        UINT64 GetTLASResultBytes() const;

        /// @brief AS 構築用スクラッチバッファの合計バイト数（BLAS 用 + TLAS 用）
        UINT64 GetScratchTotalBytes() const;

        /// @brief 解放待ちの退避リソース数（リーク調査用）
        size_t GetRetiredResourceCount() const { return retiredResources_.size(); }

        /// @brief モデルの静止形の BLAS の結果バッファのバイト数（無ければ 0）
        UINT64 GetStaticBLASBytes(const ModelResource* resource) const;

        /// @brief 持ち主の動的 BLAS の結果バッファのバイト数（無ければ 0）
        UINT64 GetDynamicBLASBytes(const void* owner) const;

    private:
        void EnsureScratchBuffer(UINT64 requiredSize);

        /// @brief スクラッチ用のバッファ（DEFAULT・UAV）を作る
        Microsoft::WRL::ComPtr<ID3D12Resource> CreateScratch(UINT64 size) const;

        /// @brief BLASDesc から形ごとの三角形ジオメトリの記述子を作る（静止形と動的 BLAS で共通）
        /// @details 形はアルファで抜く判定をシェーダーへ任せる（不透明にするかは TLAS のインスタンスで決める）
        static void MakeGeometryDescs(const BLASDesc& desc, std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>& out);

        /// @brief 結果バッファ（DEFAULT・UAV・加速構造ステート）を作る
        Microsoft::WRL::ComPtr<ID3D12Resource> CreateASBuffer(UINT64 size) const;

        Microsoft::WRL::ComPtr<ID3D12Device5> device5_;
        DescriptorAllocator* descriptorAllocator_ = nullptr;

        /// @brief 静止形の BLAS 1 本分（モデルごと）
        struct StaticBLASEntry {
            StaticBLAS view;
            Microsoft::WRL::ComPtr<ID3D12Resource> result;
            // 作ったときの形（変わったら作り直す）。SRV が指している間はバッファを持っておく
            Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
            Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;
            UINT vertexCount = 0;
            std::vector<GeometryRange> geometries;
            UniqueDescriptor vertexBufferSrv;   ///< 頂点バッファの ByteAddressBuffer SRV
            UniqueDescriptor indexBufferSrv;    ///< 索引バッファの ByteAddressBuffer SRV
            uint64_t lastUsedFrame = 0;
        };
        std::unordered_map<const ModelResource*, StaticBLASEntry> staticBlas_;

        /// @brief このフレーム数使われなかった静止形の BLAS を捨てる
        static constexpr uint64_t kStaticKeepFrames = 300;

        /// @brief 静止形の BLAS を積んで作り、entry の結果と SRV を差し替える
        bool BuildStaticBLAS(ID3D12GraphicsCommandList* cmdList, const BLASDesc& desc, StaticBLASEntry& entry);

        /// @brief entry の結果・バッファを退避リストへ回し、SRV を手放す
        void RetireStaticBLAS(StaticBLASEntry& entry);

        uint64_t releasedStaticBlasCount_ = 0;

        /// @brief MakeGeometryDescs の書き込み先（構築のたびに使い回す）
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescScratch_;

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
            std::vector<GeometryRange> geometries;
            UINT64 buildScratchSize = 0;     ///< 構築に要るスクラッチ（形が変わったときに問い合わせた値）
            UINT64 updateScratchSize = 0;    ///< 更新に要るスクラッチ
            uint32_t updatesSinceBuild = 0;  ///< 最後に構築し直してからの更新回数
            uint64_t lastUsedFrame = 0;
        };
        std::unordered_map<const void*, DynamicBLASEntry> dynamicBlas_;

        /// @brief 動的 BLAS をこの回数更新したら構築し直す
        static constexpr uint32_t kDynamicRebuildInterval = 120;
        /// @brief このフレーム数使われなかった動的 BLAS を捨てる
        static constexpr uint64_t kDynamicKeepFrames = 2;

        /// @brief 動的 BLAS のスクラッチから len バイトを切り出す（足りなければ前の構築を待って先頭へ戻る）
        D3D12_GPU_VIRTUAL_ADDRESS AllocateDynamicScratch(UINT64 len);

        // 動的 BLAS の組（BeginDynamicBLASBatch 〜 EndDynamicBLASBatch）
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> dynamicCmdList_;
        Microsoft::WRL::ComPtr<ID3D12Resource> dynamicScratch_;
        UINT64 dynamicScratchSize_ = 0;
        UINT64 dynamicScratchOffset_ = 0;
        UINT64 dynamicScratchDemand_ = 0;  ///< 直近の組で切り出した合計（次の組のスクラッチの大きさ）
        bool dynamicBuildsPending_ = false;
        /// @brief 動的 BLAS のスクラッチの上限（これを超える組は途中で前の構築を待って使い回す）
        static constexpr UINT64 kDynamicScratchMaxBytes = 64ull << 20;

        // 静止形の BLAS 構築用スクラッチ（再利用）
        Microsoft::WRL::ComPtr<ID3D12Resource> blasScratch_;
        UINT64 blasScratchSize_ = 0;

        // コマンドリスト実行完了まで旧リソースを保持する退避リスト
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> retiredResources_;

        // デバッグ表示用の統計
        UINT raytracingTier_ = 0;       ///< D3D12_RAYTRACING_TIER の生値
        UINT tlasInstanceCount_ = 0;    ///< 直近の BuildTLAS のインスタンス数
        bool tlasReady_ = false;        ///< 直近の BuildTLAS が組み終えたか

        bool isSupported_ = false;
    };
}
