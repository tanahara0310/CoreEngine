#include "pch.h"
#include "VolumetricCloudManager.h"

#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Cloud/Settings/CloudCVars.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace CoreEngine
{
    namespace
    {
        /// @brief 配置ペイントの保存先（Config 層 = git 管理・チーム共有）
        constexpr const char* kWeatherPaintFilePath =
            "Application/Config/EngineSettings/CloudWeatherPaint.bin";

        /// @brief 配置ペイントの総バイト数（512²×RGBA8）
        constexpr size_t kWeatherPaintBytes = CloudResources::kPaintBytes;

        /// @brief 保存ファイルの先頭ヘッダ
        /// @details チャンネルの意味が変わったら version を上げる。読み込み側は
        ///          一致しないファイルを破棄するので、古い形式が誤って効くことはない
        struct WeatherPaintFileHeader {
            char     magic[8];      ///< "CLDPAINT"
            uint32_t version;
            uint32_t size;          ///< 一辺のテクセル数
        };
        constexpr uint32_t kWeatherPaintFileVersion = 2;
    }

    void VolumetricCloudManager::Initialize(GraphicsCore* graphicsCore, DescriptorAllocator* descriptorAllocator)
    {
        graphicsCore_ = graphicsCore;
        device_ = graphicsCore ? graphicsCore->GetDevice() : nullptr;
        descriptorAllocator_ = descriptorAllocator;

        ID3D12Device* device = device_;
        if (!device) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "VolumetricCloudManager: デバイスが無いため初期化を中止");
            return;
        }

        // CB を作る前に設定を取り込む（既定値の実体は CVar なので、読む前は全て 0）
        CloudCVars::LoadInto(parameters_);
        enabled_ = CloudCVars::Enabled.Get();

        // 雲定数バッファ（永続マップ）
        constantBuffer_ = ResourceFactory::CreateBufferResource(device, sizeof(VolumetricCloudShaderConstants));
        constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&constantData_));
        UploadConstants();

        // ゴッドレイ定数バッファ（永続マップ）
        godRayConstantBuffer_ = ResourceFactory::CreateBufferResource(device, sizeof(GodRayShaderConstants));
        godRayConstantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&godRayConstantData_));

        // 雲シャドウ定数バッファ（永続マップ）
        cloudShadowConstantBuffer_ = ResourceFactory::CreateBufferResource(device, sizeof(CloudShadowShaderConstants));
        cloudShadowConstantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&cloudShadowConstantData_));
        UploadCloudShadowConstants();

        // ノイズテクスチャ・配置ペイントと生成パイプライン
        const bool noiseResourcesReady = resources_.CreateNoiseTextures(device, descriptorAllocator)
            && resources_.CreateWeatherPaintTexture(device, descriptorAllocator);
        noisePipelinesReady_ = noiseResourcesReady && pipelines_.BuildNoisePasses(device);

        // ペイントレイヤの CPU 実体を用意し、保存済みのペイントがあれば復元する
        weatherPaintCpu_.assign(kWeatherPaintBytes, 0);
        if (noiseResourcesReady) {
            LoadWeatherPaint();
        }
        pipelinesReady_ = pipelines_.BuildRenderPasses(device);

        // ゴッドレイ（失敗しても雲本体は無効化しない）
        const bool godRayResourcesReady = resources_.CreateCloudShadowMap(device, descriptorAllocator);
        godRayPipelinesReady_ = godRayResourcesReady && pipelines_.BuildGodRayPasses(device);

        Logger::GetInstance().Infof(LogCategory::Graphics,
            "VolumetricCloudManager: 初期化完了 (雲底={:.0f}m, 層厚={:.0f}m, ノイズ生成={}, 描画={}, ゴッドレイ={})",
            parameters_.layerBottomAltitudeM, parameters_.layerThicknessM,
            noisePipelinesReady_ ? "OK" : "無効",
            pipelinesReady_ ? "OK" : "無効",
            godRayPipelinesReady_ ? "OK" : "無効");
    }

    void VolumetricCloudManager::SetEnabled(bool enabled)
    {
        // 実体は CVar が保持する。書き戻すことで UI 表示・自動保存にも反映される
        CloudCVars::Enabled.Set(enabled);
        enabled_ = enabled;
    }

    void VolumetricCloudManager::Update(const Vector3& cameraWorldPosition,
        const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix,
        const AtmosphereManager* atmosphereManager,
        float deltaTimeSec)
    {
        // 設定は CVar が保持する。UI・設定復元のどの経路で変わってもここで取り込む
        CloudCVars::LoadInto(parameters_);
        enabled_ = CloudCVars::Enabled.Get();

        // Update() を呼ぶのは雲を使うシーンのみ。このフレームは雲を有効にする。
        cloudsActive_ = true;

        // 風アニメーション用の時刻積算
        timeSec_ += deltaTimeSec;

        // 時間再投影の ping-pong とジッタ位相はフレーム番号の純粋関数にする
        prevViewProj_ = viewProj_;
        ++frameCounter_;
        resources_.SetFrameIndex(frameCounter_);

        // カメラ情報
        cameraWorldPos_ = cameraWorldPosition;
        viewProj_ = viewMatrix * projMatrix;
        invViewProj_ = MathCore::Matrix::Inverse(viewProj_);

        // カメラ前方向 = カメラワールド行列（ビュー行列の逆行列）の第 3 行（Z 軸）
        const Matrix4x4 cameraWorld = MathCore::Matrix::Inverse(viewMatrix);
        const float fx = cameraWorld.m[2][0];
        const float fy = cameraWorld.m[2][1];
        const float fz = cameraWorld.m[2][2];
        const float forwardLen = std::sqrt(fx * fx + fy * fy + fz * fz);
        if (forwardLen > 1e-6f) {
            cameraForward_ = { fx / forwardLen, fy / forwardLen, fz / forwardLen };
        }

        // 太陽・月情報・カメラ高度は AtmosphereManager から取得（単一情報源）。
        if (atmosphereManager) {
            sunDirection_ = atmosphereManager->GetSunDirection();
            const Vector4& sc = atmosphereManager->GetSunColor();
            sunColor_ = { sc.x, sc.y, sc.z };
            sunIntensity_ = atmosphereManager->GetSunIntensity();
            hasMoon_ = atmosphereManager->HasMoonLight();
            moonDirection_ = atmosphereManager->GetMoonDirection();
            const Vector4& mc = atmosphereManager->GetMoonColor();
            moonColor_ = { mc.x, mc.y, mc.z };
            moonIntensity_ = atmosphereManager->GetMoonIntensity();
            // 雲層シェルの原点と半径は大気と同じ値を使う（食い違うと雲底と地平線がずれる）
            planetRadiusM_ = atmosphereManager->GetParameters().planetRadius;
            groundLevelY_ = atmosphereManager->GetParameters().groundLevelY;
        }

        UploadConstants();
        UploadCloudShadowConstants();
    }

    void VolumetricCloudManager::PaintWeather(const WeatherPaintStamp& stamp)
    {
        if (weatherPaintCpu_.size() != kWeatherPaintBytes || !resources_.weatherPaintMapped) {
            return;
        }

        const float regionSize = std::max(parameters_.paintRegionSizeM, 1.0f);
        constexpr int kSize = static_cast<int>(CloudResources::kPaintSize);
        const float texelsPerM = kSize / regionSize;

        // ワールド座標 → 領域内テクセル座標
        const float centerTexX =
            (stamp.worldX - parameters_.paintRegionCenterX) * texelsPerM + kSize * 0.5f;
        const float centerTexY =
            (stamp.worldZ - parameters_.paintRegionCenterZ) * texelsPerM + kSize * 0.5f;
        const float radiusTexels = std::max(stamp.radiusM * texelsPerM, 1.0f);
        const int extent = static_cast<int>(std::ceil(radiusTexels));
        const int centerX = static_cast<int>(std::floor(centerTexX));
        const int centerY = static_cast<int>(std::floor(centerTexY));

        const float targets[3] = {
            std::clamp(stamp.coverage, 0.0f, 1.0f),
            std::clamp(stamp.cloudType, 0.0f, 1.0f),
            std::clamp(stamp.cloudTop, 0.0f, 1.0f),
        };

        for (int dy = -extent; dy <= extent; ++dy) {
            for (int dx = -extent; dx <= extent; ++dx) {
                const float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                if (dist > radiusTexels) {
                    continue;
                }
                // 縁が硬くならないよう smoothstep で減衰させる
                const float t = dist / radiusTexels;
                const float falloff = 1.0f - t * t * (3.0f - 2.0f * t);
                const float alpha = std::clamp(stamp.strength * falloff, 0.0f, 1.0f);
                if (alpha <= 0.0f) {
                    continue;
                }

                // 領域外へはみ出した分は捨てる（タイルしないので折り返さない）
                const int x = centerX + dx;
                const int y = centerY + dy;
                if (x < 0 || x >= kSize || y < 0 || y >= kSize) {
                    continue;
                }
                uint8_t* texel = weatherPaintCpu_.data() + (size_t(y) * kSize + x) * 4;

                if (stamp.erase) {
                    // 消しゴムは影響度だけを下げる（性質はそのまま残しても影響しない）
                    texel[3] = static_cast<uint8_t>(texel[3] * (1.0f - alpha) + 0.5f);
                    continue;
                }

                // スタンプ（性質, alpha）を既存ペイントへアルファ合成する。
                // 影響度で重み付けした平均を保つので、連続で塗ると目標値と影響度 1 へ収束する
                const float weight = texel[3] / 255.0f;
                const float newWeight = weight * (1.0f - alpha) + alpha;
                for (int ch = 0; ch < 3; ++ch) {
                    const float value = texel[ch] / 255.0f;
                    const float newValue = (newWeight > 1e-4f)
                        ? (value * weight * (1.0f - alpha) + targets[ch] * alpha) / newWeight
                        : targets[ch];
                    texel[ch] = static_cast<uint8_t>(std::clamp(newValue, 0.0f, 1.0f) * 255.0f + 0.5f);
                }
                texel[3] = static_cast<uint8_t>(std::clamp(newWeight, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        }

        UploadWeatherPaint();
    }

    bool VolumetricCloudManager::GetCameraAimOnCloudLayer(float& outX, float& outZ) const
    {
        // 雲層の中ほどを表す球殻とカメラの視線レイの交点を取る。
        // 惑星中心・半径はレイマーチと同じ規約（カメラ基準で真下 planetRadiusM）
        const Vector3 center = {
            cameraWorldPos_.x, groundLevelY_ - planetRadiusM_, cameraWorldPos_.z };
        const float radius = planetRadiusM_ + parameters_.layerBottomAltitudeM
            + parameters_.layerThicknessM * 0.5f;

        const Vector3 toOrigin = {
            cameraWorldPos_.x - center.x, cameraWorldPos_.y - center.y, cameraWorldPos_.z - center.z };
        const Vector3& dir = cameraForward_;
        const float b = dir.x * toOrigin.x + dir.y * toOrigin.y + dir.z * toOrigin.z;
        const float c = toOrigin.x * toOrigin.x + toOrigin.y * toOrigin.y + toOrigin.z * toOrigin.z
            - radius * radius;
        const float disc = b * b - c;
        if (disc < 0.0f) {
            return false;
        }

        const float sq = std::sqrt(disc);
        // 前方の交点を採る（カメラが層より下なら遠い方の解が層内へ入る点）
        float t = -b - sq;
        if (t <= 0.0f) {
            t = -b + sq;
        }
        if (t <= 0.0f) {
            return false;
        }

        outX = cameraWorldPos_.x + dir.x * t;
        outZ = cameraWorldPos_.z + dir.z * t;
        return true;
    }

    void VolumetricCloudManager::ClearWeatherPaint()
    {
        if (weatherPaintCpu_.size() != kWeatherPaintBytes) {
            return;
        }
        std::fill(weatherPaintCpu_.begin(), weatherPaintCpu_.end(), uint8_t(0));
        UploadWeatherPaint();

        std::error_code ec;
        std::filesystem::remove(kWeatherPaintFilePath, ec);
    }

    void VolumetricCloudManager::SaveWeatherPaint() const
    {
        if (weatherPaintCpu_.size() != kWeatherPaintBytes) {
            return;
        }

        // 空のペイントはファイルごと消す（1MB のゼロ埋めファイルをリポジトリに残さない）
        std::error_code ec;
        if (!weatherPaintUsed_) {
            std::filesystem::remove(kWeatherPaintFilePath, ec);
            return;
        }

        std::filesystem::create_directories(
            std::filesystem::path(kWeatherPaintFilePath).parent_path(), ec);
        std::ofstream file(kWeatherPaintFilePath, std::ios::binary | std::ios::trunc);
        if (!file) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "VolumetricCloudManager: 配置ペイントの保存に失敗 ({})", kWeatherPaintFilePath);
            return;
        }

        WeatherPaintFileHeader header{};
        std::memcpy(header.magic, "CLDPAINT", sizeof(header.magic));
        header.version = kWeatherPaintFileVersion;
        header.size = CloudResources::kPaintSize;
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.write(reinterpret_cast<const char*>(weatherPaintCpu_.data()),
            static_cast<std::streamsize>(weatherPaintCpu_.size()));
    }

    void VolumetricCloudManager::LoadWeatherPaint()
    {
        std::ifstream file(kWeatherPaintFilePath, std::ios::binary);
        if (!file) {
            return;
        }

        WeatherPaintFileHeader header{};
        if (!file.read(reinterpret_cast<char*>(&header), sizeof(header))
            || std::memcmp(header.magic, "CLDPAINT", sizeof(header.magic)) != 0
            || header.version != kWeatherPaintFileVersion
            || header.size != CloudResources::kPaintSize) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "VolumetricCloudManager: 配置ペイントの形式が一致しないため読み込みを破棄 ({})",
                kWeatherPaintFilePath);
            return;
        }

        if (!file.read(reinterpret_cast<char*>(weatherPaintCpu_.data()),
                static_cast<std::streamsize>(kWeatherPaintBytes))) {
            std::fill(weatherPaintCpu_.begin(), weatherPaintCpu_.end(), uint8_t(0));
            return;
        }
        UploadWeatherPaint();
    }

    void VolumetricCloudManager::UploadWeatherPaint()
    {
        if (!resources_.weatherPaintMapped || weatherPaintCpu_.size() != kWeatherPaintBytes) {
            return;
        }
        std::memcpy(resources_.weatherPaintMapped, weatherPaintCpu_.data(), kWeatherPaintBytes);

        // 使用中フラグは影響度が 1 テクセルでも残っているかで決める
        weatherPaintUsed_ = false;
        for (size_t i = 3; i < kWeatherPaintBytes; i += 4) {
            if (weatherPaintCpu_[i] != 0) {
                weatherPaintUsed_ = true;
                break;
            }
        }

        noiseBaker_.MarkPaintDirty();
    }

    void VolumetricCloudManager::UploadConstants()
    {
        if (!constantData_) {
            return;
        }

        VolumetricCloudShaderConstants c{};
        c.invViewProj = invViewProj_;
        c.cameraWorldPos = cameraWorldPos_;
        c.timeSec = timeSec_;
        c.sunDirection = sunDirection_;
        c.sunIntensity = sunIntensity_;
        c.sunColor = sunColor_;
        // 雲層の球殻交差用に惑星半径 [m] を渡す（大気 CB は km なので別値）
        c.planetRadiusM = planetRadiusM_;
        c.layerBottomAltitudeM = parameters_.layerBottomAltitudeM;
        c.layerThicknessM = parameters_.layerThicknessM;
        c.groundLevelY = groundLevelY_;
        c.globalCoverage = parameters_.globalCoverage;
        c.baseNoiseScaleM = parameters_.baseNoiseScaleM;
        c.detailNoiseScaleM = parameters_.detailNoiseScaleM;
        c.detailErosionStrength = parameters_.detailErosionStrength;
        c.densityScale = parameters_.densityScale;
        c.windDirX = parameters_.windDirX;
        c.windDirZ = parameters_.windDirZ;
        c.windSpeedMPerS = parameters_.windSpeedMPerS;
        c.weatherMapScaleM = parameters_.weatherMapScaleM;
        c.dropletDiameterUm = parameters_.dropletDiameterUm;
        c.maxPhase = parameters_.maxPhase;
        c.lightMarchConeSpread = parameters_.lightMarchConeSpread;
        c.ambientIntensity = parameters_.ambientIntensity;
        c.beerPowderStrength = parameters_.beerPowderStrength;
        c.lightMarchCoverage = parameters_.lightMarchCoverage;
        c.earlyExitTransmittance = parameters_.earlyExitTransmittance;
        c.maxMarchDistanceM = parameters_.maxMarchDistanceM;
        c.maxSteps = parameters_.maxSteps;
        c.outputWidth = resources_.TargetsWidth();
        c.outputHeight = resources_.TargetsHeight();
        c.frameIndex = frameCounter_;
        c.sunLightScale = parameters_.sunLightScale;
        c.msAttenuation = parameters_.msAttenuation;
        c.msContribution = parameters_.msContribution;
        c.msEccentricity = parameters_.msEccentricity;
        c.moonDirection = moonDirection_;
        c.moonIntensity = moonIntensity_;
        c.moonColor = moonColor_;
        c.hasMoon = hasMoon_ ? 1.0f : 0.0f;
        c.baseNoiseVerticalScale = parameters_.baseNoiseVerticalScale;
        c.heightSkewM = parameters_.heightSkewM;
        c.detailFadeDistanceM = parameters_.detailFadeDistanceM;
        c.farFadeWidthM = parameters_.farFadeWidthM;
        c.hazeDistanceM = parameters_.hazeDistanceM;
        c.maxSunOpticalDepth = parameters_.maxSunOpticalDepth;
        c.ambientCosZenith = parameters_.ambientCosZenith;
        c.ambientBottomOcclusion = parameters_.ambientBottomOcclusion;
        c.ambientChroma = parameters_.ambientChroma;
        c.ambientGroundStrength = parameters_.ambientGroundStrength;
        c.upsampleDepthTolerance = parameters_.upsampleDepthTolerance;
        c.cloudStreetStretch = std::max(parameters_.cloudStreetStretch, 1.0f);
        c.prevViewProj = prevViewProj_;
        // 起動直後とターゲット再確保直後は履歴が未初期化なので混ぜない
        c.reprojectEnabled = (parameters_.reprojectEnabled && historyValid_) ? 1.0f : 0.0f;
        c.reprojectBlendMin = parameters_.reprojectBlendMin;
        c.reprojectTolerance = std::max(parameters_.reprojectTolerance, 1e-4f);
        c.cloudTopVariation = parameters_.cloudTopVariation;
        // 巻雲は積雲層より上でないと前後関係の前提が崩れる
        c.cirrusAltitudeM = std::max(parameters_.cirrusAltitudeM,
            parameters_.layerBottomAltitudeM + parameters_.layerThicknessM);
        c.cirrusCoverage = parameters_.cirrusCoverage;
        c.cirrusDensity = parameters_.cirrusDensity;
        c.cirrusScaleM = parameters_.cirrusScaleM;
        c.cirrusStretch = parameters_.cirrusStretch;
        c.cirrusWindScale = parameters_.cirrusWindScale;
        c.noiseLodBias = parameters_.noiseLodBias;

        // ペイントが 1 テクセルも無いときは領域サイズ 0 を送る。
        // シェーダー側がサンプルごと省くので、使っていない間の追加コストはゼロになる
        c.paintRegionCenterX = parameters_.paintRegionCenterX;
        c.paintRegionCenterZ = parameters_.paintRegionCenterZ;
        c.paintRegionSizeM = weatherPaintUsed_ ? parameters_.paintRegionSizeM : 0.0f;
        c.paintEdgeFade = parameters_.paintEdgeFade;
        c.pad7 = 0.0f;

        *constantData_ = c;
    }

    void VolumetricCloudManager::UploadGodRayConstants()
    {
        if (!godRayConstantData_) {
            return;
        }

        GodRayShaderConstants g{};
        g.invViewProj = invViewProj_;
        g.cameraWorldPos = cameraWorldPos_;
        g.maxDistanceM = parameters_.godRayMaxDistanceM;
        g.intensity = parameters_.godRayIntensity;
        g.mieBoost = parameters_.godRayMieBoost;
        // 地表 Y は雲 CB と同じ値を使う（食い違うとシャドウ基準面が雲底からずれる）
        g.groundLevelY = groundLevelY_;
        g.pad1 = 0.0f;
        g.stepCount = std::max(parameters_.godRayStepCount, 1u);
        g.outputWidth = resources_.TargetsWidth();
        g.outputHeight = resources_.TargetsHeight();

        *godRayConstantData_ = g;
    }

    void VolumetricCloudManager::UploadCloudShadowConstants()
    {
        if (!cloudShadowConstantData_) {
            return;
        }

        CloudShadowShaderConstants s{};

        // 範囲の中心はテクセルサイズへスナップする（カメラ移動での泳ぎ防止）
        const float texelM = parameters_.cloudShadowRegionSizeM
            / static_cast<float>(CloudResources::kCloudShadowMapSize);
        s.regionCenterX = std::floor(cameraWorldPos_.x / texelM) * texelM;
        s.regionCenterZ = std::floor(cameraWorldPos_.z / texelM) * texelM;
        s.regionSizeM = parameters_.cloudShadowRegionSizeM;
        s.anchorWorldY = groundLevelY_ + parameters_.layerBottomAltitudeM;
        s.edgeFadeStart = 0.8f;
        s.sceneStrength = parameters_.sceneShadowStrength;
        s.pad0 = 0.0f;
        s.pad1 = 0.0f;

        *cloudShadowConstantData_ = s;
    }

    CloudRenderContext VolumetricCloudManager::MakeRenderContext(
        ID3D12GraphicsCommandList* cmdList, const AtmosphereManager* atmosphereManager,
        GpuTimestampProfiler* profiler)
    {
        CloudRenderContext ctx{};
        ctx.cmdList = cmdList;
        ctx.resources = &resources_;
        ctx.pipelines = &pipelines_;
        ctx.atmosphere = atmosphereManager;
        ctx.profiler = profiler;
        ctx.cloudConstants = constantBuffer_ ? constantBuffer_->GetGPUVirtualAddress() : 0;
        ctx.godRayConstants = godRayConstantBuffer_ ? godRayConstantBuffer_->GetGPUVirtualAddress() : 0;
        ctx.cloudShadowConstants = cloudShadowConstantBuffer_ ? cloudShadowConstantBuffer_->GetGPUVirtualAddress() : 0;
        return ctx;
    }

    bool VolumetricCloudManager::EnsureFrameTargets(GpuResource& sceneColor)
    {
        bool recreated = false;
        const bool ok = resources_.EnsureFrameTargets(device_, descriptorAllocator_, graphicsCore_,
            sceneColor, parameters_.resolutionDivisor, &recreated);
        // 作り直した直後の履歴バッファは未初期化。1 フレーム書き込むまで混ぜない
        if (!ok || recreated) {
            historyValid_ = false;
        }
        return ok;
    }

    void VolumetricCloudManager::GenerateNoiseTexturesIfNeeded(
        ID3D12GraphicsCommandList* cmdList, GpuTimestampProfiler* profiler)
    {
        if (!cmdList || !noisePipelinesReady_) {
            return;
        }
        noiseBaker_.BakeIfNeeded(MakeRenderContext(cmdList, nullptr, profiler));
    }

    void VolumetricCloudManager::RenderCloudShadowMap(
        ID3D12GraphicsCommandList* cmdList,
        const AtmosphereManager* atmosphereManager,
        GpuTimestampProfiler* profiler)
    {
        if (!cmdList || !godRayPipelinesReady_ || !noiseBaker_.IsReady()) {
            return;
        }
        cloudShadowMapRenderer_.Render(MakeRenderContext(cmdList, atmosphereManager, profiler));
    }

    void VolumetricCloudManager::RenderClouds(
        ID3D12GraphicsCommandList* cmdList,
        GpuResource& sceneColor,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorUavHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE depthSrvHandle,
        const AtmosphereManager* atmosphereManager,
        GpuTimestampProfiler* profiler)
    {
        if (!cmdList || !pipelinesReady_ || !noiseBaker_.IsReady() || !atmosphereManager) {
            return;
        }
        if (!EnsureFrameTargets(sceneColor)) {
            return;
        }

        // 出力サイズ（半解像度）を CB へ反映してから Dispatch する
        UploadConstants();

        cloudRenderer_.Render(MakeRenderContext(cmdList, atmosphereManager, profiler),
            sceneColor, sceneColorUavHandle, depthSrvHandle);

        // 今フレームの書き込みで履歴が揃った（次フレームから混ぜられる）
        historyValid_ = true;
    }

    void VolumetricCloudManager::RenderCloudsToSkyCubemap(
        ID3D12GraphicsCommandList* cmdList,
        const AtmosphereManager* atmosphereManager,
        GpuTimestampProfiler* profiler)
    {
        if (!cmdList || !pipelinesReady_ || !noiseBaker_.IsReady() || !atmosphereManager) {
            return;
        }
        skyCubemapBaker_.Bake(MakeRenderContext(cmdList, atmosphereManager, profiler));
    }

    void VolumetricCloudManager::RenderGodRays(
        ID3D12GraphicsCommandList* cmdList,
        GpuResource& sceneColor,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorUavHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE depthSrvHandle,
        const AtmosphereManager* atmosphereManager,
        GpuTimestampProfiler* profiler)
    {
        if (!cmdList || !godRayPipelinesReady_ || !noiseBaker_.IsReady() || !atmosphereManager) {
            return;
        }
        if (!parameters_.godRayEnabled) {
            return;
        }
        if (!EnsureFrameTargets(sceneColor)) {
            return;
        }

        // 出力サイズ（半解像度）確定後に CB を更新する
        UploadGodRayConstants();

        godRayRenderer_.Render(MakeRenderContext(cmdList, atmosphereManager, profiler),
            sceneColor, sceneColorUavHandle, depthSrvHandle);
    }
}
