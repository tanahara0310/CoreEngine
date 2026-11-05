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
        Value,        ///< ノードに書いた値
        UV,           ///< 描いている面の UV（ポストエフェクトは画面の UV）
        MainTexture,  ///< 部品のテクスチャ（ポストエフェクトは画面。どちらも `gTexture`）
        Sampler,      ///< 線形・繰り返しのサンプラー
        DefaultRGB,   ///< 既定の色の RGB（スプライトはマテリアルなしと同じ色・ポストエフェクトはそのままの画面・3D はモデルの色）
        DefaultAlpha, ///< 既定の色の A
        ModelValue,   ///< 3D の出口: モデルの今の値（何も代入しない）
        SourceVertex, ///< 3D の頂点の出口: 元の頂点（何も代入しない）
        WorldNormal,  ///< 3D: ワールドの法線
        ViewDirection, ///< 3D の Fragment: 物からカメラへの向き
    };

    /// @brief 目標ごとの印（`NodeDesc::targets` に重ねる）
    constexpr uint8_t TargetBit(Target target)
    {
        return static_cast<uint8_t>(1u << static_cast<uint8_t>(target));
    }

    /// @brief 全部の目標
    inline constexpr uint8_t kAllTargets = TargetBit(Target::Sprite) | TargetBit(Target::PostEffect)
        | TargetBit(Target::Lit) | TargetBit(Target::Unlit);

    /// @brief 3D の目標
    inline constexpr uint8_t k3DTargets = TargetBit(Target::Lit) | TargetBit(Target::Unlit);

    /// @brief 3D のノードが働く段（`NodeDesc::stages` に重ねる）
    inline constexpr uint8_t kStageVertex = 1;   ///< 頂点の関数（SurfaceVertex）
    inline constexpr uint8_t kStageFragment = 2; ///< 画素の関数（Surface・スプライトとポストエフェクトは全部これ）
    inline constexpr uint8_t kAllStages = kStageVertex | kStageFragment;

    /// @brief 段の表示名
    const char* StageName(uint8_t stage);

    /// @brief Custom Function のノードの種類
    inline constexpr std::string_view kCustomFunctionType = "CustomFunction";

    /// @brief Custom Function のピン 1 つ（`型 名前`）
    struct FunctionPin
    {
        ValueType type = ValueType::Float;
        std::string name;
    };

    /// @brief Custom Function の入力・出力の数の上限（それぞれ）
    inline constexpr size_t kMaxFunctionPins = 16;

    /// @brief ピン 1 つの定義
    struct PinDesc
    {
        std::string_view name;
        ValueType type = ValueType::Float;
        std::array<float, 4> fallback{ 0.0f, 0.0f, 0.0f, 0.0f }; ///< つながっていないときの値
        PinBinding binding = PinBinding::Value;
        uint8_t targets = kAllTargets; ///< ピンを出す目標（出口の欄を目標で変える）

        /// @brief この目標で使うピンか
        bool Supports(Target target) const { return (targets & TargetBit(target)) != 0; }
    };

    /// @brief ノードの設定 1 つ（選択肢か文字）
    struct SettingDesc
    {
        std::string_view name;
        std::string_view label;
        std::vector<std::string_view> choices; ///< 空なら文字を書く欄
        std::string_view fallback;
        uint8_t targets = kAllTargets; ///< 設定を出す目標

        /// @brief この目標で使う設定か
        bool Supports(Target target) const { return (targets & TargetBit(target)) != 0; }
    };

    struct NodeDesc;

    /// @brief 関数の外（main の前）に置く文 1 つ
    struct GlobalCode
    {
        std::string key;  ///< 同じ key の文は 1 回だけ置く（中身が違えばエラー）
        std::string code;
    };

    /// @brief Custom Function のファイル（`Application/Assets/…`）を include の名前へ直す
    /// @return 直せなければ false（error に訳）
    using IncludeResolver = std::function<bool(const std::string& file, std::string& includeName, std::string& error)>;

    /// @brief ノードの HLSL を作るときに渡すもの
    struct EmitArgs
    {
        const Graph& graph;
        const Node& node;
        const NodeDesc& desc;
        Target target = Target::Sprite;
        uint8_t stage = kStageFragment; ///< 3D で今書いている関数（`kStageVertex` か `kStageFragment`）

        /// @brief 3D の頂点の関数を書いているか
        bool Vertex() const { return Is3D(target) && stage == kStageVertex; }

        std::vector<std::string> in;      ///< 入力の式（ピンの幅へ直したもの）
        std::vector<ValueType> inType;    ///< 入力の種類（ピンの幅へ直した後）
        std::vector<bool> connected;      ///< 入力がつながっているか
        int dynamicWidth = 1;             ///< Dynamic のピンの幅

        std::vector<ValueType> outType;   ///< 出力の種類
        std::vector<std::string> outExpr; ///< 出力を指す式（ほかのノードが使う）

        std::string code;  ///< 足す文
        std::string error; ///< 作れなかった訳（空なら作れた）

        std::vector<GlobalCode> globals; ///< 関数の外に置く文
        IncludeResolver resolveInclude;  ///< Custom Function のファイルの include の名前を決める（空ならファイル名）

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
        uint8_t targets = kAllTargets; ///< 使える目標（`TargetBit` を重ねたもの）
        uint8_t stages = kAllStages;   ///< 3D で使える段（`kStageVertex`・`kStageFragment` を重ねたもの）

        /// @brief この目標で使えるか
        bool Supports(Target target) const { return (targets & TargetBit(target)) != 0; }

        /// @brief 3D のこの段で使えるか
        bool SupportsStage(uint8_t stage) const { return (stages & stage) != 0; }

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
    /// @note Custom Function は既定のピンの定義を返す。置いたノードには `FindNodeDesc(const Node&)` を使う
    const NodeDesc* FindNodeDesc(std::string_view type);

    /// @brief 置いたノードの定義を引く（Custom Function はノードの設定のピンで作った定義。無ければ nullptr）
    const NodeDesc* FindNodeDesc(const Node& node);

    /// @brief ピンの種類の名前（Custom Function の設定に書く綴り）
    const char* ToString(ValueType type);

    /// @brief ピンの種類の名前を読む（Float・Vector2〜4・Texture2D・SamplerState）
    bool ParseValueType(std::string_view text, ValueType& type);

    /// @brief Custom Function のピンの並び（`型 名前;型 名前`）を読む（読めない項目は飛ばす）
    std::vector<FunctionPin> ParseFunctionPins(std::string_view text);

    /// @brief Custom Function のピンの並びを設定の文字にする
    std::string FormatFunctionPins(const std::vector<FunctionPin>& pins);

    /// @brief Sampler State の選択肢（Linear Repeat など）を目標のサンプラーの名前へ直す（その目標に無ければ空）
    std::string SamplerName(Target target, std::string_view filter);

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
