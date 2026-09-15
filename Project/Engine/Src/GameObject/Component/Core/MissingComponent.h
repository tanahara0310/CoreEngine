#pragma once

#include "GameObject/Component/Core/IComponent.h"

#include <cstdint>
#include <string>

namespace CoreEngine
{
    /// @brief 型が見つからないコンポーネントの保存データを、そのまま持ち続ける置き場
    /// @details シーン JSON やプレハブにある型が、C++ にもスクリプトにも無いとき（スクリプトのコンパイルに
    ///          失敗したときなど）に付く。保存すると読み込んだときの `parameters` と版をそのまま書き戻すので、
    ///          型が戻れば次の読み込みで元のコンポーネントとして読める。
    class MissingComponent final : public IComponent
    {
    public:
        /// @param typeName 保存データにある型名
        explicit MissingComponent(std::string typeName);

        const char* GetTypeName() const override { return typeName_.c_str(); }

        json OnSerialize() const override { return parameters_; }
        void OnDeserialize(const json& j) override { parameters_ = j; }

        /// @brief 保存データにあった版を覚える（ComponentHost が読み込むときに渡す）
        void SetSavedVersion(uint32_t version) { savedVersion_ = version; }

        /// @brief 保存データにあった版（ComponentHost が保存するときに書き戻す）
        uint32_t GetSavedVersion() const { return savedVersion_; }

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return inspectorName_.c_str(); }
        bool DrawInspector() override;
#endif

    private:
        std::string typeName_;
        json parameters_ = json::object();
        uint32_t savedVersion_ = 1;
#ifdef USE_IMGUI
        std::string inspectorName_;
#endif
    };
}
