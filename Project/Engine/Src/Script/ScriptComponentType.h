#pragma once

#include "Reflection/TypeDescriptor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

class asIScriptFunction;
class asITypeInfo;
class CScriptBuilder;

namespace CoreEngine
{
    class ScriptHost;

    namespace Script
    {
        struct MetadataAttribute;
    }

    /// @brief ScriptComponent を継いだスクリプトのクラス 1 つ分のコンポーネントの型
    /// @details クラスの公開メンバ変数とその属性から記述子を組み立て、ライフサイクルの関数を控える。
    class ScriptComponentType
    {
    public:
        /// @brief スクリプトのクラスに書けるライフサイクルの関数
        enum class Method : std::size_t
        {
            Awake,
            Start,
            Update,
            LateUpdate,
            OnDestroy,
            OnScriptReloaded,
            Count,
        };

        /// @param host このクラスをコンパイルした実行環境
        /// @param type 組み立てるクラス
        /// @param base スクリプトの ScriptComponent クラス
        /// @param builder このクラスをコンパイルした scriptbuilder（属性を読む）
        ScriptComponentType(ScriptHost& host, asITypeInfo* type, asITypeInfo* base, CScriptBuilder& builder);
        ~ScriptComponentType();

        ScriptComponentType(const ScriptComponentType&) = delete;
        ScriptComponentType& operator=(const ScriptComponentType&) = delete;

        /// @brief 型名（クラス名）
        const std::string& GetName() const { return name_; }

        /// @brief インスペクタでの名前（`[DisplayName]` が無ければ型名）
        const std::string& GetDisplayName() const { return displayName_; }

        ScriptHost& GetHost() const { return host_; }
        asITypeInfo* GetTypeInfo() const { return typeInfo_; }
        const Reflection::TypeDescriptor& GetDescriptor() const { return descriptor_; }

        /// @brief ライフサイクルの関数
        /// @return このクラスが書いていなければ nullptr（ScriptComponent の空の関数のままのとき）
        asIScriptFunction* GetMethod(Method method) const
        {
            return methods_[static_cast<std::size_t>(method)];
        }

        /// @brief ライフサイクルの関数の名前（ログ用）
        static const char* GetMethodName(Method method);

        /// @brief 基底クラスの `owner_`（持ち主のハンドル）のプロパティの添え字
        /// @return 見つからなければ -1
        int GetOwnerPropertyIndex() const { return ownerPropertyIndex_; }

        /// @brief このフレームの Update / LateUpdate にかかった時間を足す
        /// @param countInstance Update の呼び出しなら true（実体の数として数える）
        void AddFrameCost(double milliseconds, bool countInstance) const
        {
            frameCostMs_ += milliseconds;
            frameInstances_ += countInstance ? 1u : 0u;
        }

        /// @brief このフレームの集計を「直前のフレーム」へ移し、次のフレームのために空にする
        void RollFrameCost() const
        {
            lastCostMs_ = frameCostMs_;
            lastInstances_ = frameInstances_;
            frameCostMs_ = 0.0;
            frameInstances_ = 0;
        }

        /// @brief 直前のフレームの Update / LateUpdate にかかった時間（ミリ秒）
        double GetLastFrameCostMs() const { return lastCostMs_; }

        /// @brief 直前のフレームに Update を呼んだ実体の数
        std::uint32_t GetLastFrameInstances() const { return lastInstances_; }

    private:
        void ReadClassAttributes(CScriptBuilder& builder);
        void BuildProperties(asITypeInfo* base, CScriptBuilder& builder);
        void FindMethods(asITypeInfo* base);
        void FindOwnerProperty();

        /// @brief 属性 1 つをプロパティへ当てる
        /// @return 当てられなければ false（`error` に理由）
        bool ApplyPropertyAttribute(Reflection::PropertyDescriptor& property,
                                    const Script::MetadataAttribute& attribute, std::string& error);

        /// @brief 記述子が指す文字列を控えて、そのアドレスを返す
        const char* StoreText(std::string text);

        ScriptHost& host_;
        asITypeInfo* typeInfo_ = nullptr;
        std::string name_;
        std::string displayName_;
        Reflection::TypeDescriptor descriptor_;
        std::array<asIScriptFunction*, static_cast<std::size_t>(Method::Count)> methods_{};
        int ownerPropertyIndex_ = -1;

        /// 記述子の文字列の置き場（足しても要素のアドレスが変わらない入れ物）
        std::deque<std::string> texts_;

        /// 実行時間の集計（このフレームの分と、直前のフレームの分）
        mutable double frameCostMs_ = 0.0;
        mutable std::uint32_t frameInstances_ = 0;
        mutable double lastCostMs_ = 0.0;
        mutable std::uint32_t lastInstances_ = 0;
    };
}
