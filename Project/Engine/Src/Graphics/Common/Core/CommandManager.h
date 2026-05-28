#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <chrono>
#include <vector>

/// @brief DirectX12コマンド関連の管理クラス

namespace CoreEngine
{
class CommandManager {
public:
    /// @brief 初期化
    /// @param device D3D12デバイス
    /// @param frameCount フレームバッファリング数（デフォルト: 2）
    void Initialize(ID3D12Device* device, UINT frameCount = 2);

    /// @brief デストラクタ
    ~CommandManager();

    /// @brief フェンスを待機（GPU同期）
    void WaitForPreviousFrame();

    /// @brief 特定のフレームの完了を待つ（ダブルバッファリング用）
    /// @param frameIndex フレームインデックス
    void WaitForFrame(UINT frameIndex);

    /// @brief フレームの完了をシグナル（ダブルバッファリング用）
    /// @param frameIndex フレームインデックス
    void SignalFrame(UINT frameIndex);


    // アクセッサ
    ID3D12CommandQueue* GetCommandQueue() const { return commandQueue_.Get(); }
    ID3D12CommandAllocator* GetCommandAllocator() const { return commandAllocator_.Get(); }
    ID3D12GraphicsCommandList* GetCommandList() const { return commandList_.Get(); }

    /// @brief フレームバッファリング数を取得
    UINT GetFrameCount() const { return frameCount_; }

    /// @brief 特定のフレームのコマンドアロケータを取得
    ID3D12CommandAllocator* GetCommandAllocator(UINT frameIndex) const { return commandAllocators_[frameIndex].Get(); }

private:
    /// @brief コマンド関連の初期化
    void InitializeCommand();

    /// @brief フェンス&イベントの生成
    void CreateFenceToEvent();

    /// @brief 固定FPS制御の初期化
    void InitializeFixFPS();

    /// @brief 固定FPS制御の更新
    void UpdateFixFPS();

private:
    UINT frameCount_ = 2; // フレームバッファリング数（Initialize で確定）

    // コマンド関連
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_; // レガシー用（後方互換性）
    std::vector<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>> commandAllocators_; // フレームごとのアロケータ
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;

    // フェンス & イベント
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    std::uint64_t fenceVal_ = 0;
    std::vector<std::uint64_t> fenceValues_; // フレームごとのフェンス値
    HANDLE fenceEvent_ = nullptr;

    ID3D12Device* device_ = nullptr;

    std::chrono::steady_clock::time_point reference_;
};
}
