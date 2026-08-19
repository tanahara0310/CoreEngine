#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <chrono>
#include <vector>

namespace CoreEngine
{
/// @brief DirectX12コマンド関連の管理クラス
class CommandManager {
public:
    /// @brief 初期化
    /// @param device D3D12デバイス
    /// @param frameCount フレームバッファリング数（デフォルト: 2）
    void Initialize(ID3D12Device* device, UINT frameCount = 2);

    /// @brief デストラクタ
    ~CommandManager();

    /// @brief 投入済みの全 GPU 作業が完了するまでブロックする
    /// @details 「前フレームを待つ」のではなく、このキューへ submit された全作業を待つ完全同期。
    ///          旧名は WaitForPreviousFrame だったが、実挙動と食い違っていたため改名した。
    ///          GPU が参照中のリソースを作り直す／破棄する直前にだけ使うこと
    ///          （毎フレーム呼ぶとパイプラインが空になり性能が出ない）。
    void WaitForGpuIdle();

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

    /// @brief 現在コマンドリストが記録中のフレームインデックスを取得
    /// @details アロケータ/フェンスのローテーションはこの値を使う。
    ///          スワップチェーンの GetCurrentBackBufferIndex() は ResizeBuffers で
    ///          0 にリセットされるため、アロケータ選択に使うと実行中アロケータを
    ///          Reset してしまう（D3D12 ERROR #552）。
    UINT GetRecordingFrameIndex() const { return recordingFrameIndex_; }

    /// @brief 記録中フレームインデックスを設定（FinalizeFrame でのローテーション用）
    void SetRecordingFrameIndex(UINT index) { recordingFrameIndex_ = index % frameCount_; }

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

    // 現在記録中のフレームインデックス（スワップチェーンとは独立にローテーション）
    UINT recordingFrameIndex_ = 0;

    // フェンス & イベント
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    std::uint64_t fenceVal_ = 0;
    std::vector<std::uint64_t> fenceValues_; // フレームごとのフェンス値
    HANDLE fenceEvent_ = nullptr;

    ID3D12Device* device_ = nullptr;

    std::chrono::steady_clock::time_point reference_;
};
}
