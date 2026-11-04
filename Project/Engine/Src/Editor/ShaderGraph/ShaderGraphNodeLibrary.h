#pragma once

#ifdef CORE_EDITOR

#include "Editor/ShaderGraph/ShaderGraph.h"

#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace CoreEngine::Editor::ShaderGraph
{
    /// @brief つながっていない入力が使うもの
    enum class PinBinding : uint8_t
    {
        Value,       ///< ノードに書いた値
        UV,          ///< 描いている面の UV
        MainTexture, ///< 部品のテクスチャ（`gTexture`）
        Sampler,     ///< 線形・繰り返しのサンプラー
        SpriteRGB,   ///< マテリアルなしと同じ色の RGB
        SpriteAlpha, ///< マテリアルなしと同じ色の A
    };

    /// @brief ピン 1 つの定義
    struct PinDesc
    {
        std::string_view name;
        ValueType type = ValueType::Float;
        std::array<float, 4> fallback{ 0.0f, 0.0f, 0.0f, 0.0f }; ///< つながっていないときの値
        PinBinding binding = PinBinding::Value;
    };

    /// @brief ノードの設定 1 つ（選択肢か文字）
    struct SettingDesc
    {
        std::string_view name;
        std::string_view label;
        std::vector<std::string_view> choices; ///< 空なら文字を書く欄
        std::string_view fallback;
    };

    struct NodeDesc;

    /// @brief ノードの HLSL を作るときに渡すもの
    struct EmitArgs
    {
        const Graph& graph;
        const Node& node;
        const NodeDesc& desc;
        Target target = Target::Sprite;

        std::vector<std::string> in;      ///< 入力の式（ピンの幅へ直したもの）
        std::vector<ValueType> inType;    ///< 入力の種類（ピンの幅へ直した後）
        std::vector<bool> connected;      ///< 入力がつながっているか
        int dynamicWidth = 1;             ///< Dynamic のピンの幅

        std::vector<ValueType> outType;   ///< 出力の種類
        std::vector<std::string> outExpr; ///< 出力を指す式（ほかのノードが使う）

        std::string code;  ///< 足す文
        std::string error; ///< 作れなかった訳（空なら作れた）

        /// @brief 出力の変数の名前
        std::string VariableName(size_t output) const;

        /// @brief 出力を変数として宣言する（テクスチャとサンプラーは式をそのまま渡す）
        void Out(size_t output, std::string_view expression);

        /// @brief 設定の値（書いていなければ既定）
        std::string Setting(std::string_view name) const;
    };

    /// @brief ノード 1 種類の定義
    struct NodeDesc
    {
        std::string_view type;     ///< ファイルに書く名前
        std::string_view title;    ///< 表示名（Unity と同じ）
        std::string_view category; ///< 検索の木（`Math/Basic` など）
        std::string_view summary;  ///< 説明
        std::vector<PinDesc> inputs;
        std::vector<PinDesc> outputs;
        std::vector<SettingDesc> settings;
        bool creatable = true; ///< 検索から置けるか（出口は置けない）

        /// @brief HLSL を作る
        std::function<void(EmitArgs&)> emit;

        /// @brief 出力の種類を決める（省略すると、ピンの種類と Dynamic の幅から決める）
        std::function<void(const Graph&, const Node&, int dynamicWidth, std::vector<ValueType>&)> resolveOutputs;

        /// @brief 入力ピンの番号（無ければ -1）
        int FindInput(std::string_view name) const;

        /// @brief 出力ピンの番号（無ければ -1）
        int FindOutput(std::string_view name) const;
    };

    /// @brief 全ノードの定義（並びは検索の並び）
    const std::vector<NodeDesc>& AllNodes();

    /// @brief 種類の名前からノードの定義を引く（無ければ nullptr）
    const NodeDesc* FindNodeDesc(std::string_view type);

    /// @brief 幅から HLSL の型名（float〜float4）
    std::string TypeName(int width);

    /// @brief 種類から HLSL の型名
    std::string TypeName(ValueType type);

    /// @brief 浮動小数の書き方（必ず `1.0f` の形）
    std::string FormatFloat(float value);

    /// @brief 数の式（幅に合わせて float〜float4 の形にする）
    std::string Literal(const std::array<float, 4>& numbers, int width);

    /// @brief 式の幅を直す（広げるときは 1 個なら全部へ複製・それ以外は 0 と w=1 を足す）
    std::string Convert(std::string_view expression, int from, int to);

    /// @brief 数の種類の幅（数でなければ 0）
    int WidthOf(ValueType type);

    /// @brief 幅から数の種類
    ValueType VectorType(int width);
}

#endif // CORE_EDITOR
