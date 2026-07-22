#pragma once
#include <cstdint>
#include <string>

namespace CoreEngine
{
    /// @brief レンダリング統計情報
    struct RenderStats
    {
        uint32_t drawCallCount = 0;              // 通常ドローコール数
        uint32_t instancedDrawCallCount = 0;     // インスタンシングドローコール数
        uint32_t totalInstanceCount = 0;         // 総インスタンス数（描画されたインスタンスの合計）
        uint32_t batchCount = 0;                 // バッチ数（Flush 時に集計）
        uint32_t totalTriangles = 0;             // 描画された総三角形数（インスタンス考慮）
        uint32_t totalVertices = 0;              // 描画された総頂点数（インスタンス考慮）
        static constexpr uint32_t kLodLevelCount = 3;
        uint32_t lodInstanceCounts[kLodLevelCount] = {}; // LODレベル別インスタンス数（0=フル詳細）

        void Reset()
        {
            drawCallCount = 0;
            instancedDrawCallCount = 0;
            totalInstanceCount = 0;
            batchCount = 0;
            totalTriangles = 0;
            totalVertices = 0;
            for (uint32_t& c : lodInstanceCounts) c = 0;
        }

        uint32_t GetTotalDrawCalls() const
        {
            return drawCallCount + instancedDrawCallCount;
        }

        float GetAverageInstancesPerBatch() const
        {
            return batchCount > 0 ? static_cast<float>(totalInstanceCount) / batchCount : 0.0f;
        }
    };

    /// @brief シーン統計情報
    struct SceneStats
    {
        uint32_t totalGameObjectCount = 0;       // 全 GameObject 数
        uint32_t modelInstanceCount = 0;         // モデルインスタンス総数
        uint32_t staticModelCount = 0;           // 静的モデル数
        uint32_t keyframeModelCount = 0;         // キーフレームアニメーションモデル数
        uint32_t skeletonModelCount = 0;         // スケルトンアニメーションモデル数
        uint32_t lightCount = 0;                 // ライト数
        uint32_t playingAnimationCount = 0;      // 再生中アニメーション数

        void Reset()
        {
            totalGameObjectCount = 0;
            modelInstanceCount = 0;
            staticModelCount = 0;
            keyframeModelCount = 0;
            skeletonModelCount = 0;
            lightCount = 0;
            playingAnimationCount = 0;
        }
    };

    /// @brief メモリ使用量統計
    struct MemoryStats
    {
        uint64_t gpuMemoryUsageBytes = 0;        // GPU メモリ使用量（QueryVideoMemoryInfo）
        uint64_t gpuMemoryBudgetBytes = 0;       // GPU メモリ予算
        uint64_t cpuWorkingSetBytes = 0;         // プロセスのワーキングセット
        uint64_t cpuPrivateBytes = 0;            // プロセスのプライベートメモリ
        uint64_t modelVertexBufferBytes = 0;     // モデル頂点バッファ合計サイズ（推定）
        uint64_t modelIndexBufferBytes = 0;      // モデルインデックスバッファ合計サイズ（推定）

        void Reset()
        {
            gpuMemoryUsageBytes = 0;
            gpuMemoryBudgetBytes = 0;
            cpuWorkingSetBytes = 0;
            cpuPrivateBytes = 0;
            modelVertexBufferBytes = 0;
            modelIndexBufferBytes = 0;
        }
    };

    /// @brief リソースキャッシュ統計情報
    struct ResourceCacheStats
    {
        uint32_t loadedModelCount = 0;           // ロード済みモデル数
        uint32_t cacheHitCount = 0;              // キャッシュヒット数
        uint32_t cacheMissCount = 0;             // キャッシュミス数
        uint32_t loadingResourceCount = 0;       // ロード中のリソース数

        void Reset()
        {
            loadedModelCount = 0;
            cacheHitCount = 0;
            cacheMissCount = 0;
            loadingResourceCount = 0;
        }

        float GetCacheHitRate() const
        {
            uint32_t totalAccess = cacheHitCount + cacheMissCount;
            return totalAccess > 0 ? static_cast<float>(cacheHitCount) / totalAccess * 100.0f : 0.0f;
        }
    };

    /// @brief エンジン統計情報の集約
    class EngineStats
    {
    public:
        static EngineStats& GetInstance()
        {
            static EngineStats instance;
            return instance;
        }

        // フレーム開始時に統計をリセット
        void BeginFrame()
        {
            renderStats_.Reset();
            // sceneStats_ / memoryStats_ は外部から書き込まれるためここでは Reset しない
        }

        // レンダリング統計
        RenderStats& GetRenderStats() { return renderStats_; }
        const RenderStats& GetRenderStats() const { return renderStats_; }

        // リソースキャッシュ統計
        ResourceCacheStats& GetResourceCacheStats() { return resourceCacheStats_; }
        const ResourceCacheStats& GetResourceCacheStats() const { return resourceCacheStats_; }

        // シーン統計
        SceneStats& GetSceneStats() { return sceneStats_; }
        const SceneStats& GetSceneStats() const { return sceneStats_; }

        // メモリ統計
        MemoryStats& GetMemoryStats() { return memoryStats_; }
        const MemoryStats& GetMemoryStats() const { return memoryStats_; }

        // ドローコールを記録（三角形数・頂点数も加算）
        void RecordDrawCall(bool isInstanced, uint32_t instanceCount,
            uint32_t indexCount, uint32_t vertexCountPerInstance)
        {
            if (isInstanced)
            {
                renderStats_.instancedDrawCallCount++;
            } else
            {
                renderStats_.drawCallCount++;
            }
            renderStats_.totalInstanceCount += instanceCount;

            // 三角形数 = (indexCount / 3) * instanceCount
            const uint32_t tris = (indexCount / 3) * instanceCount;
            renderStats_.totalTriangles += tris;
            renderStats_.totalVertices += vertexCountPerInstance * instanceCount;
        }

        // 既存呼び出し互換ラッパー（インデックス数 / 頂点数なし）
        void RecordDrawCall(bool isInstanced = false, uint32_t instanceCount = 1)
        {
            RecordDrawCall(isInstanced, instanceCount, 0u, 0u);
        }

        // バッチを記録（バッチ数のみ加算。インスタンス・三角形は RecordDrawCall で集計）
        void RecordBatch()
        {
            renderStats_.batchCount++;
        }

        // LODレベル別のインスタンス使用数を記録（範囲外は最終段へクランプ）
        void RecordLodUsage(uint32_t lodIndex, uint32_t instanceCount)
        {
            const uint32_t clamped = (lodIndex < RenderStats::kLodLevelCount)
                ? lodIndex : (RenderStats::kLodLevelCount - 1);
            renderStats_.lodInstanceCounts[clamped] += instanceCount;
        }

        // 互換用（旧シグネチャ）
        void RecordBatch(uint32_t /*instanceCount*/)
        {
            renderStats_.batchCount++;
        }

        // キャッシュヒットを記録
        void RecordCacheHit()
        {
            resourceCacheStats_.cacheHitCount++;
        }

        // キャッシュミスを記録
        void RecordCacheMiss()
        {
            resourceCacheStats_.cacheMissCount++;
        }

    private:
        EngineStats() = default;
        ~EngineStats() = default;
        EngineStats(const EngineStats&) = delete;
        EngineStats& operator=(const EngineStats&) = delete;

        RenderStats        renderStats_;
        ResourceCacheStats resourceCacheStats_;
        SceneStats         sceneStats_;
        MemoryStats        memoryStats_;
    };
}
