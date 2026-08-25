#include "pch.h"
#include "ShaderBindingContract.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace CoreEngine
{
    namespace {

        const char* ToString(ShaderBindingType type)
        {
            switch (type) {
            case ShaderBindingType::CBV:     return "CBV";
            case ShaderBindingType::SRV:     return "SRV";
            case ShaderBindingType::UAV:     return "UAV";
            case ShaderBindingType::Sampler: return "Sampler";
            default:                   return "Unknown";
            }
        }

        const char* ToString(BindingUsage usage)
        {
            switch (usage) {
            case BindingUsage::Required:    return "Required";
            case BindingUsage::Conditional: return "Conditional";
            case BindingUsage::Optional:    return "Optional";
            default:                        return "Unknown";
            }
        }

        /// @brief リフレクション結果の中でその名前が実際に何として現れているか
        /// @return 見つからなければ nullopt 相当（false を返す）
        bool FindActualType(const ShaderReflectionData& reflection,
            const std::string& name, ShaderBindingType& outType)
        {
            const auto contains = [&name](const std::vector<ShaderResourceBinding>& list) {
                return std::any_of(list.begin(), list.end(),
                    [&name](const ShaderResourceBinding& b) { return b.name == name; });
            };

            if (contains(reflection.GetCBVBindings()))     { outType = ShaderBindingType::CBV;     return true; }
            if (contains(reflection.GetSRVBindings()))     { outType = ShaderBindingType::SRV;     return true; }
            if (contains(reflection.GetUAVBindings()))     { outType = ShaderBindingType::UAV;     return true; }
            if (contains(reflection.GetSamplerBindings())) { outType = ShaderBindingType::Sampler; return true; }
            return false;
        }

        /// @brief 宣言表にその名前があるか
        bool IsDeclared(const ShaderBindingDecl* decls, size_t count, const std::string& name)
        {
            for (size_t i = 0; i < count; ++i) {
                if (name == decls[i].name) {
                    return true;
                }
            }
            return false;
        }

    } // namespace

    const char* BindingTable::FindNameByRootParam(uint8_t rootParamIndex) const
    {
        for (size_t i = 0; i < count_; ++i) {
            if (slots_[i].IsValid() && slots_[i].index == rootParamIndex) {
                return decls_[i].name;
            }
        }
        return nullptr;
    }

    BindingTable BindingTable::Resolve(
        const ShaderReflectionData& reflection,
        const ShaderBindingDecl* decls,
        size_t count,
        std::string shaderName,
        bool warnUndeclared)
    {
        BindingTable table;
        table.decls_ = decls;
        table.count_ = std::min(count, kMaxBindings);
        table.shaderName_ = std::move(shaderName);

        // 契約違反はまとめて集めてから 1 度に投げる。1 件ずつ throw すると
        // 「直したら次が出る」を宣言数ぶん繰り返すことになる。
        std::vector<std::string> violations;

        for (size_t i = 0; i < table.count_; ++i) {
            const ShaderBindingDecl& decl = decls[i];
            const std::string name = decl.name;

            ShaderBindingType actualType{};
            const bool existsInShader = FindActualType(reflection, name, actualType);
            const RootSlot slot = reflection.GetRootSlot(name);

            // ---- 種別の食い違い ----
            // 宣言が CBV なのに実体が SRV なら、差し方も差すデータも間違っている
            if (existsInShader && actualType != decl.type) {
                std::ostringstream oss;
                oss << "'" << name << "' の種別が食い違っています（宣言=" << ToString(decl.type)
                    << " / シェーダー実体=" << ToString(actualType) << "）";
                violations.push_back(oss.str());
                continue;
            }

            // ---- 存在しない ----
            if (!slot.IsValid()) {
                if (decl.usage == BindingUsage::Optional) {
                    // 意図した省略。RootSlot{None} のままにして Set() を no-op にする
                    continue;
                }

                std::ostringstream oss;
                oss << "'" << name << "'（" << ToString(decl.type) << " / "
                    << ToString(decl.usage) << "）がシェーダーに見つかりません";
                if (existsInShader) {
                    // 宣言されてはいるがルートパラメータが割り当たっていない
                    // （StaticSampler、あるいは未使用でコンパイラに落とされた）
                    oss << "（シェーダーには宣言があるがルートパラメータが割り当たっていません。"
                        "StaticSampler か、未使用で削除された可能性があります）";
                }
                else {
                    oss << "（改名・削除されていないか確認してください）";
                }
                violations.push_back(oss.str());
                continue;
            }

            table.slots_[i] = slot;

            if (decl.usage == BindingUsage::Required) {
                table.requiredMask_ |= (1ull << (slot.index & 63));
            }
        }

        // ---- シェーダーにあるのに宣言が無いもの ----
        // 誰も差さないまま描画される（＝前フレームの残り物を読む）ので、警告を出す。
        // カスタムシェーダーのようにエンジンと分担している場合は対象外。
        if (warnUndeclared) {
            const auto checkList = [&](const std::vector<ShaderResourceBinding>& list) {
                for (const auto& binding : list) {
                    if (!reflection.GetRootSlot(binding.name).IsValid()) {
                        continue;  // ルートパラメータを持たない（StaticSampler 等）
                    }
                    if (IsDeclared(decls, table.count_, binding.name)) {
                        continue;
                    }
                    Logger::GetInstance().Logf(LogLevel::WARNING, LogCategory::Shader,
                        "バインド宣言がありません: name={} shader={} "
                        "（誰も差さないので前のドローの descriptor が読まれます）",
                        binding.name, table.shaderName_);
                }
            };
            checkList(reflection.GetCBVBindings());
            checkList(reflection.GetSRVBindings());
            checkList(reflection.GetUAVBindings());
        }

        if (!violations.empty()) {
            std::ostringstream oss;
            oss << "シェーダーのバインド契約違反 (" << table.shaderName_ << "): "
                << violations.size() << " 件";
            for (const auto& v : violations) {
                oss << "\n  - " << v;
            }
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Shader, "{}", oss.str());
            throw std::runtime_error(oss.str());
        }

        return table;
    }
}
