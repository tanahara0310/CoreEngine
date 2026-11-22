#include "pch.h"
#include "WaterFoamPattern.h"

#include <exception>

#include "Graphics/Texture/TextureManager.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    bool WaterFoamPattern::Initialize()
    {
        initialized_ = false;
        try {
            TextureManager& textures = TextureManager::GetInstance();
            netSRV_ = textures.Load(kNetTexture, TextureColorSpace::Linear).gpuHandle;
            grainSRV_ = textures.Load(kGrainTexture, TextureColorSpace::Linear).gpuHandle;
        } catch (const std::exception& e) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "WaterFoamPattern: 泡の模様の画像を読めないので、ボロノイの網目で描きます（{}）", e.what());
            return false;
        }
        initialized_ = netSRV_.ptr != 0 && grainSRV_.ptr != 0;
        return initialized_;
    }
}
