#include "pch.h"
#include "WaterShoreModule.h"

#include <algorithm>

#include "Camera/Camera.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Water/Foam/WaterShoreFoam.h"
#include "Graphics/Water/RayTracing/WaterSeabedRayTracingManager.h"
#include "Graphics/Water/Surface/WaterRenderResources.h"
#include "Graphics/Water/Surface/WaterShoreMeshDisplacement.h"
#include "Graphics/Water/Surface/WaterSurfaceComponent.h"
#include "Graphics/Water/Surface/WaterSurfaceMeshGenerator.h"
#include "Graphics/Water/System/WaterFrameContext.h"
#include "Graphics/Water/System/WaterSettings.h"
#include "Graphics/Water/Wave/WaterWaveModel.h"
#include "Scene/Feature/ISceneFeature.h"

namespace CoreEngine
{
    static_assert(WaterSeabedRayTracingManager::kResolution * WaterSeabedRayTracingManager::kTexelSize
            == WaterSurfaceMeshGenerator::kShoreSubdivisions * WaterSeabedRayTracingManager::kWindowSnapMeters,
        "岸の細かい格子の 1 マスは、海底の範囲の角を揃える格子の幅と同じ");

    void WaterShoreModule::UpdateWindow(const WaterFrameContext& frame, const WaterWaveCapabilities& waves)
    {
        window_ = {};
        WaterSeabedRayTracingManager* const seabed = frame.domain.GetRTEffects().Find<WaterSeabedRayTracingManager>();
        Camera* const camera = frame.scene.gameViewCamera3D;
        if (!seabed || !camera) {
            return;
        }

        const Vector3 cameraPosition = camera->GetPosition();
        const WaterSeabedWindow& window = seabed->UpdateWindow(cameraPosition.x, cameraPosition.z);
        if (!window.valid) {
            return;
        }

        window_.valid = true;
        window_.originXZ[0] = window.originX;
        window_.originXZ[1] = window.originZ;
        window_.size = window.size;
        window_.seabedSRV = seabed->GetSeabedSRVHandle(WaterSeabedRayTracingManager::ViewID::GameView);
        window_.shoreMeshEnabled = window_.seabedSRV.ptr != 0
            && waves.shoreMesh
            && frame.settings.shoreFineMeshEnabled;

        // 岸の泡。同じフレームの WaterShoreFoamPass が同じ範囲で書く
        if (const WaterShoreFoam* foam = frame.domain.GetWaterShoreFoam(); foam && foam->IsActive()) {
            window_.shoreFoamSRV = foam->GetSRVHandle();
        }

        // 岸の格子の頂点の変位。同じフレームの WaterShoreMeshDisplacementPass が同じ範囲で書く
        if (const WaterShoreMeshDisplacement* displacement = frame.domain.GetWaterShoreMeshDisplacement();
            displacement && displacement->IsInitialized()) {
            window_.shoreMeshDisplacementReady = true;
            window_.shoreMeshDisplacementSrvIndex = displacement->GetSrvHeapIndex();
        }
    }

    void WaterShoreModule::Capture(const WaterSurfaceComponent& waterPlane, WaterSurfaceData& snapshot) const
    {
        const bool seabedEnabled = window_.seabedSRV.ptr != 0 && window_.size > 0.0f;
        // RT と高さの問い合わせは岸の格子の形を頂点の変位から求めるので、変位を書けないときは粗い格子として扱う
        snapshot.shoreMeshEnabled =
            (window_.shoreMeshEnabled && seabedEnabled && window_.shoreMeshDisplacementReady) ? 1u : 0u;
        snapshot.shoreMeshDisplacementSrvIndex = window_.shoreMeshDisplacementSrvIndex;
        snapshot.shoreMeshOriginXZ[0] = window_.originXZ[0];
        snapshot.shoreMeshOriginXZ[1] = window_.originXZ[1];
        snapshot.shoreMeshSize = window_.size;
        snapshot.shoreMeshCellSize = window_.size / static_cast<float>(WaterSurfaceMeshGenerator::kShoreSubdivisions);
        const Vector2 coarseCellSize = waterPlane.GetMeshCellSizeXZ();
        snapshot.shoreMeshBlendWidth = (std::max)(coarseCellSize.x, coarseCellSize.y);
    }

    void WaterShoreModule::Bind(WaterFrameBinding& binding) const
    {
        if (!window_.valid) {
            return;
        }
        binding.resources.seabedHeightSRV = window_.seabedSRV;
        binding.seabedOriginXZ[0] = window_.originXZ[0];
        binding.seabedOriginXZ[1] = window_.originXZ[1];
        binding.seabedSize = window_.size;
        binding.shoreMeshEnabled = window_.shoreMeshEnabled;
        binding.resources.shoreFoamSRV = window_.shoreFoamSRV;
    }
}
