#include "pch.h"
#include "Script/ScriptPredefined.h"

#include "Utility/Logger/Logger.h"

#include <angelscript.h>

#include <format>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>
#include <vector>

namespace CoreEngine::Script
{
    namespace
    {
        constexpr const char* kNewLine = "\r\n";
        constexpr const char* kHeader = "// CoreEngine が起動時に、スクリプトへ登録した型と関数から書き出す補完用の宣言（手で書き換えない）";

        /// @brief 宣言のまとまりを、名前空間ごとに出てきた順でためる
        class NamespacedBlocks
        {
        public:
            void Add(const char* nameSpace, std::string block)
            {
                const std::string key = nameSpace ? nameSpace : "";
                for (auto& [name, text] : groups_) {
                    if (name == key) {
                        text += block;
                        return;
                    }
                }
                groups_.emplace_back(key, std::move(block));
            }

            void AppendTo(std::string& out) const
            {
                for (const auto& [name, text] : groups_) {
                    if (name.empty()) {
                        out += text;
                    } else {
                        out += std::format("namespace {} {{{}{}}}{}", name, kNewLine, text, kNewLine);
                    }
                }
            }

        private:
            std::vector<std::pair<std::string, std::string>> groups_;
        };

        std::string ToUtf8(const std::filesystem::path& path)
        {
            const std::u8string text = path.generic_u8string();
            return std::string(text.begin(), text.end());
        }

        /// @brief スクリプトの構文で書ける宣言か（`?&` の型・`&out` の `= void`・`...` はアプリの登録でしか書けない）
        bool IsScriptSyntax(const std::string& declaration)
        {
            return declaration.find("?&") == std::string::npos
                && declaration.find("= void") == std::string::npos
                && declaration.find("...") == std::string::npos;
        }

        /// @brief 関数の宣言（プロパティの読み書きの関数には `property` を付ける）
        std::string DeclarationOf(const asIScriptFunction& function)
        {
            std::string declaration = function.GetDeclaration(false, false, true);
            if (function.IsProperty()) {
                declaration += " property";
            }
            return declaration;
        }

        /// @brief テンプレートに型を当てはめて作られた型か（`array<int>` など。テンプレートそのものは部分型が `T` の印を持つ）
        bool IsTemplateInstance(const asITypeInfo& type)
        {
            if ((type.GetFlags() & asOBJ_TEMPLATE) == 0) {
                return false;
            }
            for (asUINT i = 0; i < type.GetSubTypeCount(); ++i) {
                const asITypeInfo* const subType = type.GetSubType(i);
                if (!subType || (subType->GetFlags() & asOBJ_TEMPLATE_SUBTYPE) == 0) {
                    return true;
                }
            }
            return false;
        }

        /// @brief 型名（テンプレートなら `<T>` まで）
        std::string TypeNameOf(const asITypeInfo& type)
        {
            std::string name = type.GetName();
            const asUINT count = type.GetSubTypeCount();
            if (count == 0) {
                return name;
            }
            name += "<";
            for (asUINT i = 0; i < count; ++i) {
                if (i > 0) {
                    name += ", ";
                }
                const asITypeInfo* const subType = type.GetSubType(i);
                name += subType ? subType->GetName() : "T";
            }
            name += ">";
            return name;
        }

        void AppendEnums(asIScriptEngine& engine, std::string& out)
        {
            NamespacedBlocks blocks;
            for (asUINT i = 0; i < engine.GetEnumCount(); ++i) {
                const asITypeInfo* const type = engine.GetEnumByIndex(i);
                if (!type) {
                    continue;
                }
                std::string block = std::format("enum {} {{{}", type->GetName(), kNewLine);
                const asUINT count = type->GetEnumValueCount();
                for (asUINT j = 0; j < count; ++j) {
                    int value = 0;
                    const char* const name = type->GetEnumValueByIndex(j, &value);
                    block += std::format("\t{} = {}{}{}", name ? name : "", value, j + 1 < count ? "," : "", kNewLine);
                }
                block += std::string("}") + kNewLine;
                blocks.Add(type->GetNamespace(), std::move(block));
            }
            blocks.AppendTo(out);
        }

        void AppendFuncdefs(asIScriptEngine& engine, std::string& out)
        {
            NamespacedBlocks blocks;
            for (asUINT i = 0; i < engine.GetFuncdefCount(); ++i) {
                const asITypeInfo* const type = engine.GetFuncdefByIndex(i);
                if (!type || type->GetParentType()) {
                    continue;
                }
                const asIScriptFunction* const signature = type->GetFuncdefSignature();
                if (!signature) {
                    continue;
                }
                const std::string declaration = signature->GetDeclaration(false, false, true);
                if (IsScriptSyntax(declaration)) {
                    blocks.Add(type->GetNamespace(), std::format("funcdef {};{}", declaration, kNewLine));
                }
            }
            blocks.AppendTo(out);
        }

        /// @brief 参照型の生成関数の宣言を、コンストラクタの宣言の形（`型名(引数)`）にする
        std::string FactoryDeclarationOf(const asITypeInfo& type, const asIScriptFunction& factory)
        {
            const std::string declaration = factory.GetDeclaration(false, false, true);
            const std::size_t open = declaration.find('(');
            return open == std::string::npos ? std::string() : TypeNameOf(type) + declaration.substr(open);
        }

        void AppendClasses(asIScriptEngine& engine, std::string& out)
        {
            NamespacedBlocks blocks;
            for (asUINT i = 0; i < engine.GetObjectTypeCount(); ++i) {
                const asITypeInfo* const type = engine.GetObjectTypeByIndex(i);
                if (!type || IsTemplateInstance(*type)) {
                    continue;
                }

                std::string block = std::format("class {} {{{}", TypeNameOf(*type), kNewLine);
                const auto appendMember = [&block](const std::string& declaration) {
                    if (IsScriptSyntax(declaration)) {
                        block += std::format("\t{};{}", declaration, kNewLine);
                    }
                };
                for (asUINT j = 0; j < type->GetBehaviourCount(); ++j) {
                    asEBehaviours behaviour = asBEHAVE_DESTRUCT;
                    const asIScriptFunction* const function = type->GetBehaviourByIndex(j, &behaviour);
                    if (function && behaviour == asBEHAVE_CONSTRUCT) {
                        appendMember(DeclarationOf(*function));
                    }
                }
                if ((type->GetFlags() & asOBJ_TEMPLATE) == 0) {
                    for (asUINT j = 0; j < type->GetFactoryCount(); ++j) {
                        if (const asIScriptFunction* const factory = type->GetFactoryByIndex(j)) {
                            appendMember(FactoryDeclarationOf(*type, *factory));
                        }
                    }
                }
                for (asUINT j = 0; j < type->GetMethodCount(); ++j) {
                    if (const asIScriptFunction* const method = type->GetMethodByIndex(j, false)) {
                        appendMember(DeclarationOf(*method));
                    }
                }
                for (asUINT j = 0; j < type->GetPropertyCount(); ++j) {
                    if (const char* const property = type->GetPropertyDeclaration(j, false)) {
                        appendMember(property);
                    }
                }
                for (asUINT j = 0; j < type->GetChildFuncdefCount(); ++j) {
                    const asITypeInfo* const child = type->GetChildFuncdef(j);
                    const asIScriptFunction* const signature = child ? child->GetFuncdefSignature() : nullptr;
                    if (signature) {
                        appendMember(std::string("funcdef ") + signature->GetDeclaration(false, false, true));
                    }
                }
                block += std::string("}") + kNewLine;
                blocks.Add(type->GetNamespace(), std::move(block));
            }
            blocks.AppendTo(out);
        }

        void AppendFunctions(asIScriptEngine& engine, std::string& out)
        {
            NamespacedBlocks blocks;
            for (asUINT i = 0; i < engine.GetGlobalFunctionCount(); ++i) {
                const asIScriptFunction* const function = engine.GetGlobalFunctionByIndex(i);
                if (!function) {
                    continue;
                }
                const std::string declaration = DeclarationOf(*function);
                if (IsScriptSyntax(declaration)) {
                    blocks.Add(function->GetNamespace(), std::format("{};{}", declaration, kNewLine));
                }
            }
            blocks.AppendTo(out);
        }

        void AppendGlobalProperties(asIScriptEngine& engine, std::string& out)
        {
            NamespacedBlocks blocks;
            for (asUINT i = 0; i < engine.GetGlobalPropertyCount(); ++i) {
                const char* name = nullptr;
                const char* nameSpace = nullptr;
                int typeId = 0;
                bool isConst = false;
                if (engine.GetGlobalPropertyByIndex(i, &name, &nameSpace, &typeId, &isConst) < 0 || !name) {
                    continue;
                }
                const char* const typeName = engine.GetTypeDeclaration(typeId, true);
                if (!typeName) {
                    continue;
                }
                blocks.Add(nameSpace, std::format("{}{} {};{}", isConst ? "const " : "", typeName, name, kNewLine));
            }
            blocks.AppendTo(out);
        }

        void AppendTypedefs(asIScriptEngine& engine, std::string& out)
        {
            NamespacedBlocks blocks;
            for (asUINT i = 0; i < engine.GetTypedefCount(); ++i) {
                const asITypeInfo* const type = engine.GetTypedefByIndex(i);
                if (!type) {
                    continue;
                }
                const char* const aliased = engine.GetTypeDeclaration(type->GetTypedefTypeId(), true);
                if (!aliased) {
                    continue;
                }
                blocks.Add(type->GetNamespace(), std::format("typedef {} {};{}", aliased, type->GetName(), kNewLine));
            }
            blocks.AppendTo(out);
        }
    }

    std::string BuildScriptPredefined(asIScriptEngine& engine)
    {
        std::string out = std::string(kHeader) + kNewLine;
        AppendEnums(engine, out);
        AppendFuncdefs(engine, out);
        AppendClasses(engine, out);
        AppendFunctions(engine, out);
        AppendGlobalProperties(engine, out);
        AppendTypedefs(engine, out);
        return out;
    }

    bool WriteScriptPredefined(asIScriptEngine& engine, const std::filesystem::path& file)
    {
        Logger& logger = Logger::GetInstance();
        const std::string text = BuildScriptPredefined(engine);

        std::error_code ec;
        if (!std::filesystem::is_directory(file.parent_path(), ec)) {
            logger.Logf(LogLevel::Warn, LogCategory::Script,
                "補完用の宣言の書き出し先のフォルダがありません: {}", ToUtf8(file.parent_path()));
            return false;
        }

        if (std::ifstream current(file, std::ios::binary); current) {
            const std::string existing{ std::istreambuf_iterator<char>(current), std::istreambuf_iterator<char>() };
            if (existing == text) {
                return true;
            }
        }

        std::ofstream stream(file, std::ios::binary | std::ios::trunc);
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        stream.close();
        if (!stream) {
            logger.Logf(LogLevel::Error, LogCategory::Script, "補完用の宣言を書き出せませんでした: {}", ToUtf8(file));
            return false;
        }
        logger.Logf(LogLevel::Info, LogCategory::Script, "補完用の宣言を書き出しました: {}", ToUtf8(file));
        return true;
    }
}
