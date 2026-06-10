#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>

namespace CoreEngine
{
// 前方宣言
class DescriptorManager;

/// @brief 深度ステンシル管理クラス
/// リソース管理・バリア遷移・クリア・SRVアクセスをすべて一か所で担う
class DepthStencilManager {
public:
    // ---------------------------------------------------------------
    // RAII: スコープ内だけ DEPTH_READ|SRV に遷移し、抜けたら DEPTH_WRITE に戻す
    // ---------------------------------------------------------------
    /// @brief 深度バッファを読み取り専用 SRV として使う区間を管理する RAII ガード
    /// @note フォワード描画（透過オブジェクト・スカイボックス等）で深度値を
    ///       シェーダーからサンプリングしたい区間に使用する
    class ScopedDepthReadSRV {
    public:
        ScopedDepthReadSRV(DepthStencilManager* manager, ID3D12GraphicsCommandList* cmdList);
        ~ScopedDepthReadSRV();

        // コピー・ムーブ禁止
        ScopedDepthReadSRV(const ScopedDepthReadSRV&) = delete;
        ScopedDepthReadSRV& operator=(const ScopedDepthReadSRV&) = delete;

    private:
        DepthStencilManager* manager_;
        ID3D12GraphicsCommandList* cmdList_;
    };

    /// @brief デストラクタ（確保したディスクリプタスロットを解放）
    ~DepthStencilManager();

    /// @brief 初期化
    /// @param device D3D12デバイス
    /// @param descriptorManager ディスクリプタマネージャー
    /// @param width 幅
    /// @param height 高さ
    void Initialize(ID3D12Device* device, DescriptorManager* descriptorManager,
        std::int32_t width, std::int32_t height);

    /// @brief リソースのみ再作成（DSVは再利用）
    /// @param width 新しい幅
    /// @param height 新しい高さ
    void ResizeResource(std::int32_t width, std::int32_t height);

    // ---------------------------------------------------------------
    // バリア遷移 / クリア操作
    // ---------------------------------------------------------------

    /// @brief DEPTH_WRITE 状態へ遷移し、深度バッファをクリアする
    /// @param cmdList コマンドリスト
    /// @note フレーム先頭や GBuffer パス開始前に呼ぶ
    void BeginDepthWrite(ID3D12GraphicsCommandList* cmdList);

    /// @brief DEPTH_WRITE → DEPTH_READ|PIXEL_SHADER_RESOURCE に遷移する
    /// @param cmdList コマンドリスト
    /// @note 深度値をシェーダーからサンプリングしたい区間の開始時に呼ぶ
    /// @note 終了時は必ず EndDepthReadSRV を呼ぶこと（ScopedDepthReadSRV の使用を推奨）
    void BeginDepthReadSRV(ID3D12GraphicsCommandList* cmdList);

    /// @brief DEPTH_READ|PIXEL_SHADER_RESOURCE → DEPTH_WRITE に遷移する
    /// @param cmdList コマンドリスト
    /// @note BeginDepthReadSRV とペアで呼ぶこと（ScopedDepthReadSRV の使用を推奨）
    void EndDepthReadSRV(ID3D12GraphicsCommandList* cmdList);

    // ---------------------------------------------------------------
    // アクセッサ
    // ---------------------------------------------------------------
    ID3D12Resource* GetDepthStencilResource() const { return depthStencilResource_.Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSVHandle() const { return dsvHandle_; }
    /// @brief 深度テクスチャの SRV GPU ハンドルを返す（シェーダーからサンプリングするために使用）
    D3D12_GPU_DESCRIPTOR_HANDLE GetDepthSRVHandle() const { return depthSRVGpuHandle_; }
    /// @brief 現在のリソースステートへの参照を返す（ResourceBarrierHelper で使用）
    D3D12_RESOURCE_STATES& GetCurrentState() { return currentState_; }

private:
    /// @brief 深度ステンシルリソースの作成
    void CreateDepthStencilResource();

    /// @brief 深度ステンシルビューの作成
    void CreateDepthStencilView();

    /// @brief 深度ステンシルビューの更新（既存のハンドルを使用）
    void UpdateDepthStencilView();

    /// @brief 深度リソースの SRV を作成する（シェーダーからのサンプリング用）
    void CreateDepthShaderResourceView();

private:
    // 深度ステンシルリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource_;

    // DSVハンドル
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle_{};

    // 深度 SRV ハンドル（Water Depth Fade 等でシェーダーからサンプリングするために使用）
    D3D12_CPU_DESCRIPTOR_HANDLE depthSRVCpuHandle_{};
    D3D12_GPU_DESCRIPTOR_HANDLE depthSRVGpuHandle_{};

    // リソースステート追跟（ResourceBarrierHelper で利用）
    D3D12_RESOURCE_STATES currentState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;

    // 初期化パラメータ
    ID3D12Device* device_ = nullptr;
    DescriptorManager* descriptorManager_ = nullptr;
    std::int32_t width_ = 0;
    std::int32_t height_ = 0;

    // フリーリスト解放用スロットインデックス（UINT_MAX = 未割り当て）
    UINT dsvSlotIndex_ = UINT_MAX;
    UINT depthSRVSlotIndex_ = UINT_MAX;

    // 初回初期化フラグ
    bool isInitialized_ = false;
};
}
