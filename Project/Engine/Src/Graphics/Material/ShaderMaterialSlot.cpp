#include "pch.h"
#include "ShaderMaterialSlot.h"

#include "Graphics/Material/ShaderMaterialLibrary.h"
#include "Graphics/Material/ShaderMaterialPipeline.h"
#include "Graphics/RHI/Resource/UploadRing.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace CoreEngine
{
    namespace
    {
        /// @brief テクスチャを指していないときに差す白
        constexpr const char* kWhiteTexture = "white1x1.png";

        /// @brief 定数バッファの配列の要素の間隔（要素ごとに 16 バイトの区切りから始まる）
        constexpr uint32_t kArrayElementStride = 16;

        /// @brief 1 要素ぶんの数を、項目の種類に合わせて書く
        void WriteElement(std::vector<uint8_t>& out, const MaterialParamField& field, uint32_t offset,
            const MaterialNumbers& numbers)
        {
            const size_t count = (std::min)(static_cast<size_t>(field.components), numbers.size());
            for (size_t component = 0; component < count; ++component) {
                const size_t at = offset + component * sizeof(float);
                if (at + sizeof(float) > out.size()) {
                    return;
                }
                const float number = numbers[component];
                switch (field.kind) {
                case MaterialParamField::Kind::Float:
                    std::memcpy(out.data() + at, &number, sizeof(float));
                    break;
                case MaterialParamField::Kind::Int: {
                    const int32_t value = static_cast<int32_t>(std::lround(number));
                    std::memcpy(out.data() + at, &value, sizeof(value));
                    break;
                }
                case MaterialParamField::Kind::Uint: {
                    const uint32_t value = number <= 0.0f ? 0u : static_cast<uint32_t>(std::lround(number));
                    std::memcpy(out.data() + at, &value, sizeof(value));
                    break;
                }
                case MaterialParamField::Kind::Bool: {
                    const uint32_t value = number != 0.0f ? 1u : 0u;
                    std::memcpy(out.data() + at, &value, sizeof(value));
                    break;
                }
                }
            }
        }
    }

    bool ShaderMaterialSlot::SetAsset(const Reflection::AssetRefValue& value)
    {
        if (value == asset_.GetValue()) {
            return false;
        }
        asset_.SetValue(value);
        material_.reset();
        packedMaterial_ = nullptr;
        return true;
    }

    void ShaderMaterialSlot::Resolve()
    {
        material_ = asset_.IsSet() ? ShaderMaterialLibrary::GetInstance().Get(asset_.GetPath()) : nullptr;
        packedMaterial_ = nullptr;

        TextureManager& textures = TextureManager::GetInstance();
        if (material_ && whiteTexture_.gpuHandle.ptr == 0 && textures.IsInitialized()) {
            whiteTexture_ = textures.Load(kWhiteTexture);
        }
    }

    void ShaderMaterialSlot::SetNumbers(std::string_view name, std::span<const float> numbers)
    {
        if (name.empty() || numbers.empty()) {
            return;
        }
        const size_t count = (std::min)(numbers.size(), static_cast<size_t>(4));
        MaterialNumbers value(numbers.begin(), numbers.begin() + static_cast<std::ptrdiff_t>(count));
        const auto found = overrideValues_.find(name);
        if (found != overrideValues_.end()) {
            if (found->second == value) {
                return;
            }
            found->second = std::move(value);
        } else {
            overrideValues_.emplace(std::string(name), std::move(value));
        }
        ++overridesRevision_;
    }

    void ShaderMaterialSlot::SetTexture(std::string_view name, const std::string& path)
    {
        if (name.empty()) {
            return;
        }
        const auto found = overrideTextures_.find(name);
        if (path.empty()) {
            if (found != overrideTextures_.end()) {
                overrideTextures_.erase(found);
            }
            return;
        }
        overrideTextures_[std::string(name)] = LoadMaterialTexture(MaterialTexture{ path, false });
    }

    void ShaderMaterialSlot::ClearOverrides()
    {
        if (!overrideValues_.empty()) {
            overrideValues_.clear();
            ++overridesRevision_;
        }
        overrideTextures_.clear();
    }

    const MaterialNumbers* ShaderMaterialSlot::FindNumbers(std::string_view name) const
    {
        if (const auto found = overrideValues_.find(name); found != overrideValues_.end()) {
            return &found->second;
        }
        return material_ ? material_->FindNumbers(name) : nullptr;
    }

    void ShaderMaterialSlot::Pack(const MaterialParamLayout& layout)
    {
        const uint64_t materialRevision = material_ ? material_->GetRevision() : 0;
        if (packedLayout_ == &layout && packedMaterial_ == material_.get()
            && packedMaterialRevision_ == materialRevision && packedOverridesRevision_ == overridesRevision_
            && packed_.size() == layout.size) {
            return;
        }
        packedLayout_ = &layout;
        packedMaterial_ = material_.get();
        packedMaterialRevision_ = materialRevision;
        packedOverridesRevision_ = overridesRevision_;

        packed_.assign(layout.size, 0);
        for (const MaterialParamField& field : layout.fields) {
            if (field.elements == 0) {
                if (const MaterialNumbers* numbers = FindNumbers(field.name)) {
                    WriteElement(packed_, field, field.offset, *numbers);
                }
                continue;
            }
            for (uint32_t index = 0; index < field.elements; ++index) {
                const std::string key = field.name + "[" + std::to_string(index) + "]";
                if (const MaterialNumbers* numbers = FindNumbers(key)) {
                    WriteElement(packed_, field, field.offset + index * kArrayElementStride, *numbers);
                }
            }
        }
    }

    D3D12_GPU_DESCRIPTOR_HANDLE ShaderMaterialSlot::GetWhiteTexture()
    {
        return whiteTexture_.gpuHandle;
    }

    bool ShaderMaterialSlot::Bind(ID3D12GraphicsCommandList* commandList, const ShaderMaterialPipeline& pipeline,
        UploadRing& uploadRing, D3D12_GPU_VIRTUAL_ADDRESS transform, D3D12_GPU_VIRTUAL_ADDRESS draw,
        D3D12_GPU_DESCRIPTOR_HANDLE mainTexture)
    {
        const MaterialParamLayout& layout = pipeline.GetLayout();

        // 足りないテクスチャを白で埋められないなら描かない（差していない表はデバッグレイヤーが止める）
        std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> extraTextures;
        extraTextures.reserve(layout.textures.size());
        for (const std::string& name : layout.textures) {
            D3D12_GPU_DESCRIPTOR_HANDLE handle{};
            if (const auto found = overrideTextures_.find(name); found != overrideTextures_.end()) {
                handle = found->second.gpuHandle;
            } else if (const TextureManager::LoadedTexture* loaded =
                           material_ ? material_->FindLoadedTexture(name) : nullptr) {
                handle = loaded->gpuHandle;
            }
            if (handle.ptr == 0) {
                handle = GetWhiteTexture();
            }
            if (handle.ptr == 0) {
                return false;
            }
            extraTextures.push_back(handle);
        }

        if (pipeline.GetTransformSlot().IsValid()) {
            commandList->SetGraphicsRootConstantBufferView(pipeline.GetTransformSlot().index, transform);
        }
        if (pipeline.GetDrawSlot().IsValid()) {
            commandList->SetGraphicsRootConstantBufferView(pipeline.GetDrawSlot().index, draw);
        }
        if (pipeline.GetTextureSlot().IsValid()) {
            commandList->SetGraphicsRootDescriptorTable(pipeline.GetTextureSlot().index, mainTexture);
        }
        if (pipeline.GetParamsSlot().IsValid() && layout.size > 0) {
            Pack(layout);
            const D3D12_GPU_VIRTUAL_ADDRESS params =
                uploadRing.AllocateConstants(packed_.data(), static_cast<uint32_t>(packed_.size()));
            if (params == 0) {
                return false;
            }
            commandList->SetGraphicsRootConstantBufferView(pipeline.GetParamsSlot().index, params);
        }
        const std::vector<RootSlot>& textureSlots = pipeline.GetExtraTextureSlots();
        for (size_t index = 0; index < textureSlots.size() && index < extraTextures.size(); ++index) {
            if (textureSlots[index].IsValid()) {
                commandList->SetGraphicsRootDescriptorTable(textureSlots[index].index, extraTextures[index]);
            }
        }
        return true;
    }
}
