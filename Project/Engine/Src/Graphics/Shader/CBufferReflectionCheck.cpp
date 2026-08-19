#include "pch.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"

#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <vector>

namespace CoreEngine::Cb {

    namespace {

        /// @brief 登録済み対応表。static 初期化順の問題を避けるため関数内 static で持つ
        std::vector<HlslBinding>& Bindings() {
            static std::vector<HlslBinding> bindings;
            return bindings;
        }

        /// @brief HLSL 側から読み出した変数 1 個分
        struct HlslVariable {
            std::string name;
            UINT        offset = 0;
        };

        /// @brief cbuffer の変数一覧を読み出す
        /// @details 宣言形式が 2 通りあるので両方を吸収する。
        ///          - cbuffer Foo { float a; float b; };        → 変数が直接並ぶ
        ///          - ConstantBuffer<Foo> gFoo;                 → 変数 1 個（構造体）に包まれる
        bool CollectVariables(
            ID3D12ShaderReflectionConstantBuffer* cbuffer,
            const D3D12_SHADER_BUFFER_DESC& bufferDesc,
            std::vector<HlslVariable>& out) {

            out.clear();

            // ConstantBuffer<T> 形式かどうかを見る
            if (bufferDesc.Variables == 1) {
                ID3D12ShaderReflectionVariable* variable = cbuffer->GetVariableByIndex(0);
                D3D12_SHADER_VARIABLE_DESC variableDesc{};
                if (variable && SUCCEEDED(variable->GetDesc(&variableDesc))) {
                    ID3D12ShaderReflectionType* type = variable->GetType();
                    D3D12_SHADER_TYPE_DESC typeDesc{};
                    if (type && SUCCEEDED(type->GetDesc(&typeDesc)) &&
                        typeDesc.Class == D3D_SVC_STRUCT && typeDesc.Members > 0) {

                        out.reserve(typeDesc.Members);
                        for (UINT m = 0; m < typeDesc.Members; ++m) {
                            ID3D12ShaderReflectionType* memberType = type->GetMemberTypeByIndex(m);
                            D3D12_SHADER_TYPE_DESC memberDesc{};
                            if (!memberType || FAILED(memberType->GetDesc(&memberDesc))) {
                                return false;
                            }
                            const char* memberName = type->GetMemberTypeName(m);
                            out.push_back({
                                memberName ? memberName : "",
                                variableDesc.StartOffset + memberDesc.Offset,
                            });
                        }
                        return true;
                    }
                }
            }

            // 素の cbuffer 形式
            out.reserve(bufferDesc.Variables);
            for (UINT i = 0; i < bufferDesc.Variables; ++i) {
                ID3D12ShaderReflectionVariable* variable = cbuffer->GetVariableByIndex(i);
                D3D12_SHADER_VARIABLE_DESC variableDesc{};
                if (!variable || FAILED(variable->GetDesc(&variableDesc))) {
                    return false;
                }
                out.push_back({
                    variableDesc.Name ? variableDesc.Name : "",
                    variableDesc.StartOffset,
                });
            }
            return true;
        }

        void LogMismatch(const std::string& message) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Shader, "{}", message);
        }

    } // namespace

    Registrar::Registrar(const HlslBinding& binding) {
        auto& bindings = Bindings();

        // 同じヘッダーが複数の翻訳単位に含まれると、同じ登録が何度も来る。
        // 名前と構造体が同一なら重複なので捨てる（別構造体が同じ cbuffer 名を
        // 主張しているケースは実行時の照合側で検出する。ここは静的初期化中で
        // Logger がまだ立ち上がっていない可能性があるためログを出さない）。
        const bool duplicated = std::any_of(bindings.begin(), bindings.end(),
            [&](const HlslBinding& registered) {
                return std::string(registered.hlslName) == binding.hlslName
                    && std::string(registered.cppName) == binding.cppName;
            });

        if (duplicated) {
            return;
        }

        bindings.push_back(binding);
    }

    size_t RegisteredBindingCount() {
        return Bindings().size();
    }

    bool CheckAgainstReflection(
        ID3D12ShaderReflectionConstantBuffer* cbuffer,
        const D3D12_SHADER_BUFFER_DESC& bufferDesc,
        const std::string& shaderName) {

#if !CB_REFLECTION_CHECK_ENABLED
        (void)cbuffer; (void)bufferDesc; (void)shaderName;
        return true;
#else
        if (!cbuffer || !bufferDesc.Name) {
            return true;
        }

        const auto& bindings = Bindings();
        const auto matches = [&](const HlslBinding& binding) {
            return std::string(binding.hlslName) == bufferDesc.Name;
        };

        const auto it = std::find_if(bindings.begin(), bindings.end(), matches);

        // 未登録の cbuffer は対象外（段階的に登録を増やせるようにするため）
        if (it == bindings.end()) {
            return true;
        }

        // 同じ cbuffer 名に別の構造体が登録されている場合、どちらを当てるか決められない。
        // 名前が被っているという事実そのものが問題なので報告して照合は行わない
        // （例: 複数のポストエフェクトが別レイアウトの cbuffer に同じ名前を付けている）
        if (std::count_if(bindings.begin(), bindings.end(), matches) > 1) {
            LogMismatch(
                "[CBufferLayout] cbuffer '" + std::string(bufferDesc.Name) +
                "' に複数の C++ 構造体が登録されているため照合できません。"
                "HLSL 側の cbuffer 名を一意にするか、登録を外してください");
            return false;
        }

        const HlslBinding& binding = *it;

        const std::string where =
            "[CBufferLayout] " + shaderName + " の cbuffer '" + bufferDesc.Name +
            "'（C++ 側 " + binding.cppName + "）: ";

        std::vector<HlslVariable> variables;
        if (!CollectVariables(cbuffer, bufferDesc, variables)) {
            LogMismatch(where + "リフレクション情報を読み出せませんでした");
            return false;
        }

        bool ok = true;

        // 1) 全体サイズ
        //    HLSL 側でメンバを増減すると必ずここか 2) に出る
        const size_t expectedTotal =
            ExpectedTotalSize(binding.fields, binding.fieldCount, binding.tight);
        if (bufferDesc.Size != expectedTotal) {
            LogMismatch(where + "全体サイズが違います（HLSL " + std::to_string(bufferDesc.Size) +
                " バイト / C++ " + std::to_string(expectedTotal) + " バイト）");
            ok = false;
        }

        // 2) HLSL の各メンバの開始位置が、C++ 側のいずれかのフィールドの開始位置と一致するか
        //    1 対 1 で突き合わせないのは、HLSL 側がスカラーをまとめてベクトルで宣言しうるため
        //    （C++ の float x; float y; が HLSL では float2 の 1 個になる）。
        //    メンバの挿入・並び替えは開始位置が境界と一致しなくなるので、この方式でも捕まる。
        std::vector<size_t> boundaries;
        boundaries.reserve(binding.fieldCount);
        for (size_t i = 0; i < binding.fieldCount; ++i) {
            boundaries.push_back(
                ExpectedOffsetOf(binding.fields, binding.fieldCount, i, binding.tight));
        }

        for (const HlslVariable& variable : variables) {
            if (std::find(boundaries.begin(), boundaries.end(), variable.offset) != boundaries.end()) {
                continue;
            }

            // どの C++ フィールドの内側に落ちたのかを示す
            std::string inside = "（対応する C++ フィールドが見つかりません）";
            for (size_t i = 0; i < binding.fieldCount; ++i) {
                const size_t start = boundaries[i];
                const size_t end = start + EffectiveType(binding.fields[i], binding.tight).hlslSize;
                if (variable.offset > start && variable.offset < end) {
                    inside = "（C++ 側では '" + std::string(binding.fields[i].name) +
                        "' の途中 " + std::to_string(start) + "〜" + std::to_string(end) + " に当たります）";
                    break;
                }
            }

            LogMismatch(where + "HLSL のメンバ '" + variable.name + "' がオフセット " +
                std::to_string(variable.offset) + " から始まりますが、"
                "C++ 側にその位置から始まるフィールドがありません" + inside);
            ok = false;
            break; // 以降は連鎖するだけなので最初の 1 件で止める
        }

        assert(ok && "cbuffer のレイアウトが HLSL と C++ で食い違っています（ログを確認してください）");
        return ok;
#endif
    }

} // namespace CoreEngine::Cb
