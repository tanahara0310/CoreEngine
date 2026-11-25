#pragma once

#include <cstddef>
#include <cstdint>
#include <source_location>

struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12GraphicsCommandList;

namespace CoreEngine
{
    /// @brief Tracy による GPU の区間の計測（コマンドキュー 1 本ぶん）
    /// @details エディタのあるビルドだけで働き、Release では全部何もしない（Utility/Profiler/ProfileZone.h）。
    ///          区間はタイムスタンプのクエリで測り、Collect でクエリの結果を Tracy へ送る。
    ///          Tracy の画面がつないでいない間はクエリを積まない。
    class GpuProfileZone
    {
    public:
        /// @brief 計測に使うキューを決める（キューを作った後に 1 回）
        static void Initialize(ID3D12Device* device, ID3D12CommandQueue* queue);

        /// @brief 計測をやめる（GPU の完了を待った後・キューを捨てる前に 1 回）
        static void Shutdown();

        /// @brief 済んだクエリの結果を Tracy へ送る（Present の後で 1 フレームに 1 回）
        static void Collect();

        /// @brief cmdList に区間の始まりを積む。区間の終わりは破棄のときに同じ cmdList へ積む
        /// @param name 区間の名前（区間の間だけ生きていればよい）
        GpuProfileZone(ID3D12GraphicsCommandList* cmdList, const char* name,
            std::source_location location = std::source_location::current());
        ~GpuProfileZone();

        GpuProfileZone(const GpuProfileZone&) = delete;
        GpuProfileZone& operator=(const GpuProfileZone&) = delete;

        /// @brief Tracy の区間のオブジェクトを置く場所の大きさ
        static constexpr std::size_t kStorageSize = 64;

    private:
        std::uint64_t storage_[kStorageSize / sizeof(std::uint64_t)];
        bool active_ = false;
    };
}
