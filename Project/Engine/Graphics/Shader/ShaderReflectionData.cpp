#include "ShaderReflectionData.h"
#include <algorithm>
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
        ss << "=== Shader Reflection Data ===\n";

        // CBVs
        ss << "\n[Constant Buffers] Count: " << cbvBindings_.size() << "\n";
        for (const auto& cbv : cbvBindings_) {
            ss << "  - " << cbv.name << " : b" << cbv.bindPoint;
            if (cbv.space > 0) ss << ", space" << cbv.space;
            ss << " (Size: " << cbv.size << " bytes)";
            ss << " [" << GetShaderVisibilityString(cbv.visibility) << "]\n";
        }

        // SRVs
        ss << "\n[Shader Resource Views] Count: " << srvBindings_.size() << "\n";
        for (const auto& srv : srvBindings_) {
            ss << "  - " << srv.name << " : t" << srv.bindPoint;
            if (srv.space > 0) ss << ", space" << srv.space;
            if (srv.bindCount > 1) ss << " [Array: " << srv.bindCount << "]";
            ss << " [" << GetShaderVisibilityString(srv.visibility) << "]\n";
        }

        // UAVs
        if (!uavBindings_.empty()) {
            ss << "\n[Unordered Access Views] Count: " << uavBindings_.size() << "\n";
            for (const auto& uav : uavBindings_) {
                ss << "  - " << uav.name << " : u" << uav.bindPoint;
                if (uav.space > 0) ss << ", space" << uav.space;
                if (uav.bindCount > 1) ss << " [Array: " << uav.bindCount << "]";
                ss << " [" << GetShaderVisibilityString(uav.visibility) << "]\n";
            }
        }

        // Samplers
        ss << "\n[Samplers] Count: " << samplerBindings_.size() << "\n";
        for (const auto& sampler : samplerBindings_) {
            ss << "  - " << sampler.name << " : s" << sampler.bindPoint;
            if (sampler.space > 0) ss << ", space" << sampler.space;
            ss << " [" << GetShaderVisibilityString(sampler.visibility) << "]\n";
        }

        // Input Layout
        if (!inputElements_.empty()) {
            ss << "\n[Input Layout] Count: " << inputElements_.size() << "\n";
            for (const auto& input : inputElements_) {
                ss << "  - " << input.semanticName << input.semanticIndex;
                ss << " (Slot: " << input.inputSlot << ", Format: " << GetFormatString(input.format) << ")\n";
            }
        }

        ss << "==============================\n";
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
}
