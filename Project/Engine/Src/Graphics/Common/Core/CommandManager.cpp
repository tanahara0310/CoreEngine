#include "CommandManager.h"
#include "Utility/Logger/Logger.h"

#include <cassert>


namespace CoreEngine
{
void CommandManager::Initialize(ID3D12Device* device)
{
    device_ = device;
    InitializeCommand();
    CreateFenceToEvent();
    InitializeFixFPS();
}

CommandManager::~CommandManager()
{
    // fenceEvent_の解放
    if (fenceEvent_) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
}

void CommandManager::WaitForPreviousFrame()
{
    // フェンスのシグナル待ち
    fenceVal_++;

    // コマンドキューにシグナルを送る
    commandQueue_->Signal(fence_.Get(), fenceVal_);

    // フェンスの値が指定の値になるまで待つ
    if (fence_->GetCompletedValue() < fenceVal_) {
        // イベントを設定
        fence_->SetEventOnCompletion(fenceVal_, fenceEvent_);
        // イベントがシグナルされるまで待つ
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
}

void CommandManager::WaitForFrame(UINT frameIndex)
{
    // 指定されたフレームのGPU処理が完了するまで待機
    const UINT64 fenceValue = fenceValues_[frameIndex];
    if (fence_->GetCompletedValue() < fenceValue) {
        HRESULT hr = fence_->SetEventOnCompletion(fenceValue, fenceEvent_);
        assert(SUCCEEDED(hr));
        (void)hr; // 警告回避：Releaseビルドでassertが消える場合
        WaitForSingleObject(fenceEvent_, INFINITE);
    }

    UpdateFixFPS();
}

void CommandManager::SignalFrame(UINT frameIndex)
{
    // 現在のフレームの処理が完了したことをシグナル
    fenceVal_++;
    fenceValues_[frameIndex] = fenceVal_;
    commandQueue_->Signal(fence_.Get(), fenceVal_);
}

void CommandManager::InitializeFixFPS()
{

    reference_ = std::chrono::high_resolution_clock::now();
}

void CommandManager::UpdateFixFPS()
{
    const std::chrono::microseconds kMinTime(uint64_t(1000000.0f / 60.0f));
    const std::chrono::microseconds kMinCheckTime(uint64_t(1000000.0f / 65.0f));

    // 現在時間を取得
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    // 前回記録からの経過時間を取得
    std::chrono::microseconds elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - reference_);

    // 1/60秒（よりわずかに短い時間）経っていない場合
    if (elapsed < kMinCheckTime) {
        while (std::chrono::steady_clock::now() - reference_ < kMinTime) {
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
    }

    reference_ = std::chrono::steady_clock::now();
}

void CommandManager::InitializeCommand()
{
    HRESULT result = S_FALSE;

    // コマンドキューの生成
    D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
    result = device_->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(commandQueue_.GetAddressOf()));
    // コマンドキューの生成が上手く行かなかったので起動できない
    assert(SUCCEEDED(result));

    // レガシーコマンドアロケータの生成（後方互換性のため）
    result = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(commandAllocator_.GetAddressOf()));
    assert(SUCCEEDED(result));

    // フレームごとのコマンドアロケータの生成
    for (UINT i = 0; i < kFrameCount; ++i) {
        result = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(commandAllocators_[i].GetAddressOf()));
        assert(SUCCEEDED(result));
    }

    // コマンドリストの生成
    result = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator_.Get(), nullptr, IID_PPV_ARGS(commandList_.GetAddressOf()));
    // コマンドリストの生成が上手く行かなかったので起動できない
    assert(SUCCEEDED(result));
}

void CommandManager::CreateFenceToEvent()
{
    HRESULT result = S_FALSE;

    // フェンスの生成
    result = device_->CreateFence(fenceVal_, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    assert(SUCCEEDED(result));

    // イベントの生成
    fenceEvent_ = CreateEvent(NULL, FALSE, FALSE, NULL);
    assert(fenceEvent_ != nullptr);

    // フレームごとのフェンス値を初期化
    for (UINT i = 0; i < kFrameCount; ++i) {
        fenceValues_[i] = 0;
    }
}
}
