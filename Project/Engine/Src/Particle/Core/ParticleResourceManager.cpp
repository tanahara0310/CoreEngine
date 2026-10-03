#include "pch.h"
#include "ParticleResourceManager.h"


namespace CoreEngine
{
void ParticleResourceManager::Initialize(GraphicsCore* dxCommon, uint32_t maxInstances) {
    instancing_.Initialize(*dxCommon, maxInstances, "ParticleInstancingSRV");
}
}
