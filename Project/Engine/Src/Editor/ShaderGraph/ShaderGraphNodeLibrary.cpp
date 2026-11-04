#include "pch.h"
#include "ShaderGraphNodeLibrary.h"

#ifdef CORE_EDITOR

#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <set>

namespace CoreEngine::Editor::ShaderGraph
{
    namespace
    {
        constexpr ValueType F = ValueType::Float;
        constexpr ValueType V2 = ValueType::Vector2;
        constexpr ValueType V3 = ValueType::Vector3;
        constexpr ValueType V4 = ValueType::Vector4;
        constexpr ValueType Dyn = ValueType::Dynamic;
        constexpr ValueType Tex = ValueType::Texture2D;
        constexpr ValueType Smp = ValueType::SamplerState;

        PinDesc Pin(std::string_view name, ValueType type, std::array<float, 4> fallback = {},
            PinBinding binding = PinBinding::Value)
        {
            return PinDesc{ name, type, fallback, binding };
        }

        PinDesc UVPin()
        {
            return Pin("UV", V2, {}, PinBinding::UV);
        }

        /// @brief `{0}`〜`{9}` を入力の式に置き換える
        std::string Format(std::string_view pattern, const EmitArgs& args)
        {
            std::string result;
            result.reserve(pattern.size() + 32);
            for (size_t index = 0; index < pattern.size(); ++index) {
                if (pattern[index] == '{' && index + 2 < pattern.size() && pattern[index + 2] == '}'
                    && pattern[index + 1] >= '0' && pattern[index + 1] <= '9') {
                    const size_t input = static_cast<size_t>(pattern[index + 1] - '0');
                    if (input < args.in.size()) {
                        result += args.in[input];
                    }
                    index += 2;
                    continue;
                }
                result += pattern[index];
            }
            return result;
        }

        /// @brief 出力 1 本を式で作るノード
        std::function<void(EmitArgs&)> Expression(std::string pattern)
        {
            return [pattern = std::move(pattern)](EmitArgs& args) { args.Out(0, Format(pattern, args)); };
        }

        /// @brief 目標で式を変えるノード
        std::function<void(EmitArgs&)> PerTarget(std::string sprite, std::string postEffect)
        {
            return [sprite = std::move(sprite), postEffect = std::move(postEffect)](EmitArgs& args) {
                args.Out(0, Format(args.target == Target::PostEffect ? postEffect : sprite, args));
            };
        }

        /// @brief Custom Function の呼び出しと関数の文を作る
        void EmitCustomFunction(EmitArgs& args)
        {
            const std::string name = args.Setting("Name");
            std::string error;
            if (!IsValidIdentifier(name, &error)) {
                args.error = "関数の名前: " + error;
                return;
            }
            std::set<std::string_view> names;
            std::string parameters;
            const auto addParameter = [&](const PinDesc& pin, bool output) {
                if (!IsValidIdentifier(pin.name, &error)) {
                    args.error = "ピン " + std::string(pin.name) + ": " + error;
                    return false;
                }
                if (!names.insert(pin.name).second) {
                    args.error = "ピンの名前 " + std::string(pin.name) + " が重なっています";
                    return false;
                }
                if (output && !IsNumeric(pin.type)) {
                    args.error = "出力にはテクスチャとサンプラーを使えません";
                    return false;
                }
                parameters += std::string(parameters.empty() ? "" : ", ") + (output ? "out " : "")
                    + TypeName(pin.type) + " " + std::string(pin.name);
                return true;
            };
            for (const PinDesc& pin : args.desc.inputs) {
                if (!addParameter(pin, false)) {
                    return;
                }
            }
            for (const PinDesc& pin : args.desc.outputs) {
                if (!addParameter(pin, true)) {
                    return;
                }
            }

            const std::string function = name + "_float";
            if (args.Setting("Type") == "File") {
                const std::string file = args.Setting("File");
                if (file.empty()) {
                    args.error = "関数のファイルを選んでください";
                    return;
                }
                std::string include;
                if (args.resolveInclude) {
                    if (!args.resolveInclude(file, include, error)) {
                        args.error = error;
                        return;
                    }
                } else {
                    const size_t slash = file.find_last_of("/\\");
                    include = slash == std::string::npos ? file : file.substr(slash + 1);
                }
                args.globals.push_back({ "include:" + include, "#include \"" + include + "\"\n" });
            } else {
                std::string body = args.Setting("Body");
                std::erase(body, '\r');
                std::string code = "void " + function + "(" + parameters + ")\n{\n";
                size_t begin = 0;
                while (begin <= body.size()) {
                    const size_t end = (std::min)(body.find('\n', begin), body.size());
                    const std::string line = body.substr(begin, end - begin);
                    code += (line.empty() ? "" : "    " + line) + "\n";
                    begin = end + 1;
                }
                code += "}\n";
                args.globals.push_back({ "function:" + function, code });
            }

            std::string call = "    " + function + "(";
            bool first = true;
            for (const std::string& input : args.in) {
                call += (first ? "" : ", ") + input;
                first = false;
            }
            for (size_t index = 0; index < args.desc.outputs.size(); ++index) {
                const std::string variable = "n" + std::to_string(args.node.id) + "_" + std::string(args.desc.outputs[index].name);
                args.code += "    " + TypeName(args.outType[index]) + " " + variable + " = "
                    + Literal({ 0.0f, 0.0f, 0.0f, 0.0f }, WidthOf(args.outType[index])) + ";\n";
                args.outExpr[index] = variable;
                call += (first ? "" : ", ") + variable;
                first = false;
            }
            args.code += call + ");\n";
        }

        /// @brief 幅が 1 のときと 2 以上のときで式を変えるノード
        std::function<void(EmitArgs&)> ScalarOrVector(std::string scalar, std::string vector)
        {
            return [scalar = std::move(scalar), vector = std::move(vector)](EmitArgs& args) {
                args.Out(0, Format(args.dynamicWidth == 1 ? scalar : vector, args));
            };
        }

        NodeDesc Unary(std::string_view type, std::string_view title, std::string_view category,
            std::string_view summary, std::string pattern, std::array<float, 4> fallback = {})
        {
            NodeDesc desc;
            desc.type = type;
            desc.title = title;
            desc.category = category;
            desc.summary = summary;
            desc.inputs = { Pin("In", Dyn, fallback) };
            desc.outputs = { Pin("Out", Dyn) };
            desc.emit = Expression(std::move(pattern));
            return desc;
        }

        NodeDesc Binary(std::string_view type, std::string_view title, std::string_view category,
            std::string_view summary, std::string pattern, std::array<float, 4> a, std::array<float, 4> b)
        {
            NodeDesc desc;
            desc.type = type;
            desc.title = title;
            desc.category = category;
            desc.summary = summary;
            desc.inputs = { Pin("A", Dyn, a), Pin("B", Dyn, b) };
            desc.outputs = { Pin("Out", Dyn) };
            desc.emit = Expression(std::move(pattern));
            return desc;
        }

        /// @brief ピンの名前から変数名に使える部分だけを残す
        std::string Identifier(std::string_view name)
        {
            std::string result;
            for (const char c : name) {
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
                    result += c;
                }
            }
            return result;
        }

        /// @brief 成分の文字から番号（無ければ -1）
        int ComponentIndex(char c)
        {
            switch (c) {
            case 'x': case 'r': return 0;
            case 'y': case 'g': return 1;
            case 'z': case 'b': return 2;
            case 'w': case 'a': return 3;
            default: return -1;
            }
        }

        std::vector<NodeDesc> BuildNodes()
        {
            std::vector<NodeDesc> nodes;

            // ===== 出口 =====
            {
                NodeDesc desc;
                desc.type = kMasterNodeType;
                desc.title = "Fragment";
                desc.category = "";
                desc.summary = "グラフの出口。つないでいない欄は、スプライトはマテリアルを付けていないときと同じ色、"
                               "ポストエフェクトはそのままの画面になります。";
                desc.inputs = {
                    Pin("Base Color", V3, {}, PinBinding::DefaultRGB),
                    Pin("Alpha", F, {}, PinBinding::DefaultAlpha),
                };
                desc.creatable = false;
                nodes.push_back(std::move(desc));
            }

            // ===== Property =====
            {
                NodeDesc desc;
                desc.type = "Property";
                desc.title = "Property";
                desc.category = "";
                desc.summary = "Blackboard のプロパティの値。.mat とスクリプトから変えられます。";
                desc.outputs = { Pin("Out", Dyn) };
                desc.creatable = false;
                desc.resolveOutputs = [](const Graph& graph, const Node& node, int, std::vector<ValueType>& out) {
                    const Property* const property = graph.FindProperty(node.property);
                    if (!property) {
                        out[0] = F;
                    } else if (property->type == PropertyType::Texture2D) {
                        out[0] = Tex;
                    } else {
                        out[0] = VectorType(WidthOf(property->type));
                    }
                };
                desc.emit = [](EmitArgs& args) {
                    const Property* const property = args.graph.FindProperty(args.node.property);
                    if (!property) {
                        args.error = "指しているプロパティがありません";
                        return;
                    }
                    if (property->type == PropertyType::Texture2D) {
                        args.Out(0, property->name);
                    } else {
                        args.Out(0, "gMaterial." + property->name);
                    }
                };
                nodes.push_back(std::move(desc));
            }

            // ===== Input/Basic =====
            {
                NodeDesc desc;
                desc.type = "Float";
                desc.title = "Float";
                desc.category = "Input/Basic";
                desc.summary = "数 1 つ。";
                desc.inputs = { Pin("X", F) };
                desc.outputs = { Pin("Out", F) };
                desc.emit = Expression("{0}");
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "Vector2";
                desc.title = "Vector 2";
                desc.category = "Input/Basic";
                desc.summary = "数 2 つを組にする。";
                desc.inputs = { Pin("X", F), Pin("Y", F) };
                desc.outputs = { Pin("Out", V2) };
                desc.emit = Expression("float2({0}, {1})");
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "Vector3";
                desc.title = "Vector 3";
                desc.category = "Input/Basic";
                desc.summary = "数 3 つを組にする。";
                desc.inputs = { Pin("X", F), Pin("Y", F), Pin("Z", F) };
                desc.outputs = { Pin("Out", V3) };
                desc.emit = Expression("float3({0}, {1}, {2})");
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "Vector4";
                desc.title = "Vector 4";
                desc.category = "Input/Basic";
                desc.summary = "数 4 つを組にする。";
                desc.inputs = { Pin("X", F), Pin("Y", F), Pin("Z", F), Pin("W", F) };
                desc.outputs = { Pin("Out", V4) };
                desc.emit = Expression("float4({0}, {1}, {2}, {3})");
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "Color";
                desc.title = "Color";
                desc.category = "Input/Basic";
                desc.summary = "色 1 つ（RGBA）。";
                desc.outputs = { Pin("Out", V4) };
                desc.emit = [](EmitArgs& args) {
                    std::array<float, 4> color{ 1.0f, 1.0f, 1.0f, 1.0f };
                    if (const auto found = args.node.values.find("Color"); found != args.node.values.end()) {
                        color = found->second;
                    }
                    args.Out(0, Literal(color, 4));
                };
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "Time";
                desc.title = "Time";
                desc.category = "Input/Basic";
                desc.summary = "起動からの時間（秒）。Unscaled Time は時間の倍率を掛けない時間。";
                desc.outputs = { Pin("Time", F), Pin("Sine Time", F), Pin("Cosine Time", F),
                                 Pin("Delta Time", F), Pin("Unscaled Time", F) };
                desc.emit = [](EmitArgs& args) {
                    args.Out(0, "IN.time");
                    args.Out(1, "sin(IN.time)");
                    args.Out(2, "cos(IN.time)");
                    args.Out(3, "IN.deltaTime");
                    args.Out(4, "IN.unscaledTime");
                };
                nodes.push_back(std::move(desc));
            }

            // ===== Input/Geometry =====
            {
                NodeDesc desc;
                desc.type = "UV";
                desc.title = "UV";
                desc.category = "Input/Geometry";
                desc.summary = "描いている面の UV（左上が 0,0）。スプライトはコマ割りと反転を済ませた値、"
                               "ポストエフェクトは画面の UV（画素の中心）。";
                desc.outputs = { Pin("Out", V2) };
                desc.emit = Expression("IN.uv");
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "Position";
                desc.title = "Position";
                desc.category = "Input/Geometry";
                desc.summary = "スプライトはワールド座標、UI 画像は画面の px（左上が原点・Y は下向き）。";
                desc.outputs = { Pin("Out", V3) };
                desc.targets = TargetBit(Target::Sprite);
                desc.emit = Expression("IN.positionWS");
                nodes.push_back(std::move(desc));
            }

            // ===== Input/Sprite =====
            {
                NodeDesc desc;
                desc.type = "SpriteColor";
                desc.title = "Sprite Color";
                desc.category = "Input/Sprite";
                desc.summary = "テクスチャ × 部品の色（マテリアルを付けていないときと同じ色）。";
                desc.outputs = { Pin("RGBA", V4), Pin("RGB", V3), Pin("A", F) };
                desc.targets = TargetBit(Target::Sprite);
                desc.emit = [](EmitArgs& args) {
                    args.Out(0, "IN.spriteColor");
                    args.Out(1, "IN.spriteColor.rgb");
                    args.Out(2, "IN.spriteColor.a");
                };
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "VertexColor";
                desc.title = "Vertex Color";
                desc.category = "Input/Sprite";
                desc.summary = "部品の色（インスペクタとスクリプトで決める色。リニアに直した値）。";
                desc.outputs = { Pin("Out", V4) };
                desc.targets = TargetBit(Target::Sprite);
                desc.emit = Expression("IN.vertexColor");
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "MainTexture";
                desc.title = "Main Texture";
                desc.category = "Input/Sprite";
                desc.summary = "部品のテクスチャと、その大きさ（px）。";
                desc.outputs = { Pin("Texture", Tex), Pin("Size", V2) };
                desc.targets = TargetBit(Target::Sprite);
                desc.emit = [](EmitArgs& args) {
                    args.Out(0, "gTexture");
                    args.Out(1, "IN.textureSize");
                };
                nodes.push_back(std::move(desc));
            }

            // ===== Input/Scene =====
            {
                NodeDesc desc;
                desc.type = "SceneColor";
                desc.title = "Scene Color";
                desc.category = "Input/Scene";
                desc.summary = "前の段までの画面の色。UV をつながなければ、この画素の色をそのまま読みます。";
                desc.inputs = { UVPin() };
                desc.outputs = { Pin("Out", V3) };
                desc.targets = TargetBit(Target::PostEffect);
                desc.emit = [](EmitArgs& args) {
                    args.Out(0, args.connected[0] ? "SampleScreen(" + args.in[0] + ").rgb" : std::string("IN.sceneColor.rgb"));
                };
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "ScreenPosition";
                desc.title = "Screen Position";
                desc.category = "Input/Scene";
                desc.summary = "画面の上の位置。Default は 0〜1（左上が 0）、Center は -1〜1（真ん中が 0）、Pixel は px。";
                desc.outputs = { Pin("Out", V2) };
                desc.settings = { SettingDesc{ "Mode", "形", { "Default", "Center", "Pixel" }, "Default" } };
                desc.targets = TargetBit(Target::PostEffect);
                desc.emit = [](EmitArgs& args) {
                    const std::string mode = args.Setting("Mode");
                    if (mode == "Center") {
                        args.Out(0, "(IN.uv * 2.0f - 1.0f)");
                    } else if (mode == "Pixel") {
                        args.Out(0, "(IN.uv * IN.screenSize)");
                    } else {
                        args.Out(0, "IN.uv");
                    }
                };
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "Screen";
                desc.title = "Screen";
                desc.category = "Input/Scene";
                desc.summary = "画面の大きさ（px）。Texel Size は 1 ÷ 大きさ（隣の画素までの UV の差）。";
                desc.outputs = { Pin("Width", F), Pin("Height", F), Pin("Texel Size", V2) };
                desc.targets = TargetBit(Target::PostEffect);
                desc.emit = [](EmitArgs& args) {
                    args.Out(0, "IN.screenSize.x");
                    args.Out(1, "IN.screenSize.y");
                    args.Out(2, "IN.invScreenSize");
                };
                nodes.push_back(std::move(desc));
            }

            // ===== Input/Texture =====
            {
                NodeDesc desc;
                desc.type = "SampleTexture2D";
                desc.title = "Sample Texture 2D";
                desc.category = "Input/Texture";
                desc.summary = "テクスチャの色を読む。Texture をつながなければ、スプライトは部品のテクスチャ、"
                               "ポストエフェクトは画面を読みます。ポストエフェクトではミップを使いません。";
                desc.inputs = { Pin("Texture", Tex, {}, PinBinding::MainTexture), UVPin(),
                                Pin("Sampler", Smp, {}, PinBinding::Sampler) };
                desc.outputs = { Pin("RGBA", V4), Pin("R", F), Pin("G", F), Pin("B", F), Pin("A", F) };
                desc.emit = [](EmitArgs& args) {
                    const bool main = !args.connected[2] && args.in[0] == "gTexture";
                    if (args.target == Target::PostEffect) {
                        args.Out(0, main ? "SampleScreen(" + args.in[1] + ")"
                                         : args.in[0] + ".SampleLevel(" + args.in[2] + ", " + args.in[1] + ", 0.0f)");
                    } else if (main) {
                        args.Out(0, "SampleSprite(" + args.in[1] + ")");
                    } else {
                        args.Out(0, args.in[0] + ".Sample(" + args.in[2] + ", " + args.in[1] + ")");
                    }
                    const std::string rgba = args.VariableName(0);
                    args.Out(1, rgba + ".r");
                    args.Out(2, rgba + ".g");
                    args.Out(3, rgba + ".b");
                    args.Out(4, rgba + ".a");
                };
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "SamplerState";
                desc.title = "Sampler State";
                desc.category = "Input/Texture";
                desc.summary = "テクスチャの読み方（補間と、端の外の扱い）。";
                desc.outputs = { Pin("Out", Smp) };
                desc.settings = { SettingDesc{ "Filter", "読み方",
                    { "Linear Repeat", "Point Repeat", "Linear Clamp", "Point Clamp" }, "Linear Repeat" } };
                desc.emit = [](EmitArgs& args) {
                    const std::string filter = args.Setting("Filter");
                    const std::string name = SamplerName(args.target, filter);
                    if (name.empty()) {
                        args.error = filter + " は" + DisplayName(args.target) + "では使えません";
                        return;
                    }
                    args.Out(0, name);
                };
                nodes.push_back(std::move(desc));
            }

            // ===== Math/Basic =====
            nodes.push_back(Binary("Add", "Add", "Math/Basic", "A + B。", "({0} + {1})", {}, {}));
            nodes.push_back(Binary("Subtract", "Subtract", "Math/Basic", "A - B。", "({0} - {1})", {}, {}));
            nodes.push_back(Binary("Multiply", "Multiply", "Math/Basic", "A × B（成分ごと）。", "({0} * {1})",
                { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }));
            nodes.push_back(Binary("Divide", "Divide", "Math/Basic", "A ÷ B（成分ごと）。", "({0} / {1})",
                { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }));
            nodes.push_back(Binary("Power", "Power", "Math/Basic", "A の B 乗（A が負なら 0 として扱う）。",
                "pow(max({0}, 0.0f), {1})", { 1.0f, 1.0f, 1.0f, 1.0f }, { 2.0f, 2.0f, 2.0f, 2.0f }));
            nodes.push_back(Unary("SquareRoot", "Square Root", "Math/Basic", "平方根（負なら 0 として扱う）。",
                "sqrt(max({0}, 0.0f))", { 1.0f, 1.0f, 1.0f, 1.0f }));

            // ===== Math/Advanced =====
            nodes.push_back(Unary("Absolute", "Absolute", "Math/Advanced", "絶対値。", "abs({0})"));
            nodes.push_back(Unary("Negate", "Negate", "Math/Advanced", "符号を反対にする。", "(-({0}))"));
            {
                NodeDesc desc = Unary("Length", "Length", "Math/Advanced", "ベクトルの長さ。", "length({0})");
                desc.outputs = { Pin("Out", F) };
                desc.emit = ScalarOrVector("abs({0})", "length({0})");
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc = Unary("Normalize", "Normalize", "Math/Advanced", "長さを 1 にする（長さ 0 なら 0）。",
                    "", { 0.0f, 0.0f, 1.0f, 0.0f });
                desc.emit = ScalarOrVector("sign({0})", "({0} * rsqrt(max(dot({0}, {0}), 1e-12f)))");
                nodes.push_back(std::move(desc));
            }

            // ===== Math/Range =====
            {
                NodeDesc desc;
                desc.type = "Clamp";
                desc.title = "Clamp";
                desc.category = "Math/Range";
                desc.summary = "Min〜Max に収める。";
                desc.inputs = { Pin("In", Dyn), Pin("Min", Dyn), Pin("Max", Dyn, { 1.0f, 1.0f, 1.0f, 1.0f }) };
                desc.outputs = { Pin("Out", Dyn) };
                desc.emit = Expression("clamp({0}, {1}, {2})");
                nodes.push_back(std::move(desc));
            }
            nodes.push_back(Binary("Maximum", "Maximum", "Math/Range", "大きいほう。", "max({0}, {1})", {}, {}));
            nodes.push_back(Binary("Minimum", "Minimum", "Math/Range", "小さいほう。", "min({0}, {1})",
                {}, { 1.0f, 1.0f, 1.0f, 1.0f }));
            nodes.push_back(Unary("OneMinus", "One Minus", "Math/Range", "1 - In。", "(1.0f - {0})"));
            {
                NodeDesc desc;
                desc.type = "Remap";
                desc.title = "Remap";
                desc.category = "Math/Range";
                desc.summary = "In Min Max の範囲を Out Min Max の範囲へ移す。";
                desc.inputs = { Pin("In", Dyn), Pin("In Min Max", V2, { -1.0f, 1.0f, 0.0f, 0.0f }),
                                Pin("Out Min Max", V2, { 0.0f, 1.0f, 0.0f, 0.0f }) };
                desc.outputs = { Pin("Out", Dyn) };
                desc.emit = Expression("({2}.x + ({0} - {1}.x) * ({2}.y - {2}.x) / ({1}.y - {1}.x))");
                nodes.push_back(std::move(desc));
            }
            nodes.push_back(Unary("Saturate", "Saturate", "Math/Range", "0〜1 に収める。", "saturate({0})"));
            nodes.push_back(Unary("Fraction", "Fraction", "Math/Range", "小数の部分。", "frac({0})"));

            // ===== Math/Round =====
            nodes.push_back(Unary("Floor", "Floor", "Math/Round", "切り下げ。", "floor({0})"));
            nodes.push_back(Unary("Ceiling", "Ceiling", "Math/Round", "切り上げ。", "ceil({0})"));
            nodes.push_back(Unary("Round", "Round", "Math/Round", "四捨五入。", "round({0})"));
            {
                NodeDesc desc;
                desc.type = "Step";
                desc.title = "Step";
                desc.category = "Math/Round";
                desc.summary = "In が Edge 以上なら 1、それ以外は 0。";
                desc.inputs = { Pin("Edge", Dyn, { 0.5f, 0.5f, 0.5f, 0.5f }), Pin("In", Dyn) };
                desc.outputs = { Pin("Out", Dyn) };
                desc.emit = Expression("step({0}, {1})");
                nodes.push_back(std::move(desc));
            }

            // ===== Math/Interpolation =====
            {
                NodeDesc desc;
                desc.type = "Lerp";
                desc.title = "Lerp";
                desc.category = "Math/Interpolation";
                desc.summary = "T が 0 なら A、1 なら B。間はまっすぐつなぐ。";
                desc.inputs = { Pin("A", Dyn), Pin("B", Dyn, { 1.0f, 1.0f, 1.0f, 1.0f }), Pin("T", Dyn) };
                desc.outputs = { Pin("Out", Dyn) };
                desc.emit = Expression("lerp({0}, {1}, {2})");
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "InverseLerp";
                desc.title = "Inverse Lerp";
                desc.category = "Math/Interpolation";
                desc.summary = "T が A〜B のどこにあるか（A で 0、B で 1）。";
                desc.inputs = { Pin("A", Dyn), Pin("B", Dyn, { 1.0f, 1.0f, 1.0f, 1.0f }), Pin("T", Dyn) };
                desc.outputs = { Pin("Out", Dyn) };
                desc.emit = Expression("(({2} - {0}) / ({1} - {0}))");
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "Smoothstep";
                desc.title = "Smoothstep";
                desc.category = "Math/Interpolation";
                desc.summary = "In が Edge1〜Edge2 の間で 0 から 1 へなめらかに変わる。";
                desc.inputs = { Pin("Edge1", Dyn), Pin("Edge2", Dyn, { 1.0f, 1.0f, 1.0f, 1.0f }), Pin("In", Dyn) };
                desc.outputs = { Pin("Out", Dyn) };
                desc.emit = Expression("smoothstep({0}, {1}, {2})");
                nodes.push_back(std::move(desc));
            }

            // ===== Math/Trigonometry =====
            nodes.push_back(Unary("Sine", "Sine", "Math/Trigonometry", "sin（ラジアン）。", "sin({0})"));
            nodes.push_back(Unary("Cosine", "Cosine", "Math/Trigonometry", "cos（ラジアン）。", "cos({0})"));

            // ===== Math/Vector =====
            {
                NodeDesc desc = Binary("DotProduct", "Dot Product", "Math/Vector", "内積。", "", {}, {});
                desc.outputs = { Pin("Out", F) };
                desc.emit = ScalarOrVector("({0} * {1})", "dot({0}, {1})");
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc = Binary("Distance", "Distance", "Math/Vector", "A と B の距離。", "", {}, {});
                desc.outputs = { Pin("Out", F) };
                desc.emit = ScalarOrVector("abs({0} - {1})", "distance({0}, {1})");
                nodes.push_back(std::move(desc));
            }

            // ===== Channel =====
            {
                NodeDesc desc;
                desc.type = "Split";
                desc.title = "Split";
                desc.category = "Channel";
                desc.summary = "成分に分ける（無い成分は 0）。";
                desc.inputs = { Pin("In", Dyn) };
                desc.outputs = { Pin("R", F), Pin("G", F), Pin("B", F), Pin("A", F) };
                desc.emit = [](EmitArgs& args) {
                    constexpr const char* kComponents[] = { "x", "y", "z", "w" };
                    for (size_t index = 0; index < 4; ++index) {
                        if (args.dynamicWidth == 1) {
                            args.Out(index, index == 0 ? args.in[0] : "0.0f");
                        } else if (static_cast<int>(index) < args.dynamicWidth) {
                            args.Out(index, "(" + args.in[0] + ")." + kComponents[index]);
                        } else {
                            args.Out(index, "0.0f");
                        }
                    }
                };
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "Combine";
                desc.title = "Combine";
                desc.category = "Channel";
                desc.summary = "成分を組にする。";
                desc.inputs = { Pin("R", F), Pin("G", F), Pin("B", F), Pin("A", F) };
                desc.outputs = { Pin("RGBA", V4), Pin("RGB", V3), Pin("RG", V2) };
                desc.emit = [](EmitArgs& args) {
                    args.Out(0, Format("float4({0}, {1}, {2}, {3})", args));
                    const std::string rgba = args.VariableName(0);
                    args.Out(1, rgba + ".rgb");
                    args.Out(2, rgba + ".rg");
                };
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "Swizzle";
                desc.title = "Swizzle";
                desc.category = "Channel";
                desc.summary = "成分を並べ替える（xyzw か rgba を 1〜4 文字）。";
                desc.inputs = { Pin("In", Dyn) };
                desc.outputs = { Pin("Out", Dyn) };
                desc.settings = { SettingDesc{ "Mask", "並び", {}, "xyzw" } };
                desc.resolveOutputs = [](const Graph&, const Node& node, int, std::vector<ValueType>& out) {
                    const auto found = node.settings.find("Mask");
                    const size_t length = found == node.settings.end() ? 4 : found->second.size();
                    out[0] = VectorType(static_cast<int>(std::clamp<size_t>(length, 1, 4)));
                };
                desc.emit = [](EmitArgs& args) {
                    const std::string mask = args.Setting("Mask");
                    if (mask.empty() || mask.size() > 4) {
                        args.error = "並びは 1〜4 文字で書いてください";
                        return;
                    }
                    std::string components;
                    for (const char c : mask) {
                        const int index = ComponentIndex(c);
                        if (index < 0) {
                            args.error = "並びに使えるのは x y z w か r g b a です";
                            return;
                        }
                        if (index >= args.dynamicWidth) {
                            args.error = "入力に無い成分（" + std::string(1, c) + "）を指しています";
                            return;
                        }
                        components += "xyzw"[index];
                    }
                    if (args.dynamicWidth == 1) {
                        args.Out(0, Convert(args.in[0], 1, static_cast<int>(components.size())));
                    } else {
                        args.Out(0, "(" + args.in[0] + ")." + components);
                    }
                };
                nodes.push_back(std::move(desc));
            }

            // ===== UV =====
            {
                NodeDesc desc;
                desc.type = "TilingAndOffset";
                desc.title = "Tiling And Offset";
                desc.category = "UV";
                desc.summary = "UV × Tiling + Offset。";
                desc.inputs = { UVPin(), Pin("Tiling", V2, { 1.0f, 1.0f, 0.0f, 0.0f }), Pin("Offset", V2) };
                desc.outputs = { Pin("Out", V2) };
                desc.emit = Expression("({0} * {1} + {2})");
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "Rotate";
                desc.title = "Rotate";
                desc.category = "UV";
                desc.summary = "UV を Center のまわりに回す。";
                desc.inputs = { UVPin(), Pin("Center", V2, { 0.5f, 0.5f, 0.0f, 0.0f }), Pin("Rotation", F) };
                desc.outputs = { Pin("Out", V2) };
                desc.settings = { SettingDesc{ "Unit", "角度の単位", { "Radians", "Degrees" }, "Radians" } };
                desc.emit = [](EmitArgs& args) {
                    const std::string rotation = args.Setting("Unit") == "Degrees"
                        ? "radians(" + args.in[2] + ")" : args.in[2];
                    args.Out(0, "SG_Rotate(" + args.in[0] + ", " + args.in[1] + ", " + rotation + ")");
                };
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "PolarCoordinates";
                desc.title = "Polar Coordinates";
                desc.category = "UV";
                desc.summary = "UV を Center からの距離（x）と角度（y）にする。";
                desc.inputs = { UVPin(), Pin("Center", V2, { 0.5f, 0.5f, 0.0f, 0.0f }),
                                Pin("Radial Scale", F, { 1.0f, 0.0f, 0.0f, 0.0f }),
                                Pin("Length Scale", F, { 1.0f, 0.0f, 0.0f, 0.0f }) };
                desc.outputs = { Pin("Out", V2) };
                desc.emit = Expression("SG_PolarCoordinates({0}, {1}, {2}, {3})");
                nodes.push_back(std::move(desc));
            }

            // ===== Procedural =====
            {
                NodeDesc desc;
                desc.type = "SimpleNoise";
                desc.title = "Simple Noise";
                desc.category = "Procedural/Noise";
                desc.summary = "値のノイズ（0〜1）。";
                desc.inputs = { UVPin(), Pin("Scale", F, { 40.0f, 0.0f, 0.0f, 0.0f }) };
                desc.outputs = { Pin("Out", F) };
                desc.emit = Expression("SG_SimpleNoise({0}, {1})");
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "GradientNoise";
                desc.title = "Gradient Noise";
                desc.category = "Procedural/Noise";
                desc.summary = "なめらかなノイズ（おおむね 0〜1）。";
                desc.inputs = { UVPin(), Pin("Scale", F, { 10.0f, 0.0f, 0.0f, 0.0f }) };
                desc.outputs = { Pin("Out", F) };
                desc.emit = Expression("SG_GradientNoise({0}, {1})");
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "Voronoi";
                desc.title = "Voronoi";
                desc.category = "Procedural/Noise";
                desc.summary = "細胞の模様。Out は近い点までの距離、Cells は細胞ごとの乱数。";
                desc.inputs = { UVPin(), Pin("Angle Offset", F, { 2.0f, 0.0f, 0.0f, 0.0f }),
                                Pin("Cell Density", F, { 5.0f, 0.0f, 0.0f, 0.0f }) };
                desc.outputs = { Pin("Out", F), Pin("Cells", F) };
                desc.emit = [](EmitArgs& args) {
                    const std::string distance = args.VariableName(0);
                    const std::string cells = args.VariableName(1);
                    args.code += "    float " + distance + " = 0.0f;\n";
                    args.code += "    float " + cells + " = 0.0f;\n";
                    args.code += "    SG_Voronoi(" + args.in[0] + ", " + args.in[1] + ", " + args.in[2] + ", "
                        + distance + ", " + cells + ");\n";
                    args.outExpr[0] = distance;
                    args.outExpr[1] = cells;
                };
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "Ellipse";
                desc.title = "Ellipse";
                desc.category = "Procedural/Shape";
                desc.summary = "UV の真ん中の楕円（内側が 1）。";
                desc.inputs = { UVPin(), Pin("Width", F, { 0.5f, 0.0f, 0.0f, 0.0f }),
                                Pin("Height", F, { 0.5f, 0.0f, 0.0f, 0.0f }) };
                desc.outputs = { Pin("Out", F) };
                desc.emit = PerTarget("SG_Ellipse({0}, {1}, {2})", "SG_EllipseScreen({0}, {1}, {2})");
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "Rectangle";
                desc.title = "Rectangle";
                desc.category = "Procedural/Shape";
                desc.summary = "UV の真ん中の長方形（内側が 1）。";
                desc.inputs = { UVPin(), Pin("Width", F, { 0.5f, 0.0f, 0.0f, 0.0f }),
                                Pin("Height", F, { 0.5f, 0.0f, 0.0f, 0.0f }) };
                desc.outputs = { Pin("Out", F) };
                desc.emit = PerTarget("SG_Rectangle({0}, {1}, {2})", "SG_RectangleScreen({0}, {1}, {2})");
                nodes.push_back(std::move(desc));
            }

            // ===== Utility =====
            nodes.push_back(Unary("Preview", "Preview", "Utility",
                "値をそのまま渡す。選ぶと Main Preview にこの値を映せます。", "{0}"));
            {
                NodeDesc desc;
                desc.type = kCustomFunctionType;
                desc.title = "Custom Function";
                desc.category = "Utility";
                desc.summary = "HLSL で書いた関数を呼ぶ。本文を書くか（String）、関数を書いたファイルを指します（File）。"
                               "呼ぶ関数の名前は「名前_float」です。";
                desc.inputs = { Pin("In", V3) };
                desc.outputs = { Pin("Out", V3) };
                desc.settings = {
                    SettingDesc{ "Type", "種類", { "String", "File" }, "String" },
                    SettingDesc{ "Name", "関数の名前", {}, "MyFunction" },
                    SettingDesc{ "Body", "本文", {}, "Out = In;" },
                    SettingDesc{ "File", "ファイル", {}, "" },
                    SettingDesc{ "Inputs", "入力", {}, "Vector3 In" },
                    SettingDesc{ "Outputs", "出力", {}, "Vector3 Out" },
                };
                desc.emit = EmitCustomFunction;
                nodes.push_back(std::move(desc));
            }

            return nodes;
        }

        /// @brief Custom Function のピンの並びごとに作った定義（ピンの名前の文字列もここに持つ）
        struct OwnedNodeDesc
        {
            std::vector<std::string> names;
            NodeDesc desc;
        };

        /// @brief Custom Function のピン 1 つの定義（テクスチャとサンプラーは、つながなければ既定のもの）
        PinDesc FunctionPinDesc(std::string_view name, ValueType type)
        {
            PinBinding binding = PinBinding::Value;
            if (type == Tex) {
                binding = PinBinding::MainTexture;
            } else if (type == Smp) {
                binding = PinBinding::Sampler;
            }
            return Pin(name, type, {}, binding);
        }
    }

    std::string EmitArgs::VariableName(size_t output) const
    {
        return "n" + std::to_string(node.id) + "_" + Identifier(desc.outputs[output].name);
    }

    void EmitArgs::Out(size_t output, std::string_view expression)
    {
        if (output >= outType.size()) {
            return;
        }
        if (!IsNumeric(outType[output])) {
            outExpr[output] = std::string(expression);
            return;
        }
        const std::string name = VariableName(output);
        code += "    " + TypeName(outType[output]) + " " + name + " = " + std::string(expression) + ";\n";
        outExpr[output] = name;
    }

    std::string EmitArgs::Setting(std::string_view name) const
    {
        if (const auto found = node.settings.find(name); found != node.settings.end()) {
            return found->second;
        }
        for (const SettingDesc& setting : desc.settings) {
            if (setting.name == name) {
                return std::string(setting.fallback);
            }
        }
        return {};
    }

    int NodeDesc::FindInput(std::string_view name) const
    {
        for (size_t index = 0; index < inputs.size(); ++index) {
            if (inputs[index].name == name) {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    int NodeDesc::FindOutput(std::string_view name) const
    {
        for (size_t index = 0; index < outputs.size(); ++index) {
            if (outputs[index].name == name) {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    const std::vector<NodeDesc>& AllNodes()
    {
        static const std::vector<NodeDesc> nodes = BuildNodes();
        return nodes;
    }

    const NodeDesc* FindNodeDesc(std::string_view type)
    {
        for (const NodeDesc& desc : AllNodes()) {
            if (desc.type == type) {
                return &desc;
            }
        }
        return nullptr;
    }

    const NodeDesc* FindNodeDesc(const Node& node)
    {
        const NodeDesc* const base = FindNodeDesc(node.type);
        if (!base || node.type != kCustomFunctionType) {
            return base;
        }
        const auto setting = [&](std::string_view name) {
            if (const auto found = node.settings.find(name); found != node.settings.end()) {
                return found->second;
            }
            for (const SettingDesc& desc : base->settings) {
                if (desc.name == name) {
                    return std::string(desc.fallback);
                }
            }
            return std::string();
        };
        const std::string inputs = setting("Inputs");
        const std::string outputs = setting("Outputs");
        const std::string key = inputs + "\n" + outputs;

        static std::mutex mutex;
        static std::map<std::string, std::unique_ptr<OwnedNodeDesc>> cache;
        std::scoped_lock lock(mutex);
        if (const auto found = cache.find(key); found != cache.end()) {
            return &found->second->desc;
        }
        const std::vector<FunctionPin> in = ParseFunctionPins(inputs);
        const std::vector<FunctionPin> out = ParseFunctionPins(outputs);
        auto owned = std::make_unique<OwnedNodeDesc>();
        owned->names.reserve(in.size() + out.size());
        for (const FunctionPin& pin : in) {
            owned->names.push_back(pin.name);
        }
        for (const FunctionPin& pin : out) {
            owned->names.push_back(pin.name);
        }
        owned->desc = *base;
        owned->desc.inputs.clear();
        owned->desc.outputs.clear();
        for (size_t index = 0; index < in.size(); ++index) {
            owned->desc.inputs.push_back(FunctionPinDesc(owned->names[index], in[index].type));
        }
        for (size_t index = 0; index < out.size(); ++index) {
            owned->desc.outputs.push_back(FunctionPinDesc(owned->names[in.size() + index], out[index].type));
        }
        return &cache.emplace(key, std::move(owned)).first->second->desc;
    }

    const char* ToString(ValueType type)
    {
        switch (type) {
        case ValueType::Float: return "Float";
        case ValueType::Vector2: return "Vector2";
        case ValueType::Vector3: return "Vector3";
        case ValueType::Vector4: return "Vector4";
        case ValueType::Dynamic: return "Dynamic";
        case ValueType::Texture2D: return "Texture2D";
        case ValueType::SamplerState: return "SamplerState";
        }
        return "Float";
    }

    bool ParseValueType(std::string_view text, ValueType& type)
    {
        for (const ValueType candidate : { F, V2, V3, V4, Tex, Smp }) {
            if (text == ToString(candidate)) {
                type = candidate;
                return true;
            }
        }
        return false;
    }

    std::vector<FunctionPin> ParseFunctionPins(std::string_view text)
    {
        const auto trim = [](std::string_view value) {
            const size_t begin = value.find_first_not_of(" \t\r\n");
            if (begin == std::string_view::npos) {
                return std::string_view();
            }
            const size_t end = value.find_last_not_of(" \t\r\n");
            return value.substr(begin, end - begin + 1);
        };
        std::vector<FunctionPin> pins;
        size_t begin = 0;
        while (begin <= text.size() && pins.size() < kMaxFunctionPins) {
            const size_t end = (std::min)(text.find(';', begin), text.size());
            const std::string_view item = trim(text.substr(begin, end - begin));
            begin = end + 1;
            const size_t space = item.find_first_of(" \t");
            if (space == std::string_view::npos) {
                continue;
            }
            FunctionPin pin;
            if (!ParseValueType(item.substr(0, space), pin.type)) {
                continue;
            }
            pin.name = std::string(trim(item.substr(space + 1)));
            if (!pin.name.empty()) {
                pins.push_back(std::move(pin));
            }
        }
        return pins;
    }

    std::string FormatFunctionPins(const std::vector<FunctionPin>& pins)
    {
        std::string text;
        for (const FunctionPin& pin : pins) {
            text += std::string(text.empty() ? "" : ";") + ToString(pin.type) + " " + pin.name;
        }
        return text;
    }

    std::string SamplerName(Target target, std::string_view filter)
    {
        if (target == Target::PostEffect) {
            if (filter == "Point Repeat") {
                return {};
            }
            if (filter == "Linear Clamp") {
                return "gSampler";
            }
            if (filter == "Point Clamp") {
                return "gPointSampler";
            }
            return "gWrapSampler";
        }
        if (filter == "Point Repeat") {
            return "gPointSampler";
        }
        if (filter == "Linear Clamp") {
            return "gClampSampler";
        }
        if (filter == "Point Clamp") {
            return {};
        }
        return "gSampler";
    }

    std::string TypeName(int width)
    {
        switch (width) {
        case 2: return "float2";
        case 3: return "float3";
        case 4: return "float4";
        default: return "float";
        }
    }

    std::string TypeName(ValueType type)
    {
        switch (type) {
        case ValueType::Texture2D: return "Texture2D<float4>";
        case ValueType::SamplerState: return "SamplerState";
        default: return TypeName(WidthOf(type));
        }
    }

    std::string FormatFloat(float value)
    {
        if (!std::isfinite(value)) {
            return "0.0f";
        }
        if (value == 0.0f) {
            return "0.0f";
        }
        char buffer[32];
        const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value);
        if (error != std::errc()) {
            return "0.0f";
        }
        std::string text(buffer, end);
        if (text.find_first_of(".eE") == std::string::npos) {
            text += ".0";
        }
        return text + "f";
    }

    std::string Literal(const std::array<float, 4>& numbers, int width)
    {
        if (width <= 1) {
            return FormatFloat(numbers[0]);
        }
        std::string text = TypeName(width) + "(";
        for (int index = 0; index < width && index < 4; ++index) {
            if (index > 0) {
                text += ", ";
            }
            text += FormatFloat(numbers[static_cast<size_t>(index)]);
        }
        return text + ")";
    }

    std::string Convert(std::string_view expression, int from, int to)
    {
        const std::string text(expression);
        if (from == to || from <= 0 || to <= 0) {
            return text;
        }
        if (from == 1) {
            return "((" + TypeName(to) + ")(" + text + "))";
        }
        if (from > to) {
            return "(" + text + ")." + std::string("xyzw").substr(0, static_cast<size_t>(to));
        }
        if (to == 3) {
            return "float3(" + text + ", 0.0f)";
        }
        return from == 2 ? "float4(" + text + ", 0.0f, 1.0f)" : "float4(" + text + ", 1.0f)";
    }

    int WidthOf(ValueType type)
    {
        switch (type) {
        case ValueType::Float:   return 1;
        case ValueType::Vector2: return 2;
        case ValueType::Vector3: return 3;
        case ValueType::Vector4: return 4;
        default:                 return 0;
        }
    }

    ValueType VectorType(int width)
    {
        switch (width) {
        case 2: return ValueType::Vector2;
        case 3: return ValueType::Vector3;
        case 4: return ValueType::Vector4;
        default: return ValueType::Float;
        }
    }
}

#endif // CORE_EDITOR
