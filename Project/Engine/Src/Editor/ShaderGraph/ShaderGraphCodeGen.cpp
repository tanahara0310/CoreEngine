#include "pch.h"
#include "ShaderGraphCodeGen.h"

#ifdef CORE_EDITOR

#include "Editor/ShaderGraph/ShaderGraphNodeLibrary.h"

#include <algorithm>
#include <set>

namespace CoreEngine::Editor::ShaderGraph
{
    namespace
    {
        /// @brief つないでよい種類の組か
        bool Compatible(ValueType from, ValueType to)
        {
            if (IsNumeric(from) && IsNumeric(to)) {
                return true;
            }
            return from == to;
        }

        /// @brief 型を決める途中の状態
        class Resolver
        {
        public:
            explicit Resolver(const Graph& graph) : graph_(graph) {}

            Resolution Run()
            {
                for (const Node& node : graph_.nodes) {
                    Visit(node.id);
                }
                return std::move(result_);
            }

        private:
            void Visit(int id)
            {
                if (result_.nodes.contains(id) || visiting_.contains(id)) {
                    return;
                }
                const Node* const node = graph_.FindNode(id);
                if (!node) {
                    return;
                }
                visiting_.insert(id);

                ResolvedNode resolved;
                const NodeDesc* const desc = FindNodeDesc(*node);
                if (!desc) {
                    result_.issues.push_back({ id, "知らないノード（" + node->type + "）です" });
                    visiting_.erase(id);
                    result_.nodes.emplace(id, std::move(resolved));
                    return;
                }

                std::vector<ValueType> sources(desc->inputs.size(), ValueType::Float);
                std::vector<bool> connected(desc->inputs.size(), false);
                for (size_t index = 0; index < desc->inputs.size(); ++index) {
                    const Link* const link = graph_.FindLinkTo(id, desc->inputs[index].name);
                    if (!link) {
                        continue;
                    }
                    if (visiting_.contains(link->fromNode)) {
                        result_.issues.push_back({ id, "つながりが輪になっています" });
                        continue;
                    }
                    Visit(link->fromNode);
                    const auto source = result_.nodes.find(link->fromNode);
                    const Node* const fromNode = graph_.FindNode(link->fromNode);
                    const NodeDesc* const sourceDesc = fromNode ? FindNodeDesc(*fromNode) : nullptr;
                    const int output = sourceDesc ? sourceDesc->FindOutput(link->fromPin) : -1;
                    if (source == result_.nodes.end() || output < 0
                        || static_cast<size_t>(output) >= source->second.outputs.size()) {
                        result_.issues.push_back({ id, std::string(desc->inputs[index].name) + " へのつながりの元のピンがありません" });
                        continue;
                    }
                    const ValueType from = source->second.outputs[static_cast<size_t>(output)];
                    const ValueType pin = desc->inputs[index].type;
                    if (!Compatible(from, pin == ValueType::Dynamic ? ValueType::Float : pin)) {
                        result_.issues.push_back({ id, std::string(desc->inputs[index].name) + " へは、この種類の値をつなげません" });
                        continue;
                    }
                    sources[index] = from;
                    connected[index] = true;
                }

                int dynamicWidth = 1;
                for (size_t index = 0; index < desc->inputs.size(); ++index) {
                    if (desc->inputs[index].type == ValueType::Dynamic && connected[index]) {
                        dynamicWidth = (std::max)(dynamicWidth, WidthOf(sources[index]));
                    }
                }
                resolved.dynamicWidth = dynamicWidth;
                for (const PinDesc& pin : desc->inputs) {
                    resolved.inputs.push_back(pin.type == ValueType::Dynamic ? VectorType(dynamicWidth) : pin.type);
                }
                for (const PinDesc& pin : desc->outputs) {
                    resolved.outputs.push_back(pin.type == ValueType::Dynamic ? VectorType(dynamicWidth) : pin.type);
                }
                if (desc->resolveOutputs) {
                    desc->resolveOutputs(graph_, *node, dynamicWidth, resolved.outputs);
                }

                visiting_.erase(id);
                result_.nodes.emplace(id, std::move(resolved));
            }

            const Graph& graph_;
            Resolution result_;
            std::set<int> visiting_;
        };

        /// @brief 型ごとの 0 の式
        std::string ZeroOf(ValueType type)
        {
            switch (type) {
            case ValueType::Texture2D: return "gTexture";
            case ValueType::SamplerState: return "gSampler";
            default: return Literal({ 0.0f, 0.0f, 0.0f, 0.0f }, WidthOf(type));
            }
        }

        /// @brief つながっていない入力の式
        std::string UnconnectedInput(Target target, const Node& node, const PinDesc& pin, ValueType type)
        {
            const char* const defaultColor = target == Target::PostEffect ? "IN.sceneColor" : "IN.spriteColor";
            switch (pin.binding) {
            case PinBinding::UV:           return Convert("IN.uv", 2, WidthOf(type));
            case PinBinding::MainTexture:  return "gTexture";
            case PinBinding::Sampler:      return SamplerName(target, "Linear Repeat");
            case PinBinding::DefaultRGB:   return Convert(std::string(defaultColor) + ".rgb", 3, WidthOf(type));
            case PinBinding::DefaultAlpha: return Convert(std::string(defaultColor) + ".a", 1, WidthOf(type));
            case PinBinding::Value:        break;
            }
            std::array<float, 4> numbers = pin.fallback;
            if (const auto found = node.values.find(pin.name); found != node.values.end()) {
                numbers = found->second;
            }
            return Literal(numbers, WidthOf(type));
        }

        /// @brief 文字列の行数
        int CountLines(const std::string& text)
        {
            return static_cast<int>(std::count(text.begin(), text.end(), '\n'));
        }

        /// @brief 値を色（float4）にする式
        std::string ToColor(const std::string& expression, int width)
        {
            switch (width) {
            case 1: return "float4(((float3)(" + expression + ")), 1.0f)";
            case 2: return "float4(" + expression + ", 0.0f, 1.0f)";
            case 3: return "float4(" + expression + ", 1.0f)";
            default: return expression;
            }
        }

        /// @brief 定数の型の並び（幅の広い順）
        int PropertyOrder(PropertyType type)
        {
            return 4 - WidthOf(type);
        }
    }

    Resolution Resolve(const Graph& graph)
    {
        return Resolver(graph).Run();
    }

    GenerateResult Generate(const Graph& graph, const GenerateOptions& options)
    {
        GenerateResult result;
        const Resolution resolution = Resolve(graph);
        for (const NodeIssue& issue : resolution.issues) {
            result.issues.push_back(issue);
        }

        // プロパティの名前を確かめる
        std::set<std::string> names;
        for (const Property& property : graph.properties) {
            std::string error;
            if (!IsValidPropertyName(property.name, &error)) {
                result.fatal = "プロパティ「" + property.display + "」: " + error;
                return result;
            }
            if (!names.insert(property.name).second) {
                result.fatal = "プロパティの名前 " + property.name + " が重なっています";
                return result;
            }
        }

        // 出口（または映すノード）から入力をたどって、使うノードを依存の順に並べる
        const Node* root = nullptr;
        if (options.previewNode != 0) {
            root = graph.FindNode(options.previewNode);
        }
        if (!root) {
            root = graph.FindMaster();
        }
        if (!root) {
            result.fatal = "出口のノードがありません";
            return result;
        }
        std::vector<int> order;
        std::set<int> visited;
        std::function<void(int)> visit = [&](int id) {
            if (!visited.insert(id).second) {
                return;
            }
            const Node* const node = graph.FindNode(id);
            const NodeDesc* const desc = node ? FindNodeDesc(*node) : nullptr;
            if (desc) {
                for (const PinDesc& pin : desc->inputs) {
                    if (const Link* const link = graph.FindLinkTo(id, pin.name)) {
                        visit(link->fromNode);
                    }
                }
            }
            order.push_back(id);
        };
        visit(root->id);

        // 出力の式（ノード・出力の番号 → 式）
        std::map<std::pair<int, int>, std::string> outputs;
        const auto inputExpression = [&](const Node& node, const NodeDesc& desc, const ResolvedNode& resolved,
                                         size_t index, bool& connected) -> std::string {
            const PinDesc& pin = desc.inputs[index];
            const ValueType type = resolved.inputs[index];
            connected = false;
            if (const Link* const link = graph.FindLinkTo(node.id, pin.name)) {
                const Node* const source = graph.FindNode(link->fromNode);
                const NodeDesc* const sourceDesc = source ? FindNodeDesc(*source) : nullptr;
                const int output = sourceDesc ? sourceDesc->FindOutput(link->fromPin) : -1;
                const auto expression = outputs.find({ link->fromNode, output });
                const auto sourceResolved = resolution.nodes.find(link->fromNode);
                if (expression != outputs.end() && sourceResolved != resolution.nodes.end()
                    && output >= 0 && static_cast<size_t>(output) < sourceResolved->second.outputs.size()) {
                    const ValueType from = sourceResolved->second.outputs[static_cast<size_t>(output)];
                    if (Compatible(from, IsNumeric(type) ? ValueType::Float : type)) {
                        connected = true;
                        return IsNumeric(type) ? Convert(expression->second, WidthOf(from), WidthOf(type)) : expression->second;
                    }
                }
            }
            return UnconnectedInput(graph.target, node, pin, type);
        };

        std::string body;
        std::vector<std::pair<int, std::pair<int, int>>> bodyLines;
        std::vector<std::pair<int, GlobalCode>> globals; ///< 関数の外に置く文と、置いたノード
        for (const int id : order) {
            const Node* const node = graph.FindNode(id);
            const NodeDesc* const desc = node ? FindNodeDesc(*node) : nullptr;
            const auto resolved = resolution.nodes.find(id);
            if (!node || !desc || resolved == resolution.nodes.end() || node->type == kMasterNodeType) {
                continue;
            }

            EmitArgs args{ graph, *node, *desc };
            args.target = graph.target;
            args.resolveInclude = options.resolveInclude;
            args.dynamicWidth = resolved->second.dynamicWidth;
            args.inType = resolved->second.inputs;
            args.outType = resolved->second.outputs;
            args.outExpr.assign(desc->outputs.size(), std::string());
            for (size_t index = 0; index < desc->inputs.size(); ++index) {
                bool connected = false;
                args.in.push_back(inputExpression(*node, *desc, resolved->second, index, connected));
                args.connected.push_back(connected);
            }
            if (!desc->Supports(graph.target)) {
                args.error = std::string("このノードは") + DisplayName(graph.target) + "では使えません";
            } else if (desc->emit) {
                desc->emit(args);
            }
            for (const GlobalCode& global : args.globals) {
                if (!args.error.empty()) {
                    break;
                }
                const auto same = std::find_if(globals.begin(), globals.end(),
                    [&](const auto& placed) { return placed.second.key == global.key; });
                if (same != globals.end() && same->second.code != global.code) {
                    args.error = "同じ関数（" + global.key.substr(global.key.find(':') + 1)
                        + "）を、中身の違う別のノードでも作っています";
                }
            }
            if (args.error.empty()) {
                for (const GlobalCode& global : args.globals) {
                    const bool placed = std::any_of(globals.begin(), globals.end(),
                        [&](const auto& item) { return item.second.key == global.key; });
                    if (!placed) {
                        globals.push_back({ id, global });
                    }
                }
            }
            if (!args.error.empty()) {
                result.issues.push_back({ id, args.error });
                args.code.clear();
                for (size_t index = 0; index < desc->outputs.size(); ++index) {
                    args.Out(index, ZeroOf(args.outType[index]));
                }
            }

            const int first = CountLines(body) + 1;
            body += "    // " + std::to_string(id) + " " + std::string(desc->title) + "\n";
            body += args.code;
            bodyLines.push_back({ id, { first, CountLines(body) } });
            for (size_t index = 0; index < desc->outputs.size(); ++index) {
                outputs[{ id, static_cast<int>(index) }] = args.outExpr[index];
            }
        }

        // 出口の式
        std::string color;
        if (root->type == kMasterNodeType) {
            const NodeDesc* const desc = FindNodeDesc(kMasterNodeType);
            const ResolvedNode& resolved = resolution.nodes.at(root->id);
            bool connected = false;
            const std::string baseColor = inputExpression(*root, *desc, resolved, 0, connected);
            const std::string alpha = inputExpression(*root, *desc, resolved, 1, connected);
            body += std::string(body.empty() ? "" : "\n") + "    float3 baseColor = " + baseColor + ";\n";
            body += "    float alpha = " + alpha + ";\n";
            color = "float4(baseColor, alpha)";
        } else {
            const NodeDesc* const desc = FindNodeDesc(*root);
            const ResolvedNode& resolved = resolution.nodes.at(root->id);
            int output = -1;
            if (desc && !options.previewPin.empty()) {
                output = desc->FindOutput(options.previewPin);
            }
            if (output < 0) {
                for (size_t index = 0; index < resolved.outputs.size(); ++index) {
                    if (IsNumeric(resolved.outputs[index])) {
                        output = static_cast<int>(index);
                        break;
                    }
                }
            }
            if (output < 0 || !IsNumeric(resolved.outputs[static_cast<size_t>(output)])) {
                result.fatal = "このノードには映せる出力がありません";
                return result;
            }
            color = ToColor(outputs[{ root->id, output }], WidthOf(resolved.outputs[static_cast<size_t>(output)]));
        }

        // ファイルの頭（プロパティの表・定数・テクスチャ）
        std::string head;
        head += std::string(kGeneratedMarker) + options.sourcePath + ". Changes are overwritten when the graph is saved.\n";
        for (const Property& property : graph.properties) {
            head += std::string(kPropertyLinePrefix) + property.name + " " + ToString(property.type);
            if (property.type == PropertyType::Color && property.hdr) {
                head += " hdr";
            }
            if (property.hasRange) {
                head += " range " + FormatFloat(property.rangeMin) + " " + FormatFloat(property.rangeMax);
            }
            if (property.type == PropertyType::Texture2D && property.linear) {
                head += " linear";
            }
            head += "\n";
        }
        const bool postEffect = graph.target == Target::PostEffect;
        head += postEffect ? "#include \"PostEffectMaterial.hlsli\"\n" : "#include \"SpriteMaterial.hlsli\"\n";
        head += "#include \"ShaderGraphLibrary.hlsli\"\n";

        std::vector<const Property*> numeric;
        std::vector<const Property*> textures;
        for (const Property& property : graph.properties) {
            (property.type == PropertyType::Texture2D ? textures : numeric).push_back(&property);
        }
        std::stable_sort(numeric.begin(), numeric.end(), [](const Property* a, const Property* b) {
            return PropertyOrder(a->type) < PropertyOrder(b->type);
        });
        if (!numeric.empty()) {
            head += "\nstruct GraphProperties\n{\n";
            for (const Property* property : numeric) {
                head += "    " + TypeName(WidthOf(property->type)) + " " + property->name + ";\n";
            }
            head += "};\nConstantBuffer<GraphProperties> gMaterial : register(b0);\n";
        }
        if (!textures.empty()) {
            head += "\n";
            for (size_t index = 0; index < textures.size(); ++index) {
                head += "Texture2D<float4> " + textures[index]->name + " : register(t" + std::to_string(index + 1) + ");\n";
            }
        }

        for (const auto& [id, global] : globals) {
            head += "\n";
            const int first = CountLines(head) + 1;
            head += global.code;
            result.globalLines[id] = { first, CountLines(head) };
        }

        if (postEffect) {
            head += R"(
struct GraphInput
{
    float2 uv;
    float4 sceneColor;
    float2 screenSize;
    float2 invScreenSize;
    float  time;
    float  unscaledTime;
    float  deltaTime;
};

POST_EFFECT_THREADS
void main(uint3 id : SV_DispatchThreadID)
{
    if (IsOutsideScreen(id.xy)) {
        return;
    }
    GraphInput IN;
    IN.uv = PostEffectUV(id.xy);
    IN.sceneColor = LoadScreen(int2(id.xy));
    IN.screenSize = float2(gPostEffect.screenSize);
    IN.invScreenSize = gPostEffect.invScreenSize;
    IN.time = gPostEffect.time;
    IN.unscaledTime = gPostEffect.unscaledTime;
    IN.deltaTime = gPostEffect.deltaTime;

)";
        } else {
            head += R"(
struct GraphInput
{
    float2 uv;
    float3 positionWS;
    float4 spriteColor;
    float4 vertexColor;
    float2 textureSize;
    float  time;
    float  unscaledTime;
    float  deltaTime;
};

float4 main(SpriteVertexOutput input) : SV_TARGET0
{
    GraphInput IN;
    IN.uv = SpriteUV(input.texcoord);
    IN.positionWS = input.worldPos;
    IN.spriteColor = SpriteDefaultColor(input);
    IN.vertexColor = SpriteColor();
    IN.textureSize = gSpriteDraw.textureSize;
    IN.time = gSpriteDraw.time;
    IN.unscaledTime = gSpriteDraw.unscaledTime;
    IN.deltaTime = gSpriteDraw.deltaTime;

)";
        }

        const int offset = CountLines(head);
        for (const auto& [id, lines] : bodyLines) {
            result.nodeLines[id] = { lines.first + offset, lines.second + offset };
        }
        if (postEffect) {
            result.hlsl = head + body + "    gOutput[id.xy] = " + color + ";\n}\n";
        } else {
            result.hlsl = head + body + "    return " + color + ";\n}\n";
        }
        return result;
    }
}

#endif // CORE_EDITOR
