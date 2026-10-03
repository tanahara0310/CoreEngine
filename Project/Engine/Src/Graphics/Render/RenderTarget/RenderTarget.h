#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>

#include "Graphics/RHI/Resource/GpuResource.h"

namespace CoreEngine
{
    class GraphicsCore;

    /// @brief Begin で深度を束ねるか
    enum class DepthBinding {
        FromDescriptor, ///< 作ったときの記述子に従う（深度ありで作ったものだけ束ねる）
        None,           ///< 束ねない（共有のシーン深度に触れない）
    };

    /// @brief Begin の設定（呼ぶたびに値で渡す。ターゲットには残らない）
    struct RenderTargetBeginDesc {
        bool clear = true;                                 ///< RTV（と束ねた深度）をクリアするか
        DepthBinding depth = DepthBinding::FromDescriptor; ///< 深度を束ねるか
        const float* clearColor = nullptr;                 ///< クリア色（nullptr ならターゲットの既定色）
    };

    /// @brief Begin が実際に束ねたビュー
    struct RenderTargetBinding {
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        D3D12_CPU_DESCRIPTOR_HANDLE dsv{}; ///< 深度を束ねていなければ ptr が 0
    };

    /// @brief レンダーターゲットの抽象基底クラス
    /// オフスクリーン/バックバッファの共通インターフェースを提供
    class RenderTarget {
    public:
        virtual ~RenderTarget() = default;

        /// @brief レンダリング開始（リソースバリア + RTV/DSV設定 + クリア）
        /// @param cmdList コマンドリスト
        /// @param desc 今回のクリア・深度・クリア色
        /// @return 実際に束ねた RTV / DSV
        virtual RenderTargetBinding Begin(ID3D12GraphicsCommandList* cmdList, const RenderTargetBeginDesc& desc = {}) = 0;

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

        /// @brief リソースをステート追跡つきで取得する
        /// @details バリアを張る側は必ずこちらを使う。
        ///          呼び出し側が `D3D12_RESOURCE_STATES` を持たないための入口
        virtual GpuResource& Resource() = 0;

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

        /// @brief 既定のクリアカラーを設定（Begin の設定でクリア色を渡さなかったときに使う）
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
        /// @brief RTVのクリア色（RGBA）
        float clearColor_[4] = {0.1f, 0.25f, 0.5f, 1.0f};
    };
}
