#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>

namespace CoreEngine
{
    class DirectXCommon;

    /// @brief レンダーターゲットの抽象基底クラス
    /// オフスクリーン/バックバッファの共通インターフェースを提供
    class RenderTarget {
    public:
        virtual ~RenderTarget() = default;

        /// @brief レンダリング開始（リソースバリア + RTV/DSV設定 + クリア）
        /// @param cmdList コマンドリスト
        virtual void Begin(ID3D12GraphicsCommandList* cmdList) = 0;

        /// @brief レンダリング終了（リソースバリアで読み込み可能状態に遷移）
        /// @param cmdList コマンドリスト
        virtual void End(ID3D12GraphicsCommandList* cmdList) = 0;

        /// @brief RTVハンドルを取得
        /// @return RTVハンドル
        virtual D3D12_CPU_DESCRIPTOR_HANDLE GetRTVHandle() const = 0;

        /// @brief SRVハンドルを取得（テクスチャとして読む用）
        /// @return SRVハンドル
        virtual D3D12_GPU_DESCRIPTOR_HANDLE GetSRVHandle() const = 0;

        /// @brief リソースを取得
        /// @return リソースポインタ
        virtual ID3D12Resource* GetResource() const = 0;

        /// @brief サイズを取得
        /// @param width 幅（出力）
        /// @param height 高さ（出力）
        virtual void GetSize(int32_t& width, int32_t& height) const = 0;

        /// @brief 幅を取得
        /// @return 幅
        virtual int32_t GetWidth() const = 0;

        /// @brief 高さを取得
        /// @return 高さ
        virtual int32_t GetHeight() const = 0;

        /// @brief クリアカラーを設定
        /// @param color クリアカラー（RGBA）
        void SetClearColor(const float color[4]) {
            clearColor_[0] = color[0];
            clearColor_[1] = color[1];
            clearColor_[2] = color[2];
            clearColor_[3] = color[3];
        }

        /// @brief クリアカラーを取得
        /// @return クリアカラー配列
        const float* GetClearColor() const { return clearColor_; }

    protected:
        /// @brief リソースバリアを設定するヘルパー関数
        /// @param cmdList コマンドリスト
        /// @param resource 遷移対象のリソース
        /// @param stateBefore 現在のステート
        /// @param stateAfter 遷移先のステート
        void TransitionBarrier(
            ID3D12GraphicsCommandList* cmdList,
            ID3D12Resource* resource,
            D3D12_RESOURCE_STATES stateBefore,
            D3D12_RESOURCE_STATES stateAfter);

        float clearColor_[4] = {0.1f, 0.25f, 0.5f, 1.0f};
    };
}
