#include "pch.h"
#include "ShaderGraphNodeLibrary.h"

#ifdef CORE_EDITOR

#include <algorithm>
#include <charconv>
#include <cmath>
#include <deque>
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

        /// @brief 3D（Lit）の出口だけに出す欄（つながなければモデルの今の値）
        PinDesc LitPin(std::string_view name, ValueType type, std::array<float, 4> fallback = {})
        {
            PinDesc pin = Pin(name, type, fallback, PinBinding::ModelValue);
            pin.targets = TargetBit(Target::Lit);
            return pin;
        }

        /// @brief 3D だけに出す設定
        SettingDesc Setting3D(std::string_view name, std::string_view label, std::vector<std::string_view> choices,
            std::string_view fallback)
        {
            SettingDesc setting{ name, label, std::move(choices), fallback };
            setting.targets = k3DTargets;
            return setting;
        }

        /// @brief 3D の頂点の関数の、物体の空間の向き → ワールドの向き（正規化）
        std::string VertexDirectionToWorld(const std::string& direction)
        {
            return "normalize(mul(" + direction + ", (float3x3)IN.objectToWorld))";
        }

        /// @brief 3D の頂点の関数の、物体の空間の法線 → ワールドの法線（正規化）
        std::string VertexNormalToWorld(const std::string& normal)
        {
            return "normalize(mul((float3x3)IN.worldToObject, " + normal + "))";
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
                const std::string variable = args.prefix + "n" + std::to_string(args.node.id) + "_"
                    + std::string(args.desc.outputs[index].name);
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
                               "ポストエフェクトはそのままの画面、3D はモデルの今の値（テクスチャ × MaterialComponent の値）になります。";
                desc.inputs = {
                    Pin("Base Color", V3, {}, PinBinding::DefaultRGB),
                    LitPin("Normal (Tangent Space)", V3, { 0.0f, 0.0f, 1.0f, 0.0f }),
                    LitPin("Metallic", F),
                    LitPin("Roughness", F, { 0.5f, 0.0f, 0.0f, 0.0f }),
                    LitPin("Ambient Occlusion", F, { 1.0f, 0.0f, 0.0f, 0.0f }),
                    LitPin("Emission", V3),
                    Pin("Alpha", F, {}, PinBinding::DefaultAlpha),
                };
                desc.creatable = false;
                desc.stages = kStageFragment;
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = kVertexMasterNodeType;
                desc.title = "Vertex";
                desc.category = "";
                desc.summary = "3D の頂点の出口（物体の空間）。つないでいない欄は元の頂点のまま。"
                               "つなぐと今と前のフレームの時間で 2 回計算し、TAA とレイトレの影も同じ形になります。";
                desc.inputs = {
                    Pin("Position", V3, {}, PinBinding::SourceVertex),
                    Pin("Normal", V3, { 0.0f, 1.0f, 0.0f, 0.0f }, PinBinding::SourceVertex),
                };
                desc.creatable = false;
                desc.targets = k3DTargets;
                desc.stages = kStageVertex;
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
                    out[0] = property ? ToValueType(property->type) : F;
                };
                desc.emit = [](EmitArgs& args) {
                    const Property* const property = args.graph.FindProperty(args.node.property);
                    if (!property) {
                        args.error = "指しているプロパティがありません";
                        return;
                    }
                    if (property->type == PropertyType::SamplerState) {
                        args.error = "サンプラーの入力はサブグラフの中でだけ使えます";
                        return;
                    }
                    if (property->type == PropertyType::Texture2D) {
                        args.Out(0, property->name);
                    } else {
                        args.Out(0, (Is3D(args.target) ? "gGraph." : "gMaterial.") + property->name);
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
                desc.summary = "起動からの時間（秒）。Unscaled Time は時間の倍率を掛けない時間。"
                               "3D は 240 秒で 0 に戻り、Unscaled Time は Time と同じ。Vertex では今と前のフレームの時間。";
                desc.outputs = { Pin("Time", F), Pin("Sine Time", F), Pin("Cosine Time", F),
                                 Pin("Delta Time", F), Pin("Unscaled Time", F) };
                desc.emit = [](EmitArgs& args) {
                    if (!Is3D(args.target)) {
                        args.Out(0, "IN.time");
                        args.Out(1, "sin(IN.time)");
                        args.Out(2, "cos(IN.time)");
                        args.Out(3, "IN.deltaTime");
                        args.Out(4, "IN.unscaledTime");
                        return;
                    }
                    args.Out(0, args.Vertex() ? "IN.time" : "SurfaceTime()");
                    const std::string time = args.VariableName(0);
                    args.Out(1, "sin(" + time + ")");
                    args.Out(2, "cos(" + time + ")");
                    args.Out(3, "SurfaceDeltaTime()");
                    args.Out(4, time);
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
                               "ポストエフェクトは画面の UV（画素の中心）、3D はモデルの UV（Fragment は MaterialComponent の"
                               " UV の変換を掛けた値・Vertex は頂点の値のまま）。";
                desc.outputs = { Pin("Out", V2) };
                desc.emit = Expression("IN.uv");
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "Position";
                desc.title = "Position";
                desc.category = "Input/Geometry";
                desc.summary = "スプライトはワールド座標、UI 画像は画面の px（左上が原点・Y は下向き）。"
                               "3D は Space で World（ワールド）か Object（物体の空間・Vertex だけ）を選ぶ。";
                desc.outputs = { Pin("Out", V3) };
                desc.settings = { Setting3D("Space", "空間", { "World", "Object" }, "World") };
                desc.targets = TargetBit(Target::Sprite) | k3DTargets;
                desc.emit = [](EmitArgs& args) {
                    if (!Is3D(args.target)) {
                        args.Out(0, "IN.positionWS");
                        return;
                    }
                    const bool object = args.Setting("Space") == "Object";
                    if (args.Vertex()) {
                        args.Out(0, object ? "IN.positionOS" : "mul(float4(IN.positionOS, 1.0f), IN.objectToWorld).xyz");
                    } else if (object) {
                        args.error = "Object の空間は Vertex だけで使えます";
                    } else {
                        args.Out(0, "IN.positionWS");
                    }
                };
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "NormalVector";
                desc.title = "Normal Vector";
                desc.category = "Input/Geometry";
                desc.summary = "面の法線（正規化済み）。World・Object（Vertex だけ）・Tangent（接空間・いつも 0,0,1）。";
                desc.outputs = { Pin("Out", V3) };
                desc.settings = { SettingDesc{ "Space", "空間", { "World", "Object", "Tangent" }, "World" } };
                desc.targets = k3DTargets;
                desc.emit = [](EmitArgs& args) {
                    const std::string space = args.Setting("Space");
                    if (space == "Tangent") {
                        args.Out(0, "float3(0.0f, 0.0f, 1.0f)");
                    } else if (args.Vertex()) {
                        args.Out(0, space == "Object" ? "IN.normalOS" : VertexNormalToWorld("IN.normalOS"));
                    } else if (space == "Object") {
                        args.error = "Object の空間は Vertex だけで使えます";
                    } else {
                        args.Out(0, "normalize(IN.normalWS)");
                    }
                };
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "TangentVector";
                desc.title = "Tangent Vector";
                desc.category = "Input/Geometry";
                desc.summary = "面の接線（UV の U の向き・正規化済み）。World・Object（Vertex だけ）。";
                desc.outputs = { Pin("Out", V3) };
                desc.settings = { SettingDesc{ "Space", "空間", { "World", "Object" }, "World" } };
                desc.targets = k3DTargets;
                desc.emit = [](EmitArgs& args) {
                    const bool object = args.Setting("Space") == "Object";
                    if (args.Vertex()) {
                        args.Out(0, object ? "IN.tangentOS" : VertexDirectionToWorld("IN.tangentOS"));
                    } else if (object) {
                        args.error = "Object の空間は Vertex だけで使えます";
                    } else {
                        args.Out(0, "normalize(IN.tangentWS)");
                    }
                };
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "ViewDirection";
                desc.title = "View Direction";
                desc.category = "Input/Geometry";
                desc.summary = "物の表面からカメラへの向き（ワールド・正規化済み）。";
                desc.outputs = { Pin("Out", V3) };
                desc.targets = k3DTargets;
                desc.stages = kStageFragment;
                desc.emit = Expression("SurfaceViewDirection(IN)");
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

            // ===== Input/Model =====
            {
                NodeDesc desc;
                desc.type = "ModelSurface";
                desc.title = "Model Surface";
                desc.category = "Input/Model";
                desc.summary = "モデルの今の値（テクスチャ × MaterialComponent の値）。出口の欄につながないときの値と同じ。"
                               "Normal はモデルの法線マップを接空間のまま返します（無ければ 0,0,1）。";
                desc.outputs = { Pin("Base Color", V3), Pin("Alpha", F), Pin("Normal (Tangent Space)", V3),
                                 Pin("Metallic", F), Pin("Roughness", F), Pin("Ambient Occlusion", F), Pin("Emission", V3) };
                desc.targets = k3DTargets;
                desc.stages = kStageFragment;
                desc.emit = [](EmitArgs& args) {
                    args.Out(0, "OUT.baseColor");
                    args.Out(1, "OUT.alpha");
                    args.Out(2, "SurfaceModelNormalTangent(IN)");
                    args.Out(3, "OUT.metallic");
                    args.Out(4, "OUT.roughness");
                    args.Out(5, "OUT.occlusion");
                    args.Out(6, "OUT.emission");
                };
                nodes.push_back(std::move(desc));
            }

            // ===== Input/Scene =====
            {
                NodeDesc desc;
                desc.type = "Camera";
                desc.title = "Camera";
                desc.category = "Input/Scene";
                desc.summary = "描いているカメラの位置（ワールド）。";
                desc.outputs = { Pin("Position", V3) };
                desc.targets = k3DTargets;
                desc.stages = kStageFragment;
                desc.emit = Expression("gCamera.worldPosition");
                nodes.push_back(std::move(desc));
            }
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
                desc.summary = "画面の上の位置。Default は 0〜1（左上が 0）、Center は -1〜1（真ん中が 0）、Pixel は px。"
                               "3D は画素の位置（px・左上が 0）だけ。";
                desc.outputs = { Pin("Out", V2) };
                desc.settings = { SettingDesc{ "Mode", "形", { "Default", "Center", "Pixel" }, "Default" } };
                desc.settings.front().targets = TargetBit(Target::PostEffect);
                desc.targets = TargetBit(Target::PostEffect) | k3DTargets;
                desc.stages = kStageFragment;
                desc.emit = [](EmitArgs& args) {
                    if (Is3D(args.target)) {
                        args.Out(0, "IN.screenPosition");
                        return;
                    }
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
                               "ポストエフェクトは画面、3D はモデルのベースカラーのテクスチャを読みます。"
                               "ポストエフェクトではミップを使いません。Vertex では Sample Texture 2D LOD を使います。";
                desc.inputs = { Pin("Texture", Tex, {}, PinBinding::MainTexture), UVPin(),
                                Pin("Sampler", Smp, {}, PinBinding::Sampler) };
                desc.outputs = { Pin("RGBA", V4), Pin("R", F), Pin("G", F), Pin("B", F), Pin("A", F) };
                desc.stages = kStageFragment;
                desc.emit = [](EmitArgs& args) {
                    const bool main = !args.connected[2] && args.in[0] == "gTexture";
                    if (args.target == Target::PostEffect) {
                        args.Out(0, main ? "SampleScreen(" + args.in[1] + ")"
                                         : args.in[0] + ".SampleLevel(" + args.in[2] + ", " + args.in[1] + ", 0.0f)");
                    } else if (main && args.target == Target::Sprite) {
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
                desc.type = "SampleTexture2DLOD";
                desc.title = "Sample Texture 2D LOD";
                desc.category = "Input/Texture";
                desc.summary = "ミップの段（LOD・0 がいちばん細かい）を決めてテクスチャの色を読む。Vertex でも使えます。"
                               "Texture をつながなければ Sample Texture 2D と同じものを読みます。";
                desc.inputs = { Pin("Texture", Tex, {}, PinBinding::MainTexture), UVPin(),
                                Pin("Sampler", Smp, {}, PinBinding::Sampler), Pin("LOD", F) };
                desc.outputs = { Pin("RGBA", V4), Pin("R", F), Pin("G", F), Pin("B", F), Pin("A", F) };
                desc.emit = [](EmitArgs& args) {
                    args.Out(0, args.in[0] + ".SampleLevel(" + args.in[2] + ", " + args.in[1] + ", " + args.in[3] + ")");
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
            {
                NodeDesc desc;
                desc.type = "FresnelEffect";
                desc.title = "Fresnel Effect";
                desc.category = "Math/Vector";
                desc.summary = "面を斜めから見るほど 1 に近づく値（pow(1 - dot(N, V), Power)）。"
                               "つながなければ Normal は面の法線、View Dir はカメラへの向き（どちらもワールド）。";
                desc.inputs = { Pin("Normal", V3, { 0.0f, 0.0f, 1.0f, 0.0f }, PinBinding::WorldNormal),
                                Pin("View Dir", V3, { 0.0f, 0.0f, 1.0f, 0.0f }, PinBinding::ViewDirection),
                                Pin("Power", F, { 1.0f, 0.0f, 0.0f, 0.0f }) };
                desc.outputs = { Pin("Out", F) };
                desc.targets = k3DTargets;
                desc.stages = kStageFragment;
                desc.emit = Expression("SG_FresnelEffect({0}, {1}, {2})");
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "Transform";
                desc.title = "Transform";
                desc.category = "Math/Vector";
                desc.summary = "位置か向きを空間の間で移す。Object と World の間は Vertex、World と Tangent の間は Fragment で使えます。"
                               "Direction は移した後に正規化します。";
                desc.inputs = { Pin("In", V3) };
                desc.outputs = { Pin("Out", V3) };
                desc.settings = { SettingDesc{ "From", "元", { "Object", "World", "Tangent" }, "Object" },
                                  SettingDesc{ "To", "先", { "Object", "World", "Tangent" }, "World" },
                                  SettingDesc{ "Type", "種類", { "Position", "Direction" }, "Position" } };
                desc.targets = k3DTargets;
                desc.emit = [](EmitArgs& args) {
                    const std::string from = args.Setting("From");
                    const std::string to = args.Setting("To");
                    const bool position = args.Setting("Type") == "Position";
                    const std::string& value = args.in[0];
                    if (from == to) {
                        args.Out(0, value);
                        return;
                    }
                    if (args.Vertex()) {
                        if (from == "Object" && to == "World") {
                            args.Out(0, position ? "mul(float4(" + value + ", 1.0f), IN.objectToWorld).xyz"
                                                 : VertexDirectionToWorld(value));
                            return;
                        }
                        if (from == "World" && to == "Object") {
                            args.Out(0, position ? "mul(float4(" + value + ", 1.0f), IN.worldToObject).xyz"
                                                 : "normalize(mul(" + value + ", (float3x3)IN.worldToObject))");
                            return;
                        }
                        args.error = "Vertex では Object と World の間だけ移せます";
                        return;
                    }
                    if (from == "World" && to == "Tangent") {
                        args.Out(0, "SurfaceWorldToTangent(IN, " + value + ")");
                        return;
                    }
                    if (from == "Tangent" && to == "World") {
                        args.Out(0, "SurfaceTangentToWorld(IN, " + value + ")");
                        return;
                    }
                    args.error = "Fragment では World と Tangent の間だけ移せます";
                };
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

            // ===== Artistic/Normal =====
            {
                NodeDesc desc;
                desc.type = "NormalUnpack";
                desc.title = "Normal Unpack";
                desc.category = "Artistic/Normal";
                desc.summary = "法線マップの色（0〜1）を接空間の向き（-1〜1）にする。"
                               "Texture の Linear にチェックを入れたテクスチャを読んだ色をつなぎます。";
                desc.inputs = { Pin("In", V4, { 0.5f, 0.5f, 1.0f, 1.0f }) };
                desc.outputs = { Pin("Out", V3) };
                desc.emit = Expression("SG_NormalUnpack({0})");
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "NormalStrength";
                desc.title = "Normal Strength";
                desc.category = "Artistic/Normal";
                desc.summary = "接空間の法線の傾きを Strength 倍にする（0 で平ら）。";
                desc.inputs = { Pin("In", V3, { 0.0f, 0.0f, 1.0f, 0.0f }), Pin("Strength", F, { 1.0f, 0.0f, 0.0f, 0.0f }) };
                desc.outputs = { Pin("Out", V3) };
                desc.emit = Expression("SG_NormalStrength({0}, {1})");
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = "NormalBlend";
                desc.title = "Normal Blend";
                desc.category = "Artistic/Normal";
                desc.summary = "接空間の法線 2 つを重ねる（Whiteout・正規化）。";
                desc.inputs = { Pin("A", V3, { 0.0f, 0.0f, 1.0f, 0.0f }), Pin("B", V3, { 0.0f, 0.0f, 1.0f, 0.0f }) };
                desc.outputs = { Pin("Out", V3) };
                desc.emit = Expression("SG_NormalBlend({0}, {1})");
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
                desc.stages = kStageFragment;
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
                desc.stages = kStageFragment;
                desc.emit = PerTarget("SG_Rectangle({0}, {1}, {2})", "SG_RectangleScreen({0}, {1}, {2})");
                nodes.push_back(std::move(desc));
            }

            // ===== Utility =====
            nodes.push_back(Unary("Preview", "Preview", "Utility",
                "値をそのまま渡す。選ぶと Main Preview にこの値を映せます。", "{0}"));
            {
                NodeDesc desc;
                desc.type = kRedirectNodeType;
                desc.title = "Redirect";
                desc.category = "Utility";
                desc.summary = "線の途中の中継点。値をそのまま渡します（テクスチャとサンプラーも通します）。"
                               "線をダブルクリックしても置けます。";
                desc.inputs = { Pin("In", Dyn) };
                desc.outputs = { Pin("Out", Dyn) };
                desc.emit = [](EmitArgs& args) { args.outExpr[0] = args.in[0]; };
                nodes.push_back(std::move(desc));
            }
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

            // ===== サブグラフ（ピンはノードの設定の写しから作る） =====
            {
                NodeDesc desc;
                desc.type = kSubGraphNodeType;
                desc.title = "Sub Graph";
                desc.category = "";
                desc.summary = "サブグラフ（.shadersubgraph）の中のノードを、ここへ展開して使います。ダブルクリックで開きます。";
                desc.creatable = false;
                nodes.push_back(std::move(desc));
            }
            {
                NodeDesc desc;
                desc.type = kSubGraphOutputType;
                desc.title = "Output";
                desc.category = "";
                desc.summary = "サブグラフの出口。ここへつないだ値が、使う側のノードの出力になります。"
                               "出力の足し引きは、選んで Inspector から行います。";
                desc.creatable = false;
                nodes.push_back(std::move(desc));
            }

            // ===== 付箋（検索の窓の下の「付箋を置く」で置く） =====
            {
                NodeDesc desc;
                desc.type = kStickyNoteType;
                desc.title = "Sticky Note";
                desc.category = "";
                desc.summary = "グラフに書くメモ。シェーダには入りません。";
                desc.settings = {
                    SettingDesc{ "Text", "文", {}, "" },
                    SettingDesc{ "Color", "色", { "Yellow", "Blue", "Green", "Pink", "Gray", "Purple" }, "Yellow" },
                    SettingDesc{ "Width", "幅", {}, "200" },
                };
                desc.creatable = false;
                desc.emit = [](EmitArgs&) {};
                nodes.push_back(std::move(desc));
            }

            return nodes;
        }

        /// @brief ノードの設定のピンで作った定義（ピンの名前・表示名・見出しの文字列もここに持つ）
        struct OwnedNodeDesc
        {
            std::deque<std::string> strings;
            NodeDesc desc;

            /// @brief 文字列を持ち、指す view を返す
            std::string_view Keep(std::string text)
            {
                return strings.emplace_back(std::move(text));
            }
        };

        /// @brief サブグラフのピン 1 つの定義（テクスチャとサンプラーは、つながなければ既定のもの）
        PinDesc SubGraphPinDesc(OwnedNodeDesc& owned, const SubGraphPin& pin)
        {
            const ValueType type = ToValueType(pin.type);
            PinBinding binding = PinBinding::Value;
            if (type == Tex) {
                binding = PinBinding::MainTexture;
            } else if (type == Smp) {
                binding = PinBinding::Sampler;
            }
            PinDesc desc = Pin(owned.Keep(pin.key), type, {}, binding);
            desc.label = owned.Keep(pin.label);
            return desc;
        }

        /// @brief パスのファイル名から拡張子を除いたもの
        std::string FileStem(std::string_view path)
        {
            const size_t slash = path.find_last_of("/\\");
            std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
            if (const size_t dot = name.find('.'); dot != std::string_view::npos) {
                name = name.substr(0, dot);
            }
            return std::string(name);
        }

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

    const char* StageName(uint8_t stage)
    {
        return stage == kStageVertex ? "Vertex" : "Fragment";
    }

    std::string EmitArgs::VariableName(size_t output) const
    {
        return prefix + "n" + std::to_string(node.id) + "_" + Identifier(desc.outputs[output].name);
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

    bool HasNodePreview(const NodeDesc& desc)
    {
        constexpr std::string_view kWithoutPreview[] = {
            "Property", "Float", "Vector2", "Vector3", "Vector4", "Color", "Time", kStickyNoteType, kRedirectNodeType,
        };
        if (IsMasterNodeType(desc.type)
            || std::find(std::begin(kWithoutPreview), std::end(kWithoutPreview), desc.type) != std::end(kWithoutPreview)) {
            return false;
        }
        return std::any_of(desc.outputs.begin(), desc.outputs.end(), [](const PinDesc& pin) { return IsNumeric(pin.type); });
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
        if (!base || (node.type != kCustomFunctionType && node.type != kSubGraphNodeType && node.type != kSubGraphOutputType)) {
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
        std::string key = node.type + "\n" + setting("Inputs") + "\n" + setting("Outputs");
        if (node.type == kSubGraphNodeType) {
            key += "\n" + setting("Path") + "\n" + setting("Targets");
        }

        static std::mutex mutex;
        static std::map<std::string, std::unique_ptr<OwnedNodeDesc>> cache;
        std::scoped_lock lock(mutex);
        if (const auto found = cache.find(key); found != cache.end()) {
            return &found->second->desc;
        }
        auto owned = std::make_unique<OwnedNodeDesc>();
        owned->desc = *base;
        owned->desc.inputs.clear();
        owned->desc.outputs.clear();
        if (node.type == kCustomFunctionType) {
            for (const FunctionPin& pin : ParseFunctionPins(setting("Inputs"))) {
                owned->desc.inputs.push_back(FunctionPinDesc(owned->Keep(pin.name), pin.type));
            }
            for (const FunctionPin& pin : ParseFunctionPins(setting("Outputs"))) {
                owned->desc.outputs.push_back(FunctionPinDesc(owned->Keep(pin.name), pin.type));
            }
        } else if (node.type == kSubGraphOutputType) {
            for (const SubGraphPin& pin : ParseSubGraphPins(setting("Outputs"))) {
                owned->desc.inputs.push_back(SubGraphPinDesc(*owned, pin));
            }
        } else {
            for (const SubGraphPin& pin : ParseSubGraphPins(setting("Inputs"))) {
                owned->desc.inputs.push_back(SubGraphPinDesc(*owned, pin));
            }
            for (const SubGraphPin& pin : ParseSubGraphPins(setting("Outputs"))) {
                owned->desc.outputs.push_back(SubGraphPinDesc(*owned, pin));
            }
            const std::string stem = FileStem(setting("Path"));
            if (!stem.empty()) {
                owned->desc.title = owned->Keep(stem);
            }
            owned->desc.targets = ParseTargets(setting("Targets"));
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

    std::vector<SubGraphPin> ParseSubGraphPins(std::string_view text)
    {
        const auto trim = [](std::string_view value) {
            const size_t begin = value.find_first_not_of(" \t\r\n");
            if (begin == std::string_view::npos) {
                return std::string_view();
            }
            const size_t end = value.find_last_not_of(" \t\r\n");
            return value.substr(begin, end - begin + 1);
        };
        constexpr PropertyType kTypes[] = { PropertyType::Float, PropertyType::Vector2, PropertyType::Vector3,
            PropertyType::Vector4, PropertyType::Color, PropertyType::Texture2D, PropertyType::SamplerState };
        std::vector<SubGraphPin> pins;
        std::set<std::string, std::less<>> keys;
        size_t begin = 0;
        while (begin <= text.size() && pins.size() < kMaxSubGraphPins) {
            const size_t end = (std::min)(text.find(';', begin), text.size());
            const std::string_view item = trim(text.substr(begin, end - begin));
            begin = end + 1;
            const size_t typeEnd = item.find_first_of(" \t");
            if (typeEnd == std::string_view::npos) {
                continue;
            }
            const std::string_view typeText = item.substr(0, typeEnd);
            const auto type = std::find_if(std::begin(kTypes), std::end(kTypes),
                [typeText](PropertyType candidate) { return typeText == ToString(candidate); });
            if (type == std::end(kTypes)) {
                continue;
            }
            const std::string_view rest = trim(item.substr(typeEnd + 1));
            const size_t keyEnd = (std::min)(rest.find_first_of(" \t"), rest.size());
            SubGraphPin pin;
            pin.type = *type;
            pin.key = std::string(rest.substr(0, keyEnd));
            pin.label = std::string(trim(rest.substr(keyEnd)));
            if (pin.key.empty() || !keys.insert(pin.key).second) {
                continue;
            }
            if (pin.label.empty()) {
                pin.label = pin.key;
            }
            pins.push_back(std::move(pin));
        }
        return pins;
    }

    std::string FormatSubGraphPins(const std::vector<SubGraphPin>& pins)
    {
        std::string text;
        for (const SubGraphPin& pin : pins) {
            std::string label = pin.label;
            std::erase(label, ';');
            text += std::string(text.empty() ? "" : ";") + ToString(pin.type) + " " + pin.key + " " + label;
        }
        return text;
    }

    ValueType ToValueType(PropertyType type)
    {
        switch (type) {
        case PropertyType::Texture2D: return Tex;
        case PropertyType::SamplerState: return Smp;
        default: return VectorType(WidthOf(type));
        }
    }

    std::string FormatTargets(uint8_t targets)
    {
        std::string text;
        for (const Target target : kAllTargetList) {
            if ((targets & TargetBit(target)) != 0) {
                text += std::string(text.empty() ? "" : ",") + ToString(target);
            }
        }
        return text.empty() ? std::string("None") : text;
    }

    uint8_t ParseTargets(std::string_view text)
    {
        if (text.empty()) {
            return kAllTargets;
        }
        uint8_t targets = 0;
        for (const Target target : kAllTargetList) {
            const std::string_view name = ToString(target);
            for (size_t at = text.find(name); at != std::string_view::npos; at = text.find(name, at + 1)) {
                const size_t end = at + name.size();
                if ((at == 0 || text[at - 1] == ',') && (end == text.size() || text[end] == ',')) {
                    targets |= TargetBit(target);
                }
            }
        }
        return targets;
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
