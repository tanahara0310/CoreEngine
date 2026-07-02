#include "pch.h"
#include "FFTOceanReadbackHelper.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "Graphics/Resource/ResourceFactory.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace
    {
        struct Float4DebugValue {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float w = 0.0f;
        };

        float HalfToFloat(uint16_t value)
        {
            const uint32_t sign = (static_cast<uint32_t>(value) >> 15) & 0x1;
            const uint32_t exponent = (static_cast<uint32_t>(value) >> 10) & 0x1F;
            const uint32_t mantissa = static_cast<uint32_t>(value) & 0x3FF;

            uint32_t resultBits = 0;
            if (exponent == 0) {
                if (mantissa == 0) {
                    resultBits = sign << 31;
                } else {
                    uint32_t normalizedMantissa = mantissa;
                    uint32_t adjustedExponent = 113;
                    while ((normalizedMantissa & 0x400) == 0) {
                        normalizedMantissa <<= 1;
                        --adjustedExponent;
                    }
                    normalizedMantissa &= 0x3FF;
                    resultBits = (sign << 31) | (adjustedExponent << 23) | (normalizedMantissa << 13);
                }
            } else if (exponent == 0x1F) {
                resultBits = (sign << 31) | 0x7F800000 | (mantissa << 13);
            } else {
                const uint32_t adjustedExponent = exponent + (127 - 15);
                resultBits = (sign << 31) | (adjustedExponent << 23) | (mantissa << 13);
            }

            float result = 0.0f;
            std::memcpy(&result, &resultBits, sizeof(float));
            return result;
        }

        Float4DebugValue LoadHalf4(const uint8_t* baseAddress, uint32_t x, uint32_t y, uint32_t rowPitch)
        {
            const uint8_t* texelAddress = baseAddress + static_cast<size_t>(y) * rowPitch + static_cast<size_t>(x) * sizeof(uint16_t) * 4;
            const uint16_t* halfValues = reinterpret_cast<const uint16_t*>(texelAddress);
            Float4DebugValue value{};
            value.x = HalfToFloat(halfValues[0]);
            value.y = HalfToFloat(halfValues[1]);
            value.z = HalfToFloat(halfValues[2]);
            value.w = HalfToFloat(halfValues[3]);
            return value;
        }

        Float4DebugValue LoadFloat4(const uint8_t* baseAddress, uint32_t x, uint32_t y, uint32_t rowPitch)
        {
            const uint8_t* texelAddress = baseAddress + static_cast<size_t>(y) * rowPitch + static_cast<size_t>(x) * sizeof(float) * 4;
            Float4DebugValue value{};
            std::memcpy(&value, texelAddress, sizeof(Float4DebugValue));
            return value;
        }
    }

    Microsoft::WRL::ComPtr<ID3D12Resource> FFTOceanReadbackHelper::CreateReadbackBuffer(ID3D12Device* device, UINT64 sizeInBytes)
    {
        if (!device) {
            return nullptr;
        }

        try {
            Microsoft::WRL::ComPtr<ID3D12Device> deviceRef = device;
            return ResourceFactory::CreateBufferResource(
                deviceRef,
                static_cast<size_t>(sizeInBytes),
                D3D12_HEAP_TYPE_READBACK);
        }
        catch (const std::exception&) {
            return nullptr;
        }
    }

    bool FFTOceanReadbackHelper::CreateTextureReadbackBuffer(const FFTOceanReadbackTextureCreateRequest& request)
    {
        if (!request.device || !request.texture || !request.outReadbackBuffer || !request.outLayout || !request.outTotalBytes) {
            return false;
        }

        UINT numRows = 0;
        UINT64 rowSizeInBytes = 0;
        const D3D12_RESOURCE_DESC textureDesc = request.texture->GetDesc();
        request.device->GetCopyableFootprints(
            &textureDesc,
            0,
            1,
            0,
            request.outLayout,
            &numRows,
            &rowSizeInBytes,
            request.outTotalBytes);

        *request.outReadbackBuffer = CreateReadbackBuffer(request.device, *request.outTotalBytes);
        if (!(*request.outReadbackBuffer)) {
            Logger::GetInstance().Errorf(
                LogCategory::Graphics,
                LogSubCategory::Buffer,
                "FFTOceanReadbackHelper: {} readback buffer creation failed. bytes={}",
                request.debugName ? request.debugName : "texture",
                *request.outTotalBytes);
            return false;
        }

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Buffer,
            "FFTOceanReadbackHelper: {} readback ready. rowPitch={} totalBytes={}",
            request.debugName ? request.debugName : "texture",
            request.outLayout->Footprint.RowPitch,
            *request.outTotalBytes);

        return true;
    }

    bool FFTOceanReadbackHelper::TryLogSurfaceReadback(const SurfaceReadbackRequest& request)
    {
        if (!request.displacementReadbackBuffer || !request.normalReadbackBuffer || request.resolution == 0) {
            return false;
        }

        void* displacementMapped = nullptr;
        void* normalMapped = nullptr;
        const D3D12_RANGE displacementReadRange{ 0, static_cast<SIZE_T>(request.displacementReadbackBytes) };
        const D3D12_RANGE normalReadRange{ 0, static_cast<SIZE_T>(request.normalReadbackBytes) };
        const HRESULT displacementHr = request.displacementReadbackBuffer->Map(0, &displacementReadRange, &displacementMapped);
        const HRESULT normalHr = request.normalReadbackBuffer->Map(0, &normalReadRange, &normalMapped);
        if (FAILED(displacementHr) || FAILED(normalHr) || !displacementMapped || !normalMapped) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Buffer,
                "FFTOceanManager: debug readback map failed. displacementHr={:#x} normalHr={:#x}",
                static_cast<uint32_t>(displacementHr),
                static_cast<uint32_t>(normalHr));
            return false;
        }

        const uint32_t centerX = request.resolution / 2;
        const uint32_t centerY = request.resolution / 2;
        const Float4DebugValue centerDisplacement = LoadHalf4(
            static_cast<const uint8_t*>(displacementMapped),
            centerX,
            centerY,
            request.displacementLayout.Footprint.RowPitch);
        const Float4DebugValue centerNormal = LoadHalf4(
            static_cast<const uint8_t*>(normalMapped),
            centerX,
            centerY,
            request.normalLayout.Footprint.RowPitch);

        float minHeight = (std::numeric_limits<float>::max)();
        float maxHeight = (std::numeric_limits<float>::lowest)();
        float accumulatedAbsHeight = 0.0f;
        float maxHorizontalMagnitude = 0.0f;
        const uint8_t* displacementBase = static_cast<const uint8_t*>(displacementMapped);
        for (uint32_t y = 0; y < request.resolution; ++y) {
            for (uint32_t x = 0; x < request.resolution; ++x) {
                const Float4DebugValue displacementValue = LoadHalf4(
                    displacementBase,
                    x,
                    y,
                    request.displacementLayout.Footprint.RowPitch);
                minHeight = (std::min)(minHeight, displacementValue.y);
                maxHeight = (std::max)(maxHeight, displacementValue.y);
                accumulatedAbsHeight += std::abs(displacementValue.y);
                const float horizontalMagnitude = std::sqrt(
                    displacementValue.x * displacementValue.x + displacementValue.z * displacementValue.z);
                maxHorizontalMagnitude = (std::max)(maxHorizontalMagnitude, horizontalMagnitude);
            }
        }

        request.displacementReadbackBuffer->Unmap(0, nullptr);
        request.normalReadbackBuffer->Unmap(0, nullptr);

        const float averageAbsHeight = accumulatedAbsHeight / static_cast<float>(request.resolution * request.resolution);
        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "FFTOceanManager: readback seq={} centerDisp=({:.6f}, {:.6f}, {:.6f}, {:.6f}) centerNormal=({:.6f}, {:.6f}, {:.6f}, {:.6f}) height[min,max,avgAbs]=[{:.6f}, {:.6f}, {:.6f}] maxHorizontal={:.6f}",
            request.sequence,
            centerDisplacement.x,
            centerDisplacement.y,
            centerDisplacement.z,
            centerDisplacement.w,
            centerNormal.x,
            centerNormal.y,
            centerNormal.z,
            centerNormal.w,
            minHeight,
            maxHeight,
            averageAbsHeight,
            maxHorizontalMagnitude);
        return true;
    }

    bool FFTOceanReadbackHelper::TryLogSpectrumReadback(const SpectrumReadbackRequest& request)
    {
        if (!request.spectrumAReadbackBuffer || !request.spectrumBReadbackBuffer || request.resolution == 0) {
            return false;
        }

        void* spectrumAMapped = nullptr;
        void* spectrumBMapped = nullptr;
        const D3D12_RANGE spectrumAReadRange{ 0, static_cast<SIZE_T>(request.spectrumAReadbackBytes) };
        const D3D12_RANGE spectrumBReadRange{ 0, static_cast<SIZE_T>(request.spectrumBReadbackBytes) };
        const HRESULT spectrumAHr = request.spectrumAReadbackBuffer->Map(0, &spectrumAReadRange, &spectrumAMapped);
        const HRESULT spectrumBHr = request.spectrumBReadbackBuffer->Map(0, &spectrumBReadRange, &spectrumBMapped);
        if (FAILED(spectrumAHr) || FAILED(spectrumBHr) || !spectrumAMapped || !spectrumBMapped) {
            if (request.isIfft) {
                Logger::GetInstance().Warnf(
                    LogCategory::Graphics,
                    LogSubCategory::Buffer,
                    "FFTOceanManager: ifft readback map failed. aHr={:#x} bHr={:#x}",
                    static_cast<uint32_t>(spectrumAHr),
                    static_cast<uint32_t>(spectrumBHr));
            } else {
                Logger::GetInstance().Warnf(
                    LogCategory::Graphics,
                    LogSubCategory::Buffer,
                    "FFTOceanManager: evolution readback map failed. aHr={:#x} bHr={:#x}",
                    static_cast<uint32_t>(spectrumAHr),
                    static_cast<uint32_t>(spectrumBHr));
            }
            return false;
        }

        const uint32_t centerX = request.resolution / 2;
        const uint32_t centerY = request.resolution / 2;
        const Float4DebugValue centerA = LoadFloat4(
            static_cast<const uint8_t*>(spectrumAMapped),
            centerX,
            centerY,
            request.spectrumALayout.Footprint.RowPitch);
        const Float4DebugValue centerB = LoadFloat4(
            static_cast<const uint8_t*>(spectrumBMapped),
            centerX,
            centerY,
            request.spectrumBLayout.Footprint.RowPitch);

        float maxAbsA0 = 0.0f;
        float maxAbsA1 = 0.0f;
        float maxAbsA2 = 0.0f;
        float maxAbsA3 = 0.0f;
        float maxAbsB0 = 0.0f;
        float maxAbsB1 = 0.0f;
        float maxAbsB2 = 0.0f;
        float maxAbsB3 = 0.0f;
        const uint8_t* spectrumABase = static_cast<const uint8_t*>(spectrumAMapped);
        const uint8_t* spectrumBBase = static_cast<const uint8_t*>(spectrumBMapped);
        for (uint32_t y = 0; y < request.resolution; ++y) {
            for (uint32_t x = 0; x < request.resolution; ++x) {
                const Float4DebugValue valueA = LoadFloat4(spectrumABase, x, y, request.spectrumALayout.Footprint.RowPitch);
                const Float4DebugValue valueB = LoadFloat4(spectrumBBase, x, y, request.spectrumBLayout.Footprint.RowPitch);
                maxAbsA0 = (std::max)(maxAbsA0, std::abs(valueA.x));
                maxAbsA1 = (std::max)(maxAbsA1, std::abs(valueA.y));
                maxAbsA2 = (std::max)(maxAbsA2, std::abs(valueA.z));
                maxAbsA3 = (std::max)(maxAbsA3, std::abs(valueA.w));
                maxAbsB0 = (std::max)(maxAbsB0, std::abs(valueB.x));
                maxAbsB1 = (std::max)(maxAbsB1, std::abs(valueB.y));
                maxAbsB2 = (std::max)(maxAbsB2, std::abs(valueB.z));
                maxAbsB3 = (std::max)(maxAbsB3, std::abs(valueB.w));
            }
        }

        request.spectrumAReadbackBuffer->Unmap(0, nullptr);
        request.spectrumBReadbackBuffer->Unmap(0, nullptr);

        if (request.isIfft) {
            Logger::GetInstance().Infof(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "FFTOceanManager: ifftReadback seq={} centerA=({:.6f}, {:.6f}, {:.6f}, {:.6f}) centerB=({:.6f}, {:.6f}, {:.6f}, {:.6f}) maxAbsA[x,y,z,w]=[{:.6f}, {:.6f}, {:.6f}, {:.6f}] maxAbsB[x,y,z,w]=[{:.6f}, {:.6f}, {:.6f}, {:.6f}]",
                request.sequence,
                centerA.x,
                centerA.y,
                centerA.z,
                centerA.w,
                centerB.x,
                centerB.y,
                centerB.z,
                centerB.w,
                maxAbsA0,
                maxAbsA1,
                maxAbsA2,
                maxAbsA3,
                maxAbsB0,
                maxAbsB1,
                maxAbsB2,
                maxAbsB3);
        } else {
            Logger::GetInstance().Infof(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "FFTOceanManager: evolutionReadback seq={} centerA=({:.6f}, {:.6f}, {:.6f}, {:.6f}) centerB=({:.6f}, {:.6f}, {:.6f}, {:.6f}) maxAbsA={:.6f} maxAbsB={:.6f}",
                request.sequence,
                centerA.x,
                centerA.y,
                centerA.z,
                centerA.w,
                centerB.x,
                centerB.y,
                centerB.z,
                centerB.w,
                (std::max)((std::max)(maxAbsA0, maxAbsA1), (std::max)(maxAbsA2, maxAbsA3)),
                (std::max)((std::max)(maxAbsB0, maxAbsB1), (std::max)(maxAbsB2, maxAbsB3)));
        }

        return true;
    }
}
