#pragma once

//========================================================================================
// RootSlot.h
//
// 「ルートパラメータ番号」と「その差し方」を 1 つの値にまとめた型。
//
// 従来は名前からルートパラメータ番号（int）だけを引いていたため、
// SetGraphicsRootDescriptorTable / SetGraphicsRootConstantBufferView / ... の
// どれを呼ぶかは呼び出し側が暗記しているしかなかった。RootSignatureBuilder は
// 戦略（BindingStrategy）を決めた張本人なのに、その情報を捨てて番号だけ返していた。
//
// 種別を番号に同梱して返せば、Set* の選択はエンジン側（ShaderBinder）で行える。
// 「戦略を変えたら全呼び出し側が無言で不正になる」という事故が構造的に起きなくなる。
//
// 詳細: Docs/Engine/Graphics/Shader/ShaderBinding_Design_Review.md §4.2
//========================================================================================

#include <cstdint>

namespace CoreEngine
{
    /// @brief ルートパラメータの受け取り方
    /// @details RootSignatureBuilder が BindingStrategy から決めた結果がそのまま入る
    enum class RootSlotKind : uint8_t {
        None,            ///< 未解決（そのリソースはシェーダーに存在しない）
        RootCBV,         ///< Set{Graphics|Compute}RootConstantBufferView
        RootSRV,         ///< Set{Graphics|Compute}RootShaderResourceView
        RootUAV,         ///< Set{Graphics|Compute}RootUnorderedAccessView
        DescriptorTable, ///< Set{Graphics|Compute}RootDescriptorTable
        RootConstants,   ///< Set{Graphics|Compute}Root32BitConstants
    };

    /// @brief ルートパラメータ 1 個への参照
    /// @note 番号だけを裸で持ち回らないこと。kind が無いと Set* を選べない。
    struct RootSlot {
        /// @brief ルートパラメータ番号。D3D12 の上限は 64 なので uint8_t で足りる
        uint8_t      index = 0;
        RootSlotKind kind = RootSlotKind::None;

        /// @brief シェーダーに存在し、ルートパラメータが割り当てられているか
        bool IsValid() const { return kind != RootSlotKind::None; }

        friend bool operator==(const RootSlot& a, const RootSlot& b) {
            return a.index == b.index && a.kind == b.kind;
        }
        friend bool operator!=(const RootSlot& a, const RootSlot& b) { return !(a == b); }
    };

    /// @brief ログ用の種別名
    inline const char* ToString(RootSlotKind kind)
    {
        switch (kind) {
        case RootSlotKind::RootCBV:         return "RootCBV";
        case RootSlotKind::RootSRV:         return "RootSRV";
        case RootSlotKind::RootUAV:         return "RootUAV";
        case RootSlotKind::DescriptorTable: return "DescriptorTable";
        case RootSlotKind::RootConstants:   return "RootConstants";
        default:                            return "None";
        }
    }
}
