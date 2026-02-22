#include "ShaderReflectionData.h"
#include "Engine/Utility/Logger/Logger.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace CoreEngine
{
    void ShaderReflectionData::AddCBV(const ShaderResourceBinding& binding) {
        // 重複チェック（bindPoint、space、visibilityで判定）
        if (!HasBinding(cbvBindings_, binding.bindPoint, binding.space, binding.visibility)) {
            cbvBindings_.push_back(binding);
        }
    }

    void ShaderReflectionData::AddSRV(const ShaderResourceBinding& binding) {
        if (!HasBinding(srvBindings_, binding.bindPoint, binding.space, binding.visibility)) {
            srvBindings_.push_back(binding);
        }
    }

    void ShaderReflectionData::AddUAV(const ShaderResourceBinding& binding) {
        if (!HasBinding(uavBindings_, binding.bindPoint, binding.space, binding.visibility)) {
            uavBindings_.push_back(binding);
        }
    }

    void ShaderReflectionData::AddSampler(const ShaderResourceBinding& binding) {
        if (!HasBinding(samplerBindings_, binding.bindPoint, binding.space, binding.visibility)) {
            samplerBindings_.push_back(binding);
        }
    }

    std::vector<ShaderResourceBinding> ShaderReflectionData::GetAllBindingsSorted() const {
        std::vector<ShaderResourceBinding> allBindings;

        // すべてのバインディングを結合
        allBindings.insert(allBindings.end(), cbvBindings_.begin(), cbvBindings_.end());
        allBindings.insert(allBindings.end(), srvBindings_.begin(), srvBindings_.end());
        allBindings.insert(allBindings.end(), uavBindings_.begin(), uavBindings_.end());
        allBindings.insert(allBindings.end(), samplerBindings_.begin(), samplerBindings_.end());

        // bindPointでソート
        std::sort(allBindings.begin(), allBindings.end(),
            [](const ShaderResourceBinding& a, const ShaderResourceBinding& b) {
                if (a.space != b.space) return a.space < b.space;
                if (a.type != b.type) return a.type < b.type;
                return a.bindPoint < b.bindPoint;
            });

        return allBindings;
    }

    const ShaderResourceBinding* ShaderReflectionData::FindCBV(const std::string& name) const {
        auto it = std::find_if(cbvBindings_.begin(), cbvBindings_.end(),
            [&name](const ShaderResourceBinding& binding) {
                return binding.name == name;
            });
        return it != cbvBindings_.end() ? &(*it) : nullptr;
    }

    const ShaderResourceBinding* ShaderReflectionData::FindSRV(const std::string& name) const {
        auto it = std::find_if(srvBindings_.begin(), srvBindings_.end(),
            [&name](const ShaderResourceBinding& binding) {
                return binding.name == name;
            });
        return it != srvBindings_.end() ? &(*it) : nullptr;
    }

    const ShaderResourceBinding* ShaderReflectionData::FindUAV(const std::string& name) const {
        auto it = std::find_if(uavBindings_.begin(), uavBindings_.end(),
            [&name](const ShaderResourceBinding& binding) {
                return binding.name == name;
            });
        return it != uavBindings_.end() ? &(*it) : nullptr;
    }

    const ShaderResourceBinding* ShaderReflectionData::FindSampler(const std::string& name) const {
        auto it = std::find_if(samplerBindings_.begin(), samplerBindings_.end(),
            [&name](const ShaderResourceBinding& binding) {
                return binding.name == name;
            });
        return it != samplerBindings_.end() ? &(*it) : nullptr;
    }

    std::string ShaderReflectionData::ToString() const {
        std::stringstream ss;
        
        // シェーダー名のタイトル作成
        std::string title = "SHADER REFLECTION";
        if (!shaderName_.empty()) {
            title = shaderName_;
        }
        
        // タイトルを中央揃え（最大幅65文字）
        const size_t boxWidth = 65;
        size_t padding = (boxWidth > title.length()) ? (boxWidth - title.length()) / 2 : 0;
        std::string centeredTitle = std::string(padding, ' ') + title;
        
        // ヘッダーライン
        ss << "\n";
        ss << "┌─────────────────────────────────────────────────────────────────┐\n";
        ss << "│" << centeredTitle;
        // 右側のパディング
        size_t rightPad = boxWidth - centeredTitle.length();
        ss << std::string(rightPad, ' ') << "│\n";
        ss << "├─────────────────────────────────────────────────────────────────┤\n";

        // サマリー
        ss << "│  Summary: CBV=" << cbvBindings_.size() 
           << "  SRV=" << srvBindings_.size() 
           << "  UAV=" << uavBindings_.size()
           << "  Sampler=" << samplerBindings_.size() 
           << "  InputLayout=" << inputElements_.size() << "\n";
        ss << "├─────────────────────────────────────────────────────────────────┤\n";

        // CBVs
        if (!cbvBindings_.empty()) {
            ss << "│  [Constant Buffers] (" << cbvBindings_.size() << ")\n";
            for (const auto& cbv : cbvBindings_) {
                ss << "│    • " << cbv.name;
                ss << "  →  b" << cbv.bindPoint;
                if (cbv.space > 0) ss << ", space" << cbv.space;
                ss << "  (" << cbv.size << " bytes)";
                ss << "  [" << GetShaderVisibilityString(cbv.visibility) << "]\n";
            }
        }

        // SRVs
        if (!srvBindings_.empty()) {
            ss << "│  [Shader Resource Views] (" << srvBindings_.size() << ")\n";
            for (const auto& srv : srvBindings_) {
                ss << "│    • " << srv.name;
                ss << "  →  t" << srv.bindPoint;
                if (srv.space > 0) ss << ", space" << srv.space;
                if (srv.bindCount > 1) ss << " [×" << srv.bindCount << "]";
                ss << "  [" << GetShaderVisibilityString(srv.visibility) << "]\n";
            }
        }

        // UAVs
        if (!uavBindings_.empty()) {
            ss << "│  [Unordered Access Views] (" << uavBindings_.size() << ")\n";
            for (const auto& uav : uavBindings_) {
                ss << "│    • " << uav.name;
                ss << "  →  u" << uav.bindPoint;
                if (uav.space > 0) ss << ", space" << uav.space;
                if (uav.bindCount > 1) ss << " [×" << uav.bindCount << "]";
                ss << "  [" << GetShaderVisibilityString(uav.visibility) << "]\n";
            }
        }

        // Samplers
        if (!samplerBindings_.empty()) {
            ss << "│  [Samplers] (" << samplerBindings_.size() << ")\n";
            for (const auto& sampler : samplerBindings_) {
                ss << "│    • " << sampler.name;
                ss << "  →  s" << sampler.bindPoint;
                if (sampler.space > 0) ss << ", space" << sampler.space;
                ss << "  [" << GetShaderVisibilityString(sampler.visibility) << "]\n";
            }
        }

        // Input Layout
        if (!inputElements_.empty()) {
            ss << "│  [Input Layout] (" << inputElements_.size() << ")\n";
            for (const auto& input : inputElements_) {
                ss << "│    • " << input.semanticName << input.semanticIndex;
                ss << "  (Slot:" << input.inputSlot << ", " << GetFormatString(input.format) << ")\n";
            }
        }

        ss << "└─────────────────────────────────────────────────────────────────┘\n";
        return ss.str();
    }

    void ShaderReflectionData::Clear() {
        cbvBindings_.clear();
        srvBindings_.clear();
        uavBindings_.clear();
        samplerBindings_.clear();
        inputElements_.clear();
        rootParameterMapping_.clear();
    }

    void ShaderReflectionData::Merge(const ShaderReflectionData& other) {
        // CBVをマージ（重複チェック）
        for (const auto& cbv : other.cbvBindings_) {
            if (!HasBinding(cbvBindings_, cbv.bindPoint, cbv.space, cbv.visibility)) {
                cbvBindings_.push_back(cbv);
            }
        }

        // SRVをマージ
        for (const auto& srv : other.srvBindings_) {
            if (!HasBinding(srvBindings_, srv.bindPoint, srv.space, srv.visibility)) {
                srvBindings_.push_back(srv);
            }
        }

        // UAVをマージ
        for (const auto& uav : other.uavBindings_) {
            if (!HasBinding(uavBindings_, uav.bindPoint, uav.space, uav.visibility)) {
                uavBindings_.push_back(uav);
            }
        }

        // Samplerをマージ
        for (const auto& sampler : other.samplerBindings_) {
            if (!HasBinding(samplerBindings_, sampler.bindPoint, sampler.space, sampler.visibility)) {
                samplerBindings_.push_back(sampler);
            }
        }

        // Input Layoutは頂点シェーダーからのみ取得するので、既存を優先
        if (inputElements_.empty() && !other.inputElements_.empty()) {
            inputElements_ = other.inputElements_;
        }
    }

    int ShaderReflectionData::GetRootParameterIndexByName(const std::string& resourceName) const {
        auto it = rootParameterMapping_.find(resourceName);
        if (it != rootParameterMapping_.end()) {
            return static_cast<int>(it->second);
        }
        return -1;  // 見つからない場合は-1を返す
    }

    void ShaderReflectionData::SetRootParameterMapping(const std::map<std::string, UINT>& mapping) {
        rootParameterMapping_ = mapping;
    }

    bool ShaderReflectionData::HasBinding(const std::vector<ShaderResourceBinding>& bindings,
        UINT bindPoint, UINT space, D3D12_SHADER_VISIBILITY visibility) const {
        return std::find_if(bindings.begin(), bindings.end(),
            [bindPoint, space, visibility](const ShaderResourceBinding& binding) {
                return binding.bindPoint == bindPoint && 
                       binding.space == space && 
                       binding.visibility == visibility;
            }) != bindings.end();
    }

    // ヘルパー関数
    std::string ShaderReflectionData::GetShaderVisibilityString(D3D12_SHADER_VISIBILITY visibility) const {
        switch (visibility) {
        case D3D12_SHADER_VISIBILITY_ALL: return "ALL";
        case D3D12_SHADER_VISIBILITY_VERTEX: return "VS";
        case D3D12_SHADER_VISIBILITY_HULL: return "HS";
        case D3D12_SHADER_VISIBILITY_DOMAIN: return "DS";
        case D3D12_SHADER_VISIBILITY_GEOMETRY: return "GS";
        case D3D12_SHADER_VISIBILITY_PIXEL: return "PS";
        default: return "UNKNOWN";
        }
    }

    std::string ShaderReflectionData::GetFormatString(DXGI_FORMAT format) const {
        switch (format) {
        case DXGI_FORMAT_R32G32B32A32_FLOAT: return "R32G32B32A32_FLOAT";
        case DXGI_FORMAT_R32G32B32_FLOAT: return "R32G32B32_FLOAT";
        case DXGI_FORMAT_R32G32_FLOAT: return "R32G32_FLOAT";
        case DXGI_FORMAT_R32_FLOAT: return "R32_FLOAT";
        case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
        default: return "UNKNOWN";
        }
    }

    bool ShaderReflectionData::ValidateCBVSize(const std::string& cbvName, size_t cppStructSize) const {
        const auto* cbv = FindCBV(cbvName);
        
        if (!cbv) {
            // CBVが見つからない場合は警告（オプショナルなリソースかもしれない）
            Logger::GetInstance().Log(
                "[CBV Validation] Warning: CBV '" + cbvName + "' not found in " + shaderName_,
                LogLevel::WARNING, LogCategory::Shader);
            return true;  // 見つからない場合は検証スキップ（オプショナルとみなす）
        }
        
        // HLSLの16バイトアライメントを考慮
        // シェーダーのサイズは既にアライメント済みのはず
        size_t shaderSize = cbv->size;
        
        // C++側のサイズとシェーダー側のサイズを比較
        if (cppStructSize != shaderSize) {
            std::ostringstream oss;
            oss << "\n";
            oss << "┌─────────────────────────────────────────────────────────────────┐\n";
            oss << "│  [ERROR] CBV Size Mismatch Detected!                           │\n";
            oss << "├─────────────────────────────────────────────────────────────────┤\n";
            oss << "│  Shader:    " << shaderName_ << "\n";
            oss << "│  CBV Name:  " << cbvName << "\n";
            oss << "├─────────────────────────────────────────────────────────────────┤\n";
            oss << "│  Shader expects:  " << shaderSize << " bytes\n";
            oss << "│  C++ provides:    " << cppStructSize << " bytes\n";
            oss << "│  Difference:      " << static_cast<int>(shaderSize) - static_cast<int>(cppStructSize) << " bytes\n";
            oss << "├─────────────────────────────────────────────────────────────────┤\n";
            oss << "│  [Possible Causes]                                             │\n";
            
            if (cppStructSize < shaderSize) {
                oss << "│    • C++ struct is missing members                             │\n";
                oss << "│    • HLSL has additional padding                               │\n";
            } else {
                oss << "│    • C++ struct has extra members                              │\n";
                oss << "│    • HLSL struct is missing members                            │\n";
            }
            oss << "│    • Alignment mismatch (HLSL uses 16-byte alignment)          │\n";
            oss << "└─────────────────────────────────────────────────────────────────┘\n";
            
            Logger::GetInstance().Log(oss.str(), LogLevel::Error, LogCategory::Shader);
            return false;
        }
        
        return true;
    }

    bool ShaderReflectionData::ValidateAllCBVSizes(
        const std::vector<std::pair<std::string, size_t>>& validations) const {
        
        bool allValid = true;
        
        for (const auto& [cbvName, structSize] : validations) {
            if (!ValidateCBVSize(cbvName, structSize)) {
                allValid = false;
            }
        }
        
        // 全て成功した場合はログ出力
        if (allValid && !validations.empty()) {
#ifdef _DEBUG
            std::ostringstream oss;
            oss << "\n";
            oss << "┌─────────────────────────────────────────────────────────────────┐\n";
            oss << "│  [OK] CBV Size Validation Passed - " << shaderName_ << "\n";
            oss << "├─────────────────────────────────────────────────────────────────┤\n";
            
            for (const auto& [cbvName, structSize] : validations) {
                const auto* cbv = FindCBV(cbvName);
                if (cbv) {
                    oss << "│    ✓ " << cbvName << ": " << structSize << " bytes\n";
                }
            }
            
            oss << "└─────────────────────────────────────────────────────────────────┘\n";
            Logger::GetInstance().Log(oss.str(), LogLevel::INFO, LogCategory::Shader);
#endif
        }
        
        return allValid;
    }

    // ================================================================================
    // スロット自動検出
    // ================================================================================
    
    namespace {
        // スキニング関連のセマンティック（スロット1に割り当て）
        const std::vector<std::string> kSkinningSemantics = {
            "WEIGHT", "INDEX", "BONEINDEX", "BONEWEIGHT", "BLENDWEIGHT", "BLENDINDICES"
        };
        
        // インスタンシング関連のセマンティック（スロット2に割り当て）
        const std::vector<std::string> kInstancingSemantics = {
            "INSTANCE", "INSTANCEID", "INSTANCEDATA"
        };
        
        // セマンティック名からスロットを判定
        UINT DetectSlotFromSemantic(const std::string& semanticName) {
            // 大文字に変換して比較
            std::string upperSemantic = semanticName;
            std::transform(upperSemantic.begin(), upperSemantic.end(), 
                          upperSemantic.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            
            // スキニングセマンティックチェック
            for (const auto& skinSemantic : kSkinningSemantics) {
                if (upperSemantic.find(skinSemantic) != std::string::npos) {
                    return 1;  // スロット1
                }
            }
            
            // インスタンシングセマンティックチェック
            for (const auto& instSemantic : kInstancingSemantics) {
                if (upperSemantic.find(instSemantic) != std::string::npos) {
                    return 2;  // スロット2
                }
            }
            
            return 0;  // デフォルトはスロット0
        }
    }

    void ShaderReflectionData::ApplyAutoSlotDetection() {
        bool hasSlotChange = false;
        
        for (auto& element : inputElements_) {
            UINT detectedSlot = DetectSlotFromSemantic(element.semanticName);
            if (detectedSlot != element.inputSlot) {
                element.inputSlot = detectedSlot;
                hasSlotChange = true;
            }
        }
        
#ifdef _DEBUG
        if (hasSlotChange) {
            std::ostringstream oss;
            oss << "\n";
            oss << "┌─────────────────────────────────────────────────────────────────┐\n";
            oss << "│  [Auto Slot Detection] " << shaderName_ << "\n";
            oss << "├─────────────────────────────────────────────────────────────────┤\n";
            
            for (const auto& element : inputElements_) {
                oss << "│    • " << element.semanticName << element.semanticIndex 
                    << " → Slot " << element.inputSlot;
                if (element.inputSlot > 0) {
                    oss << " (auto-detected)";
                }
                oss << "\n";
            }
            
            oss << "└─────────────────────────────────────────────────────────────────┘\n";
            Logger::GetInstance().Log(oss.str(), LogLevel::INFO, LogCategory::Shader);
        }
#endif
    }

    std::vector<InputElementInfo> ShaderReflectionData::GetInputElementsWithAutoSlots() const {
        std::vector<InputElementInfo> elements = inputElements_;
        
        for (auto& element : elements) {
            UINT detectedSlot = DetectSlotFromSemantic(element.semanticName);
            element.inputSlot = detectedSlot;
        }
        
        return elements;
    }
}
