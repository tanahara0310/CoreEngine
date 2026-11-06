#include "pch.h"
#include "ShaderGraphCodeGen.h"

#ifdef CORE_EDITOR

#include "Editor/ShaderGraph/ShaderGraphNodeLibrary.h"

#include <algorithm>
#include <functional>
#include <map>
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

                const bool redirect = node->type == kRedirectNodeType;
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
                    if (!redirect && !Compatible(from, pin == ValueType::Dynamic ? ValueType::Float : pin)) {
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
                if (redirect && !resolved.inputs.empty() && !resolved.outputs.empty()) {
                    // 中継点は、つながった値の種類（テクスチャとサンプラーも）をそのまま渡す
                    resolved.inputs[0] = connected[0] ? sources[0] : ValueType::Float;
                    resolved.outputs[0] = resolved.inputs[0];
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
        std::string UnconnectedInput(Target target, uint8_t stage, const Node& node, const PinDesc& pin, ValueType type)
        {
            const bool model = Is3D(target);
            const bool vertex = model && stage == kStageVertex;
            const char* const defaultColor = target == Target::PostEffect ? "IN.sceneColor" : "IN.spriteColor";
            switch (pin.binding) {
            case PinBinding::UV:           return Convert("IN.uv", 2, WidthOf(type));
            case PinBinding::MainTexture:  return "gTexture";
            case PinBinding::Sampler:      return SamplerName(target, "Linear Repeat");
            case PinBinding::DefaultRGB:
                return Convert(model ? std::string("OUT.baseColor") : std::string(defaultColor) + ".rgb", 3, WidthOf(type));
            case PinBinding::DefaultAlpha:
                return Convert(model ? std::string("OUT.alpha") : std::string(defaultColor) + ".a", 1, WidthOf(type));
            case PinBinding::WorldNormal:
                if (model) {
                    return Convert(vertex ? "normalize(mul((float3x3)IN.worldToObject, IN.normalOS))" : "normalize(IN.normalWS)",
                        3, WidthOf(type));
                }
                break;
            case PinBinding::ViewDirection:
                if (model && !vertex) {
                    return Convert("SurfaceViewDirection(IN)", 3, WidthOf(type));
                }
                break;
            case PinBinding::WorldPosition:
                if (model) {
                    return Convert(vertex ? "mul(float4(IN.positionOS, 1.0f), IN.objectToWorld).xyz" : "IN.positionWS",
                        3, WidthOf(type));
                }
                break;
            case PinBinding::ScreenUV:
                if (target == Target::PostEffect) {
                    return Convert("IN.uv", 2, WidthOf(type));
                }
                return Convert(model ? "SceneScreenUV(IN.screenPosition)" : "SceneScreenUV(input.position.xy)", 2,
                    WidthOf(type));
            case PinBinding::RequiredTexture:
                return "gTexture";
            case PinBinding::Value:
            case PinBinding::ModelValue:
            case PinBinding::SourceVertex:
                break;
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

        /// @brief 出口の欄と、代入する `OUT` の名前
        struct MasterField
        {
            std::string_view pin;
            std::string_view field;
            bool tangentNormal = false; ///< 接空間の法線をワールドへ直して代入する
        };

        constexpr MasterField kFragmentFields[] = {
            { "Base Color", "baseColor" },
            { "Normal (Tangent Space)", "normalWS", true },
            { "Metallic", "metallic" },
            { "Roughness", "roughness" },
            { "Ambient Occlusion", "occlusion" },
            { "Emission", "emission" },
            { "Alpha", "alpha" },
        };

        constexpr MasterField kVertexFields[] = {
            { "Position", "positionOS" },
            { "Normal", "normalOS" },
        };

        /// @brief root から入力をたどって、使うノードを依存の順に並べる（root では rootPin が true のピンだけたどる）
        std::vector<int> CollectOrder(const Graph& graph, const Node& root, const std::function<bool(const PinDesc&)>& rootPin)
        {
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
                        if (id == root.id && rootPin && !rootPin(pin)) {
                            continue;
                        }
                        if (const Link* const link = graph.FindLinkTo(id, pin.name)) {
                            visit(link->fromNode);
                        }
                    }
                }
                order.push_back(id);
            };
            visit(root.id);
            return order;
        }

        /// @brief いくつかの根から入力をたどって、使うノードを依存の順に並べる（共有するノードは 1 回）
        std::vector<int> CollectOrder(const Graph& graph, const std::vector<const Node*>& roots)
        {
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
            for (const Node* root : roots) {
                visit(root->id);
            }
            return order;
        }

        /// @brief ノードの番号の並び
        std::vector<int> NodeIds(const std::vector<const Node*>& nodes)
        {
            std::vector<int> ids;
            for (const Node* node : nodes) {
                ids.push_back(node->id);
            }
            return ids;
        }

        /// @brief 並べたノードが 3D の値（位置・法線・接線・視線・カメラ・モデルの面・Parallax）を使うか
        bool Uses3DInput(const Graph& graph, const std::vector<int>& order)
        {
            for (const int id : order) {
                const Node* const node = graph.FindNode(id);
                const NodeDesc* const desc = node ? FindNodeDesc(*node) : nullptr;
                if (!desc) {
                    continue;
                }
                if (desc->category.starts_with("Input/Geometry") || desc->category.starts_with("Input/Model")
                    || desc->type == "Camera" || desc->type == "ParallaxMapping" || desc->type == "ParallaxOcclusionMapping") {
                    return true;
                }
                for (const PinDesc& pin : desc->inputs) {
                    if ((pin.binding == PinBinding::WorldNormal || pin.binding == PinBinding::ViewDirection
                            || pin.binding == PinBinding::WorldPosition)
                        && !graph.FindLinkTo(id, pin.name)) {
                        return true;
                    }
                }
            }
            return false;
        }

        /// @brief 関数 1 つ（スプライト・ポストエフェクトの main、3D の SurfaceVertex か Surface）のノードの文を作る
        class FunctionWriter
        {
        public:
            FunctionWriter(const Graph& graph, const Resolution& resolution, const GenerateOptions& options, uint8_t stage,
                std::vector<std::pair<int, GlobalCode>>& globals, std::vector<NodeIssue>& issues)
                : graph_(graph), resolution_(resolution), options_(options), stage_(stage), target_(graph.target),
                  globals_(globals), issues_(issues)
            {
            }

            /// @brief 入力ピンの式（つながっていなければ目標と段で決まる既定）
            std::string Input(const Node& node, const NodeDesc& desc, const ResolvedNode& resolved, size_t index,
                bool& connected) const
            {
                const PinDesc& pin = desc.inputs[index];
                const ValueType type = resolved.inputs[index];
                connected = false;
                if (const Link* const link = graph_.FindLinkTo(node.id, pin.name)) {
                    const Node* const source = graph_.FindNode(link->fromNode);
                    const NodeDesc* const sourceDesc = source ? FindNodeDesc(*source) : nullptr;
                    const int output = sourceDesc ? sourceDesc->FindOutput(link->fromPin) : -1;
                    const auto expression = outputs_.find({ link->fromNode, output });
                    const auto sourceResolved = resolution_.nodes.find(link->fromNode);
                    if (expression != outputs_.end() && sourceResolved != resolution_.nodes.end()
                        && output >= 0 && static_cast<size_t>(output) < sourceResolved->second.outputs.size()) {
                        const ValueType from = sourceResolved->second.outputs[static_cast<size_t>(output)];
                        if (Compatible(from, IsNumeric(type) ? ValueType::Float : type)) {
                            connected = true;
                            return IsNumeric(type) ? Convert(expression->second, WidthOf(from), WidthOf(type)) : expression->second;
                        }
                    }
                }
                return UnconnectedInput(target_, stage_, node, pin, type);
            }

            /// @brief 並べたノードの文を書く（出口のノードは飛ばす）
            /// @param roots 並べたときの根（根のサブグラフのノードは全部の出力を書く）
            /// @param rootPin 根でたどったピン（空なら全部）
            void Write(const std::vector<int>& order, const std::vector<int>& roots,
                const std::function<bool(const PinDesc&)>& rootPin = nullptr)
            {
                const std::set<int> written(order.begin(), order.end());
                for (const int id : order) {
                    const Node* const node = graph_.FindNode(id);
                    const NodeDesc* const desc = node ? FindNodeDesc(*node) : nullptr;
                    const auto resolved = resolution_.nodes.find(id);
                    if (!node || !desc || resolved == resolution_.nodes.end() || IsMasterNodeType(node->type)) {
                        continue;
                    }

                    EmitArgs args{ graph_, *node, *desc };
                    args.target = target_;
                    args.stage = stage_;
                    args.resolveInclude = options_.resolveInclude;
                    args.prefix = prefix_;
                    args.dynamicWidth = resolved->second.dynamicWidth;
                    args.inType = resolved->second.inputs;
                    args.outType = resolved->second.outputs;
                    args.outExpr.assign(desc->outputs.size(), std::string());
                    for (size_t index = 0; index < desc->inputs.size(); ++index) {
                        bool connected = false;
                        args.in.push_back(Input(*node, *desc, resolved->second, index, connected));
                        args.connected.push_back(connected);
                    }
                    if (!desc->Supports(target_)) {
                        args.error = std::string("このノードは") + DisplayName(target_) + "では使えません";
                    } else if (Is3D(target_) && !desc->SupportsStage(stage_)) {
                        args.error = std::string("このノードは ") + StageName(stage_) + " では使えません";
                    } else if (properties_ && node->type == "Property") {
                        const auto found = properties_->find(node->property);
                        if (found == properties_->end()) {
                            args.error = "指しているプロパティがありません";
                        } else if (!args.outExpr.empty()) {
                            args.outExpr[0] = found->second;
                        }
                    } else if (node->type == kSubGraphNodeType) {
                        WriteSubGraph(args, UsedOutputs(*node, *desc, written, roots, rootPin));
                    } else if (desc->emit) {
                        desc->emit(args);
                    }
                    for (const GlobalCode& global : args.globals) {
                        if (!args.error.empty()) {
                            break;
                        }
                        const auto same = std::find_if(globals_.begin(), globals_.end(),
                            [&](const auto& placed) { return placed.second.key == global.key; });
                        if (same != globals_.end() && same->second.code != global.code) {
                            args.error = "同じ関数（" + global.key.substr(global.key.find(':') + 1)
                                + "）を、中身の違う別のノードでも作っています";
                        }
                    }
                    if (args.error.empty()) {
                        uses_ |= args.uses;
                        for (const GlobalCode& global : args.globals) {
                            const bool placed = std::any_of(globals_.begin(), globals_.end(),
                                [&](const auto& item) { return item.second.key == global.key; });
                            if (!placed) {
                                globals_.push_back({ id, global });
                            }
                        }
                    }
                    if (!args.error.empty()) {
                        ++errors_;
                        const bool reported = std::any_of(issues_.begin(), issues_.end(),
                            [&](const NodeIssue& issue) { return issue.node == id && issue.message == args.error; });
                        if (!reported) {
                            issues_.push_back({ id, args.error });
                        }
                        args.code.clear();
                        for (size_t index = 0; index < desc->outputs.size(); ++index) {
                            args.Out(index, ZeroOf(args.outType[index]));
                        }
                    }

                    if (!args.code.empty() || (prefix_.empty() && node->type != kRedirectNodeType)) {
                        const int first = CountLines(body_) + 1;
                        body_ += "    // " + label_ + std::to_string(id) + " " + std::string(desc->title) + "\n";
                        body_ += args.code;
                        lines_.push_back({ id, { first, CountLines(body_) } });
                    }
                    for (size_t index = 0; index < desc->outputs.size(); ++index) {
                        outputs_[{ id, static_cast<int>(index) }] = args.outExpr[index];
                    }
                }
            }

            /// @brief ノードの出力の式（無ければ空）
            std::string Output(int node, int output) const
            {
                const auto found = outputs_.find({ node, output });
                return found != outputs_.end() ? found->second : std::string();
            }

            const std::string& Body() const { return body_; }

            /// @brief 書いた文の行を、関数の本文の前の行数 offset だけずらして result へ足す
            void AddLines(std::multimap<int, std::pair<int, int>>& result, int offset) const
            {
                for (const auto& [id, lines] : lines_) {
                    result.emplace(id, std::pair<int, int>{ lines.first + offset, lines.second + offset });
                }
            }

            /// @brief 作れなかったノードの数
            int ErrorCount() const { return errors_; }

            /// @brief 書いたノードが 3D で使った値の印（`kUsesObjectSpace`・`kUsesVertexColor`）
            uint8_t Uses() const { return uses_; }

        private:
            /// @brief この関数で使うサブグラフのノードの出力（根なら全部・ほかは書くノードへ入る線の元）
            std::set<std::string, std::less<>> UsedOutputs(const Node& node, const NodeDesc& desc, const std::set<int>& written,
                const std::vector<int>& roots, const std::function<bool(const PinDesc&)>& rootPin) const
            {
                std::set<std::string, std::less<>> used;
                if (std::find(roots.begin(), roots.end(), node.id) != roots.end()) {
                    for (const PinDesc& pin : desc.outputs) {
                        used.emplace(pin.name);
                    }
                    return used;
                }
                for (const Link& link : graph_.links) {
                    if (link.fromNode != node.id || !written.contains(link.toNode)) {
                        continue;
                    }
                    if (rootPin && std::find(roots.begin(), roots.end(), link.toNode) != roots.end()) {
                        const Node* const target = graph_.FindNode(link.toNode);
                        const NodeDesc* const targetDesc = target ? FindNodeDesc(*target) : nullptr;
                        const int index = targetDesc ? targetDesc->FindInput(link.toPin) : -1;
                        if (index < 0 || !rootPin(targetDesc->inputs[static_cast<size_t>(index)])) {
                            continue;
                        }
                    }
                    used.insert(link.fromPin);
                }
                return used;
            }

            /// @brief サブグラフの中のノードの問題を、使う側のノードの問題の文にする
            static std::string DescribeInnerIssue(const Graph& subGraph, std::string_view name, const NodeIssue& issue)
            {
                if (issue.node == 0) {
                    return std::string(name) + ": " + issue.message;
                }
                const Node* const node = subGraph.FindNode(issue.node);
                const NodeDesc* const desc = node ? FindNodeDesc(*node) : nullptr;
                const std::string title = desc ? std::string(desc->title) : (node ? node->type : std::string("?"));
                return std::string(name) + " の " + title + "（" + std::to_string(issue.node) + "）: " + issue.message;
            }

            /// @brief サブグラフのノード: 使う出力へつながる中のノードを前置きを付けてこの関数へ書き、中の出口の式を出力にする
            /// @details 中のノードの段の検査は中のノードで行う（Vertex だけの出力と Fragment だけの出力を 1 つのサブグラフに持てる）
            void WriteSubGraph(EmitArgs& args, const std::set<std::string, std::less<>>& used)
            {
                const Node& node = args.node;
                const NodeDesc& desc = args.desc;
                const std::string guid = args.Setting("Guid");
                const std::string path = args.Setting("Path");
                const std::string identity = guid.empty() ? path : guid;
                if (identity.empty()) {
                    args.error = "サブグラフを指していません";
                    return;
                }
                if (std::find(chain_.begin(), chain_.end(), identity) != chain_.end()) {
                    args.error = "サブグラフが自分を含んでいます";
                    return;
                }
                std::string error;
                const std::shared_ptr<const Graph> subGraph =
                    options_.resolveSubGraph ? options_.resolveSubGraph(guid, path, error) : nullptr;
                if (!subGraph) {
                    args.error = error.empty() ? "サブグラフ " + path + " が見つかりません" : error;
                    return;
                }
                const Node* const output = subGraph->FindSubGraphOutput();
                const NodeDesc* const outputDesc = output ? FindNodeDesc(*output) : nullptr;
                if (!output || !outputDesc) {
                    args.error = "サブグラフに出口がありません";
                    return;
                }

                // 入力の式（中の Property ノードの値。つないでいない数は、ノードの値か、無ければサブグラフの既定値）
                std::map<std::string, std::string, std::less<>> inputs;
                for (const Property& property : subGraph->properties) {
                    const ValueType type = ToValueType(property.type);
                    const int index = desc.FindInput(property.id);
                    std::string expression;
                    if (index >= 0) {
                        const size_t at = static_cast<size_t>(index);
                        const ValueType from = args.inType[at];
                        if (IsNumeric(type) && IsNumeric(from)) {
                            if (args.connected[at] || node.values.contains(property.id)) {
                                expression = Convert(args.in[at], WidthOf(from), WidthOf(type));
                            }
                        } else if (type == from) {
                            expression = args.in[at];
                        }
                    }
                    if (expression.empty()) {
                        if (type == ValueType::Texture2D) {
                            expression = "gTexture";
                        } else if (type == ValueType::SamplerState) {
                            expression = SamplerName(target_, "Linear Repeat");
                        } else {
                            expression = Literal(property.value, WidthOf(type));
                        }
                    }
                    inputs.emplace(property.id, std::move(expression));
                }

                // 中のノード（出口からたどったもの）
                const Resolution resolution = Resolve(*subGraph);
                std::vector<std::pair<int, GlobalCode>> globals = globals_;
                const size_t placed = globals.size();
                std::vector<NodeIssue> issues;
                FunctionWriter inner(*subGraph, resolution, options_, stage_, globals, issues);
                inner.target_ = target_;
                inner.prefix_ = prefix_ + "n" + std::to_string(node.id) + "_";
                inner.label_ = label_ + std::to_string(node.id) + "/";
                inner.properties_ = &inputs;
                inner.chain_ = chain_;
                inner.chain_.push_back(identity);
                const auto usedPin = [&used](const PinDesc& pin) { return used.contains(pin.name); };
                const std::vector<int> order = CollectOrder(*subGraph, *output, usedPin);
                inner.Write(order, { output->id }, usedPin);
                for (const NodeIssue& issue : resolution.issues) {
                    if (std::find(order.begin(), order.end(), issue.node) != order.end()) {
                        issues.push_back(issue);
                    }
                }

                // 中の出口の欄の式を、写しの出力の種類へ直す
                std::vector<std::string> outputs(desc.outputs.size());
                const ResolvedNode& outputResolved = resolution.nodes.at(output->id);
                for (size_t index = 0; index < desc.outputs.size() && issues.empty(); ++index) {
                    const PinDesc& pin = desc.outputs[index];
                    if (!used.contains(pin.name)) {
                        outputs[index] = ZeroOf(args.outType[index]);
                        continue;
                    }
                    const int source = outputDesc->FindInput(pin.name);
                    if (source < 0) {
                        issues.push_back({ 0, "出力 " + std::string(pin.Label()) + " がサブグラフにありません" });
                        break;
                    }
                    bool connected = false;
                    std::string expression = inner.Input(*output, *outputDesc, outputResolved, static_cast<size_t>(source),
                        connected);
                    const ValueType from = outputResolved.inputs[static_cast<size_t>(source)];
                    const ValueType to = args.outType[index];
                    if (IsNumeric(from) && IsNumeric(to)) {
                        expression = Convert(expression, WidthOf(from), WidthOf(to));
                    } else if (from != to) {
                        issues.push_back({ 0, "出力 " + std::string(pin.Label()) + " の種類がサブグラフと違います" });
                        break;
                    }
                    outputs[index] = std::move(expression);
                }

                if (!issues.empty()) {
                    for (const NodeIssue& issue : issues) {
                        const std::string message = DescribeInnerIssue(*subGraph, desc.title, issue);
                        const bool reported = std::any_of(issues_.begin(), issues_.end(),
                            [&](const NodeIssue& item) { return item.node == node.id && item.message == message; });
                        if (!reported) {
                            issues_.push_back({ node.id, message });
                        }
                    }
                    args.error = DescribeInnerIssue(*subGraph, desc.title, issues.front());
                    return;
                }
                args.code += inner.Body();
                for (size_t index = 0; index < outputs.size(); ++index) {
                    args.outExpr[index] = std::move(outputs[index]);
                }
                for (size_t index = placed; index < globals.size(); ++index) {
                    args.globals.push_back(globals[index].second);
                }
                args.uses |= inner.Uses();
            }

            const Graph& graph_;
            const Resolution& resolution_;
            const GenerateOptions& options_;
            uint8_t stage_ = kStageFragment;
            Target target_ = Target::Sprite; ///< 書いている目標（サブグラフの中も使う側の目標）
            std::vector<std::pair<int, GlobalCode>>& globals_;
            std::vector<NodeIssue>& issues_;
            std::map<std::pair<int, int>, std::string> outputs_; ///< ノード・出力の番号 → 式
            std::string body_;
            std::vector<std::pair<int, std::pair<int, int>>> lines_;
            int errors_ = 0;
            uint8_t uses_ = 0; ///< 書いたノードが 3D で使った値の印
            std::string prefix_; ///< 変数の名前の前置き（サブグラフの中は `n使う側の番号_`）
            std::string label_;  ///< 文の前の注釈の番号の前置き（サブグラフの中は `使う側の番号/`）
            const std::map<std::string, std::string, std::less<>>* properties_ = nullptr; ///< サブグラフの中: 入力の id → 式
            std::vector<std::string> chain_; ///< 展開している途中のサブグラフ（輪の判定）
        };

        /// @brief 映すノードの、色にする出力の式（無ければ空）
        std::string PreviewColor(const FunctionWriter& writer, const Resolution& resolution, const GenerateOptions& options,
            const Node& root)
        {
            const NodeDesc* const desc = FindNodeDesc(root);
            const ResolvedNode& resolved = resolution.nodes.at(root.id);
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
                return {};
            }
            return ToColor(writer.Output(root.id, output), WidthOf(resolved.outputs[static_cast<size_t>(output)]));
        }

        /// @brief ファイルの先頭の行（生成物の印とプロパティの表）
        std::string HeaderLines(const Graph& graph, const GenerateOptions& options)
        {
            std::string head;
            head += std::string(kGeneratedMarker) + options.sourcePath + ". Changes are overwritten when the graph is saved.\n";
            for (const Property& property : graph.properties) {
                if (property.type == PropertyType::SamplerState) {
                    continue;
                }
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
            return head;
        }

        /// @brief プロパティの定数バッファとテクスチャの宣言
        std::string PropertyDeclarations(const Graph& graph, std::string_view bufferName, int bufferRegister, int firstTexture)
        {
            std::vector<const Property*> numeric;
            std::vector<const Property*> textures;
            for (const Property& property : graph.properties) {
                if (property.type != PropertyType::SamplerState) {
                    (property.type == PropertyType::Texture2D ? textures : numeric).push_back(&property);
                }
            }
            std::stable_sort(numeric.begin(), numeric.end(), [](const Property* a, const Property* b) {
                return PropertyOrder(a->type) < PropertyOrder(b->type);
            });
            std::string text;
            if (!numeric.empty()) {
                text += "\nstruct GraphProperties\n{\n";
                for (const Property* property : numeric) {
                    text += "    " + TypeName(WidthOf(property->type)) + " " + property->name + ";\n";
                }
                text += "};\nConstantBuffer<GraphProperties> " + std::string(bufferName) + " : register(b"
                    + std::to_string(bufferRegister) + ");\n";
            }
            if (!textures.empty()) {
                text += "\n";
                for (size_t index = 0; index < textures.size(); ++index) {
                    text += "Texture2D<float4> " + textures[index]->name + " : register(t"
                        + std::to_string(static_cast<size_t>(firstTexture) + index) + ");\n";
                }
            }
            return text;
        }

        /// @brief 関数の外に置く文を並べる
        void AppendGlobals(std::string& head, const std::vector<std::pair<int, GlobalCode>>& globals, GenerateResult& result)
        {
            for (const auto& [id, global] : globals) {
                head += "\n";
                const int first = CountLines(head) + 1;
                head += global.code;
                result.globalLines[id] = { first, CountLines(head) };
            }
        }

        /// @brief スプライトとポストエフェクトのファイルの頭（プロパティの表・include・定数・テクスチャ・関数の外の文）
        std::string FlatHead(const Graph& graph, const GenerateOptions& options,
            const std::vector<std::pair<int, GlobalCode>>& globals, GenerateResult& result)
        {
            std::string head = HeaderLines(graph, options);
            head += graph.target == Target::PostEffect ? "#include \"PostEffectMaterial.hlsli\"\n" : "#include \"SpriteMaterial.hlsli\"\n";
            head += "#include \"ShaderGraphLibrary.hlsli\"\n";
            head += PropertyDeclarations(graph, "gMaterial", 0, 1);
            AppendGlobals(head, globals, result);
            return head;
        }

        /// @brief 3D のファイルの頭（プロパティの表・define・include・定数・テクスチャ・関数の外の文）
        /// @param uses ノードが使った値の印（`kUsesObjectSpace`・`kUsesVertexColor`）
        std::string SurfaceHead(const Graph& graph, const GenerateOptions& options,
            const std::vector<std::pair<int, GlobalCode>>& globals, GenerateResult& result, bool lit, bool hasVertex,
            uint8_t uses)
        {
            std::string head = HeaderLines(graph, options);
            head += lit ? "#define SURFACE_LIT\n" : "#define SURFACE_UNLIT\n";
            if (hasVertex) {
                head += "#define SURFACE_VERTEX\n";
            }
            if ((uses & kUsesObjectSpace) != 0) {
                head += "#define SURFACE_OBJECT_SPACE\n";
            }
            if ((uses & kUsesVertexColor) != 0) {
                head += "#define SURFACE_VERTEX_COLOR\n";
            }
            head += "#include \"ModelSurface.hlsli\"\n";
            head += "#include \"ShaderGraphLibrary.hlsli\"\n";
            head += PropertyDeclarations(graph, "gGraph", kSurfaceGraphConstantRegister, kSurfaceGraphFirstTexture);
            AppendGlobals(head, globals, result);
            return head;
        }

        /// @brief ポストエフェクトの GraphInput と main の始め（IN を宣言するところまで）
        constexpr std::string_view kPostEffectMainBegin = R"(
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
)";

        /// @brief ポストエフェクトの IN の画面の値（この画素）
        constexpr std::string_view kPostEffectScreenInputs = R"(    IN.uv = PostEffectUV(id.xy);
    IN.sceneColor = LoadScreen(int2(id.xy));
    IN.screenSize = float2(gPostEffect.screenSize);
    IN.invScreenSize = gPostEffect.invScreenSize;
)";

        /// @brief ポストエフェクトの IN の時間
        constexpr std::string_view kPostEffectTimeInputs = R"(    IN.time = gPostEffect.time;
    IN.unscaledTime = gPostEffect.unscaledTime;
    IN.deltaTime = gPostEffect.deltaTime;

)";

        /// @brief スプライトの GraphInput と main の始め（IN を全部置くところまで）
        constexpr std::string_view kSpriteMainBegin = R"(
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

        /// @brief 3D のモデルのマテリアル（`名前.Graph.Surface.hlsl`）を作る
        void Generate3D(const Graph& graph, const GenerateOptions& options, const Resolution& resolution, const Node& root,
            GenerateResult& result)
        {
            std::vector<std::pair<int, GlobalCode>> globals;
            const bool previewing = root.type != kMasterNodeType;

            // 頂点の関数（Vertex の出口からたどる）
            FunctionWriter vertex(graph, resolution, options, kStageVertex, globals, result.issues);
            std::string vertexAssignments;
            if (const Node* const master = graph.FindVertexMaster()) {
                const NodeDesc* const desc = FindNodeDesc(*master);
                const auto resolved = resolution.nodes.find(master->id);
                if (desc && resolved != resolution.nodes.end()) {
                    vertex.Write(CollectOrder(graph, *master, nullptr), { master->id });
                    for (const MasterField& field : kVertexFields) {
                        const int index = desc->FindInput(field.pin);
                        if (index < 0) {
                            continue;
                        }
                        bool connected = false;
                        const std::string expression = vertex.Input(*master, *desc, resolved->second,
                            static_cast<size_t>(index), connected);
                        if (connected) {
                            vertexAssignments += "    OUT." + std::string(field.field) + " = " + expression + ";\n";
                        }
                    }
                }
            }
            if (vertex.ErrorCount() > 0) {
                result.fatal = "Vertex の出口へつながるノードに問題があるので、シェーダを作りません";
                return;
            }
            const bool hasVertex = !vertexAssignments.empty();

            // 画素の関数（Fragment の出口か映すノードからたどる）
            FunctionWriter fragment(graph, resolution, options, kStageFragment, globals, result.issues);
            std::string fragmentAssignments;
            if (previewing) {
                fragment.Write(CollectOrder(graph, root, nullptr), { root.id });
                const std::string color = PreviewColor(fragment, resolution, options, root);
                if (color.empty()) {
                    result.fatal = "このノードには映せる出力がありません";
                    return;
                }
                fragmentAssignments = "    OUT.baseColor = (" + color + ").rgb;\n    OUT.alpha = 1.0f;\n";
            } else {
                const Target target = graph.target;
                const auto targetPin = [target](const PinDesc& pin) { return pin.Supports(target); };
                fragment.Write(CollectOrder(graph, root, targetPin), { root.id }, targetPin);
                const NodeDesc* const desc = FindNodeDesc(root);
                const ResolvedNode& resolved = resolution.nodes.at(root.id);
                for (const MasterField& field : kFragmentFields) {
                    const int index = desc ? desc->FindInput(field.pin) : -1;
                    if (index < 0 || !desc->inputs[static_cast<size_t>(index)].Supports(target)) {
                        continue;
                    }
                    bool connected = false;
                    const std::string expression = fragment.Input(root, *desc, resolved, static_cast<size_t>(index), connected);
                    if (!connected) {
                        continue;
                    }
                    fragmentAssignments += "    OUT." + std::string(field.field) + " = "
                        + (field.tangentNormal ? "SurfaceTangentToWorld(IN, " + expression + ")" : expression) + ";\n";
                }
            }

            const uint8_t uses = static_cast<uint8_t>(fragment.Uses() | (hasVertex ? vertex.Uses() : 0));
            std::string head = SurfaceHead(graph, options, globals, result, graph.target == Target::Lit && !previewing, hasVertex,
                uses);
            if (hasVertex) {
                head += "\nvoid SurfaceVertex(SurfaceVertexInput IN, inout SurfaceVertexOutput OUT)\n{\n";
                vertex.AddLines(result.nodeLines, CountLines(head));
                head += vertex.Body() + (vertex.Body().empty() ? "" : "\n") + vertexAssignments + "}\n";
            }
            head += "\nvoid Surface(SurfaceInput IN, inout SurfaceOutput OUT)\n{\n";
            fragment.AddLines(result.nodeLines, CountLines(head));
            head += fragment.Body() + (fragment.Body().empty() || fragmentAssignments.empty() ? "" : "\n")
                + fragmentAssignments + "}\n";
            result.hlsl = std::move(head);
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

        // 出口（または映すノード）
        const Node* root = nullptr;
        if (options.previewNode != 0) {
            root = graph.FindNode(options.previewNode);
            if (root && IsMasterNodeType(root->type)) {
                root = nullptr;
            }
        }
        if (!root) {
            root = graph.FindMaster();
        }
        if (!root) {
            result.fatal = "出口のノードがありません";
            return result;
        }
        if (Is3D(graph.target)) {
            Generate3D(graph, options, resolution, *root, result);
            return result;
        }

        // 出口から入力をたどって、使うノードを依存の順に書く
        std::vector<std::pair<int, GlobalCode>> globals; ///< 関数の外に置く文と、置いたノード
        FunctionWriter writer(graph, resolution, options, kStageFragment, globals, result.issues);
        writer.Write(CollectOrder(graph, *root, nullptr), { root->id });
        std::string body = writer.Body();

        // 出口の式
        std::string color;
        if (root->type == kMasterNodeType) {
            const NodeDesc* const desc = FindNodeDesc(kMasterNodeType);
            const ResolvedNode& resolved = resolution.nodes.at(root->id);
            bool connected = false;
            const std::string baseColor = writer.Input(*root, *desc, resolved,
                static_cast<size_t>(desc->FindInput("Base Color")), connected);
            const std::string alpha = writer.Input(*root, *desc, resolved, static_cast<size_t>(desc->FindInput("Alpha")), connected);
            body += std::string(body.empty() ? "" : "\n") + "    float3 baseColor = " + baseColor + ";\n";
            body += "    float alpha = " + alpha + ";\n";
            color = "float4(baseColor, alpha)";
        } else {
            color = PreviewColor(writer, resolution, options, *root);
            if (color.empty()) {
                result.fatal = "このノードには映せる出力がありません";
                return result;
            }
        }

        // ファイルの頭（プロパティの表・定数・テクスチャ）
        std::string head = FlatHead(graph, options, globals, result);
        const bool postEffect = graph.target == Target::PostEffect;
        if (postEffect) {
            head += std::string(kPostEffectMainBegin) + std::string(kPostEffectScreenInputs) + std::string(kPostEffectTimeInputs);
        } else {
            head += std::string(kSpriteMainBegin);
        }

        writer.AddLines(result.nodeLines, CountLines(head));
        if (postEffect) {
            result.hlsl = head + body + "    gOutput[id.xy] = " + color + ";\n}\n";
        } else {
            result.hlsl = head + body + "    return " + color + ";\n}\n";
        }
        return result;
    }

    NodePreviewResult GenerateNodePreviews(const Graph& graph, const GenerateOptions& options)
    {
        NodePreviewResult result;
        std::set<std::string> names;
        for (const Property& property : graph.properties) {
            if (!IsValidPropertyName(property.name, nullptr) || !names.insert(property.name).second) {
                return result;
            }
        }

        // プレビューを開いた、映せる種類のノード（番号の順）
        std::vector<const Node*> candidates;
        for (const Node& node : graph.nodes) {
            const NodeDesc* const desc = FindNodeDesc(node);
            if (node.preview && desc && HasNodePreview(*desc) && desc->Supports(graph.target)) {
                candidates.push_back(&node);
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](const Node* a, const Node* b) { return a->id < b->id; });
        if (candidates.empty()) {
            return result;
        }

        // 1 回目: 問題のあるノードを集める（3D は画素の関数で書けるかを見る）
        const Resolution resolution = Resolve(graph);
        std::map<int, std::string> reasons;
        for (const NodeIssue& issue : resolution.issues) {
            reasons.emplace(issue.node, issue.message);
        }
        {
            std::vector<std::pair<int, GlobalCode>> globals;
            std::vector<NodeIssue> issues;
            FunctionWriter probe(graph, resolution, options, kStageFragment, globals, issues);
            probe.Write(CollectOrder(graph, candidates), NodeIds(candidates));
            for (const NodeIssue& issue : issues) {
                reasons.emplace(issue.node, issue.message);
            }
        }

        // 映すノードとタイルを決める（たどったノードに問題があれば映さない）
        std::vector<const Node*> chosen;
        for (const Node* node : candidates) {
            const std::vector<int> order = CollectOrder(graph, *node, nullptr);
            std::string reason;
            for (const int id : order) {
                if (const auto found = reasons.find(id); found != reasons.end()) {
                    reason = found->second;
                    break;
                }
            }
            const ResolvedNode& resolved = resolution.nodes.at(node->id);
            if (reason.empty() && std::none_of(resolved.outputs.begin(), resolved.outputs.end(), IsNumeric)) {
                reason = "映せる出力がありません";
            }
            if (!reason.empty()) {
                result.unavailable[node->id] = reason;
                continue;
            }
            if (static_cast<int>(chosen.size()) >= kNodePreviewMax) {
                result.overflow.push_back(node->id);
                continue;
            }
            const bool sphere = Is3D(graph.target) && Uses3DInput(graph, order);
            result.tiles.push_back(NodePreviewTile{ node->id, static_cast<int>(chosen.size()), sphere });
            chosen.push_back(node);
        }
        if (chosen.empty()) {
            return result;
        }

        // 2 回目: 映すノードとたどったノードを書き、タイルごとに色を選ぶ
        std::vector<std::pair<int, GlobalCode>> globals;
        std::vector<NodeIssue> issues;
        FunctionWriter writer(graph, resolution, options, kStageFragment, globals, issues);
        writer.Write(CollectOrder(graph, chosen), NodeIds(chosen));
        GenerateOptions colorOptions = options;
        colorOptions.previewPin.clear();

        // 画素の位置（描き先の px）からタイルの番号を出す
        const bool postEffect = graph.target == Target::PostEffect;
        const char* const pixel = Is3D(graph.target) ? "IN.screenPosition" : (postEffect ? "id.xy" : "input.position.xy");
        const std::string tile = std::to_string(kNodePreviewTile);
        std::string select;
        select += "\n    int2 SG_cell = int2(" + std::string(pixel) + ") / " + tile + ";\n";
        select += "    float4 SG_color = float4(0.0f, 0.0f, 0.0f, 1.0f);\n";
        select += "    switch (SG_cell.y * " + std::to_string(kNodePreviewColumns) + " + SG_cell.x) {\n";
        for (size_t index = 0; index < chosen.size(); ++index) {
            select += "    case " + std::to_string(index) + ": SG_color = "
                + PreviewColor(writer, resolution, colorOptions, *chosen[index]) + "; break;\n";
        }
        select += "    default: break;\n    }\n";

        GenerateResult unused;
        std::string hlsl;
        if (Is3D(graph.target)) {
            hlsl = SurfaceHead(graph, options, globals, unused, false, false, writer.Uses());
            hlsl += "\nvoid Surface(SurfaceInput IN, inout SurfaceOutput OUT)\n{\n";
            hlsl += writer.Body() + select;
            hlsl += "    OUT.baseColor = SG_color.rgb;\n    OUT.alpha = 1.0f;\n}\n";
        } else if (postEffect) {
            hlsl = FlatHead(graph, options, globals, unused);
            hlsl += std::string(kPostEffectMainBegin);
            hlsl += "    IN.uv = (float2(id.xy % " + tile + "u) + 0.5f) / " + tile + ".0f;\n";
            hlsl += "    IN.sceneColor = SampleScreen(IN.uv);\n";
            hlsl += "    IN.screenSize = float2(" + tile + ".0f, " + tile + ".0f);\n";
            hlsl += "    IN.invScreenSize = float2(1.0f / " + tile + ".0f, 1.0f / " + tile + ".0f);\n";
            hlsl += std::string(kPostEffectTimeInputs);
            hlsl += writer.Body() + select;
            hlsl += "    gOutput[id.xy] = float4(SG_color.rgb, 1.0f);\n}\n";
        } else {
            hlsl = FlatHead(graph, options, globals, unused);
            hlsl += std::string(kSpriteMainBegin);
            hlsl += writer.Body() + select;
            hlsl += "    return float4(SG_color.rgb, 1.0f);\n}\n";
        }
        result.hlsl = std::move(hlsl);
        return result;
    }
}

#endif // CORE_EDITOR
