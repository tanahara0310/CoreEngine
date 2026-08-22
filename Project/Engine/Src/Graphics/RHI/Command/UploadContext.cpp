#include "pch.h"
#include "Graphics/RHI/Command/UploadContext.h"

#include <cassert>

#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    // ================================================================
    // ScopedRecording
    // ================================================================

    UploadContext::ScopedRecording::ScopedRecording(
        UploadContext* owner, ID3D12GraphicsCommandList* list, size_t batchIndex)
        : owner_(owner)
        , list_(list)
        , batchIndex_(batchIndex)
    {
    }

    UploadContext::ScopedRecording::ScopedRecording(ScopedRecording&& other) noexcept
        : owner_(other.owner_)
        , list_(other.list_)
        , batchIndex_(other.batchIndex_)
    {
        // 移動元が submit しないよう無効化する（二重 submit / 二重 unlock を防ぐ）。
        other.owner_ = nullptr;
        other.list_ = nullptr;
    }

    UploadContext::ScopedRecording::~ScopedRecording()
    {
        if (!owner_) {
            return; // 無効スコープ（初期化前 or 移動済み）はロックも持っていない
        }
        owner_->SubmitBatch(batchIndex_);
        owner_->mutex_.unlock();
    }

    void UploadContext::ScopedRecording::KeepAlive(Microsoft::WRL::ComPtr<ID3D12Resource> resource)
    {
        if (!owner_ || !resource) {
            return;
        }
        // ロックは BeginRecording から継続保持しているので、ここでの追加は安全。
        owner_->batches_[batchIndex_].keepAlive.push_back(std::move(resource));
    }

    // ================================================================
    // UploadContext
    // ================================================================

    void UploadContext::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue)
    {
        assert(device && queue && "UploadContext requires a device and a queue");
        device_ = device;
        queue_ = queue;

        // 最初のバッチ（アロケータ）とコマンドリストを用意する。
        Batch first{};
        HRESULT hr = device_->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(first.allocator.GetAddressOf()));
        assert(SUCCEEDED(hr) && "UploadContext: failed to create command allocator");
        batches_.push_back(std::move(first));

        hr = device_->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, batches_[0].allocator.Get(), nullptr,
            IID_PPV_ARGS(list_.GetAddressOf()));
        assert(SUCCEEDED(hr) && "UploadContext: failed to create command list");

        // 生成直後のコマンドリストは «記録中» なので閉じておく。
        // BeginRecording が Reset して記録を始める。
        hr = list_->Close();
        assert(SUCCEEDED(hr) && "UploadContext: failed to close the initial command list");

        hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence_.GetAddressOf()));
        assert(SUCCEEDED(hr) && "UploadContext: failed to create fence");

        fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        assert(fenceEvent_ != nullptr && "UploadContext: failed to create fence event");

        (void)hr;

        Logger::GetInstance().Infof(LogCategory::Graphics, LogSubCategory::Command,
            "UploadContext初期化完了（フレーム描画とは独立したコマンドリスト）");
    }

    void UploadContext::Shutdown()
    {
        if (queue_ && fence_) {
            WaitForIdle();
        }

        std::lock_guard<std::mutex> lock(mutex_);
        batches_.clear();
        list_.Reset();
        fence_.Reset();
        if (fenceEvent_) {
            CloseHandle(fenceEvent_);
            fenceEvent_ = nullptr;
        }
        device_ = nullptr;
        queue_ = nullptr;
    }

    UploadContext::ScopedRecording UploadContext::BeginRecording()
    {
        if (!IsInitialized()) {
            // 未初期化でも呼び出し側がクラッシュしないよう、無効スコープを返す。
            // owner_ が nullptr なのでデストラクタは何もせず、ロックも取っていない。
            return ScopedRecording(nullptr, nullptr, 0);
        }

        // 記録終了（ScopedRecording のデストラクタ）まで保持し続けるロック。
        mutex_.lock();

        const size_t batchIndex = AcquireBatch();
        if (batchIndex == SIZE_MAX) {
            mutex_.unlock();
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Command,
                "UploadContext::BeginRecording: バッチを確保できませんでした");
            return ScopedRecording(nullptr, nullptr, 0);
        }

        Batch& batch = batches_[batchIndex];

        HRESULT hr = batch.allocator->Reset();
        if (FAILED(hr)) {
            mutex_.unlock();
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Command,
                "UploadContext::BeginRecording: アロケータの Reset に失敗 hr=0x{:08X}",
                static_cast<unsigned int>(hr));
            return ScopedRecording(nullptr, nullptr, 0);
        }

        hr = list_->Reset(batch.allocator.Get(), nullptr);
        if (FAILED(hr)) {
            mutex_.unlock();
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Command,
                "UploadContext::BeginRecording: コマンドリストの Reset に失敗 hr=0x{:08X}",
                static_cast<unsigned int>(hr));
            return ScopedRecording(nullptr, nullptr, 0);
        }

        return ScopedRecording(this, list_.Get(), batchIndex);
    }

    size_t UploadContext::AcquireBatch()
    {
        const std::uint64_t completed = fence_->GetCompletedValue();

        // 1. 完了済み（または未 submit）のバッチを再利用する。
        //    このタイミングで中間バッファも解放できる。
        for (size_t index = 0; index < batches_.size(); ++index) {
            Batch& batch = batches_[index];
            if (batch.fenceValue == 0 || batch.fenceValue <= completed) {
                batch.keepAlive.clear();
                batch.fenceValue = 0;
                return index;
            }
        }

        // 2. 空きが無ければ増やす。
        if (batches_.size() < kMaxBatches) {
            Batch batch{};
            const HRESULT hr = device_->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(batch.allocator.GetAddressOf()));
            if (FAILED(hr)) {
                return SIZE_MAX;
            }
            batches_.push_back(std::move(batch));
            return batches_.size() - 1;
        }

        // 3. 上限に達したら、最も古い（フェンス値が最小の）バッチの完了を待って再利用する。
        size_t oldestIndex = 0;
        for (size_t index = 1; index < batches_.size(); ++index) {
            if (batches_[index].fenceValue < batches_[oldestIndex].fenceValue) {
                oldestIndex = index;
            }
        }
        WaitForFenceValueLocked(batches_[oldestIndex].fenceValue);
        batches_[oldestIndex].keepAlive.clear();
        batches_[oldestIndex].fenceValue = 0;
        return oldestIndex;
    }

    void UploadContext::SubmitBatch(size_t batchIndex)
    {
        HRESULT hr = list_->Close();
        if (FAILED(hr)) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Command,
                "UploadContext::SubmitBatch: Close に失敗 hr=0x{:08X}",
                static_cast<unsigned int>(hr));
            return;
        }

        ID3D12CommandList* lists[] = { list_.Get() };
        queue_->ExecuteCommandLists(1, lists);

        // キューはフレームと共有のため submit 順 = 実行順。
        // アップロードは、それを読むフレームより必ず前に実行される。
        ++nextFenceValue_;
        queue_->Signal(fence_.Get(), nextFenceValue_);
        batches_[batchIndex].fenceValue = nextFenceValue_;
    }

    void UploadContext::ReleaseCompletedResources()
    {
        if (!IsInitialized()) {
            return;
        }

        // 毎フレーム呼ばれる回収処理なので、記録中のワーカーがいるときは諦めて次フレームに回す。
        // ここで待つと、バッチ枯渇時に AcquireBatch が握っている GPU 待ちへ
        // 描画スレッドが巻き込まれる（回収は遅れても困らない）。
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) {
            return;
        }

        const std::uint64_t completed = fence_->GetCompletedValue();
        for (Batch& batch : batches_) {
            if (batch.fenceValue != 0 && batch.fenceValue <= completed) {
                batch.keepAlive.clear();
                batch.fenceValue = 0;
            }
        }
    }

    void UploadContext::WaitForIdle()
    {
        if (!IsInitialized()) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        std::uint64_t newest = 0;
        for (const Batch& batch : batches_) {
            newest = (batch.fenceValue > newest) ? batch.fenceValue : newest;
        }
        if (newest == 0) {
            return; // まだ何も submit していない
        }

        WaitForFenceValueLocked(newest);

        for (Batch& batch : batches_) {
            if (batch.fenceValue <= newest) {
                batch.keepAlive.clear();
                batch.fenceValue = 0;
            }
        }
    }

    void UploadContext::WaitForFenceValueLocked(std::uint64_t fenceValue)
    {
        if (fenceValue == 0 || fence_->GetCompletedValue() >= fenceValue) {
            return;
        }
        const HRESULT hr = fence_->SetEventOnCompletion(fenceValue, fenceEvent_);
        assert(SUCCEEDED(hr));
        (void)hr;
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
}
