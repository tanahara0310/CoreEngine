#pragma once

//========================================================================================
// ShaderBinder.h
//
// ルートバインドの入口を 1 本に集約する薄いラッパ。
//
//   ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Graphics);
//   binder.Set(slotCamera,  cameraCbAddress);   // RootCBV なら SetGraphicsRootConstantBufferView
//   binder.Set(slotTexture, baseColorHandle);   // DescriptorTable なら SetGraphicsRootDescriptorTable
//
// 従来との違いは 3 点。
//   1. Graphics / Compute の Set* 書き分けが呼び出し側から消える
//   2. RootSlot の種別と引数の型が食い違ったら assert で即死する（従来は GPU が落ちるまで不明）
//   3. 差したルートパラメータをビットで覚えるので、Draw 直前に取りこぼしを検出できる
//
// 詳細: Docs/Engine/Graphics/Shader/ShaderBinding_Design_Review.md §4.3
//========================================================================================

#include "Graphics/RootSignature/RootSlot.h"

#include <d3d12.h>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace CoreEngine
{
    class BindingTable;

    /// @brief 1 パス分のルートバインドを引き受ける
    /// @note コマンドリストを所有しない。パスのスコープで値として作って捨てる想定。
    class ShaderBinder {
    public:
        enum class Pipeline : uint8_t { Graphics, Compute };

        ShaderBinder(ID3D12GraphicsCommandList* cmdList, Pipeline pipeline)
            : cmdList_(cmdList), pipeline_(pipeline) {}

        /// @brief ディスクリプタテーブルを差す
        /// @note slot が DescriptorTable 以外なら assert。無効スロット（Optional な未使用リソース）は何もしない。
        void Set(RootSlot slot, D3D12_GPU_DESCRIPTOR_HANDLE table);

        /// @brief ルートディスクリプタ（CBV/SRV/UAV）を差す
        /// @note slot の種別に応じて ConstantBufferView / ShaderResourceView / UnorderedAccessView を選ぶ
        void Set(RootSlot slot, D3D12_GPU_VIRTUAL_ADDRESS address);

        /// @brief ルート定数を差す
        /// @param value 4 バイト境界に揃った POD。sizeof が 4 の倍数であることを要求する
        template <class T>
        void SetConstants(RootSlot slot, const T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>, "ルート定数は trivially copyable であること");
            static_assert(sizeof(T) % 4 == 0, "ルート定数は 4 バイトの倍数であること");
            SetConstantsRaw(slot, &value, sizeof(T) / 4);
        }

        /// @brief Draw / Dispatch の直前に呼ぶ。Required 宣言の差し忘れを検出する
        /// @details 「シェーダーは要求しているのに誰も差さなかった」を、GPU が前フレームの
        ///          descriptor を読んで絵が壊れる前に捕まえる。検出時は名前つきで [error] ログ。
        /// @note Release（CB_REFLECTION_CHECK_ENABLED が 0）では何もしない
        void ValidateBeforeDraw(const BindingTable& table) const;

        /// @brief このバインダーで差したルートパラメータのビット集合
        /// @details bit N = ルートパラメータ N を差した
        uint64_t GetBoundMask() const { return boundMask_; }

        /// @brief 指定スロットを差したか
        bool IsBound(RootSlot slot) const
        {
            return slot.IsValid() && (boundMask_ & Bit(slot.index)) != 0;
        }

    private:
        static uint64_t Bit(uint8_t index) { return 1ull << (index & 63); }

        void SetConstantsRaw(RootSlot slot, const void* data, uint32_t num32BitValues);

        ID3D12GraphicsCommandList* cmdList_ = nullptr;
        Pipeline pipeline_ = Pipeline::Graphics;

        /// @brief 差したルートパラメータのビット（D3D12 のルートパラメータ上限 64 と一致）
        uint64_t boundMask_ = 0;
    };
}
