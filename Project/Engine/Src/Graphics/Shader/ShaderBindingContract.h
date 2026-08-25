#pragma once

#include "Graphics/RootSignature/RootSlot.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace CoreEngine
{
    class ShaderReflectionData;

    /// @brief 宣言側が期待するリソース種別
    enum class ShaderBindingType : uint8_t { CBV, SRV, UAV, Sampler };

    /// @brief そのリソースをどれだけ強く要求するか
    enum class BindingUsage : uint8_t {
        /// @brief シェーダーに必須。かつ毎ドロー必ず差す
        /// @note 差し忘れは ShaderBinder::ValidateBeforeDraw() が Draw 前に検出する
        Required,

        /// @brief シェーダーには必須だが、差すかどうかはフレームごとの判断
        /// @note Draw 前検査の対象外（RT シャドウマスク・空スペキュラ等）
        Conditional,

        /// @brief シェーダーに無くてもよい
        /// @note 未解決なら RootSlot{None} になり、ShaderBinder::Set() は何もしない
        Optional,
    };

    /// @brief C++ 側が宣言する「1 リソース分の契約」
    struct ShaderBindingDecl {
        const char*  name;
        ShaderBindingType  type;
        BindingUsage usage;
    };

    /// @brief 宣言表を解決した結果。添字は宣言表の並びと 1 対 1 に対応する
    /// @note 実行時に文字列を触らせないための箱。描画中は operator[] だけを使う。
    class BindingTable {
    public:
        /// @brief D3D12 のルートパラメータ上限。宣言数もこれを超えられない
        static constexpr size_t kMaxBindings = 64;

        BindingTable() = default;

        /// @brief 宣言表をリフレクション結果と突き合わせて解決する
        /// @param reflection      RootSignature 構築後のリフレクションデータ
        /// @param decls           宣言表（静的寿命であること。診断で名前を引くため保持する）
        /// @param count           宣言数
        /// @param shaderName      ログ用の識別名
        /// @param warnUndeclared  シェーダーにあるのに宣言が無いリソースを警告するか
        /// @throws std::runtime_error 契約違反（Required/Conditional が不在、種別違い）
        static BindingTable Resolve(
            const ShaderReflectionData& reflection,
            const ShaderBindingDecl* decls,
            size_t count,
            std::string shaderName,
            bool warnUndeclared = true);

        /// @brief 配列サイズを推論する版
        template <size_t N>
        static BindingTable Resolve(
            const ShaderReflectionData& reflection,
            const ShaderBindingDecl (&decls)[N],
            std::string shaderName,
            bool warnUndeclared = true)
        {
            static_assert(N <= kMaxBindings, "宣言数がルートパラメータ上限を超えています");
            return Resolve(reflection, decls, N, std::move(shaderName), warnUndeclared);
        }

        /// @brief 宣言表の添字で解決済みスロットを引く
        RootSlot operator[](size_t declIndex) const
        {
            return declIndex < count_ ? slots_[declIndex] : RootSlot{};
        }

        /// @brief Required 宣言のうち、実際にルートパラメータが割り当たったもののビット集合
        /// @details ShaderBinder::ValidateBeforeDraw() が boundMask_ と比べて取りこぼしを見る
        uint64_t RequiredMask() const { return requiredMask_; }

        size_t Count() const { return count_; }
        const std::string& ShaderName() const { return shaderName_; }

        /// @brief ルートパラメータ番号からリソース名を逆引きする（診断用。無ければ nullptr）
        const char* FindNameByRootParam(uint8_t rootParamIndex) const;

    private:
        std::array<RootSlot, kMaxBindings> slots_{};
        const ShaderBindingDecl* decls_ = nullptr;
        size_t   count_ = 0;
        uint64_t requiredMask_ = 0;
        std::string shaderName_;
    };
}
