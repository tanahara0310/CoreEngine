#include "ParticlePresetManager.h"
#include "ParticleSystem.h"
#include <filesystem>
#include <iostream>

#ifdef _DEBUG
#include "Engine/Utility/Debug/ImGui/ImguiManager.h"
#endif

// Windows.hのマクロ干渉を回避
#ifdef CreateDirectory
#undef CreateDirectory
#endif

// TODO: MainModule対応のため、Save/Load機能は一時的に無効化
// 後でMainModuleからデータを取得・設定するように修正する必要があります


namespace CoreEngine
{
bool ParticlePresetManager::SavePreset(const ParticleSystem* particleSystem, const std::string& filePath)
{
	json presetData;

	// エミッター位置の保存
	presetData["emitterPosition"] = JsonManager::Vector3ToJson(particleSystem->GetEmitterPosition());

	// ビルボードタイプとブレンドモードの保存
	presetData["billboardType"] = static_cast<int>(particleSystem->GetBillboardType());
	presetData["blendMode"] = static_cast<int>(particleSystem->GetBlendMode());

	// MainModuleの保存
	auto& mainModule = const_cast<ParticleSystem*>(particleSystem)->GetMainModule();
	auto mainData = mainModule.GetMainData();
	json mainJson;
	mainJson["duration"] = mainData.duration;
	mainJson["looping"] = mainData.looping;
	mainJson["playOnAwake"] = mainData.playOnAwake;
	mainJson["maxParticles"] = mainData.maxParticles;
	mainJson["simulationSpace"] = static_cast<int>(mainData.simulationSpace);
	mainJson["startLifetime"] = mainData.startLifetime;
	mainJson["startLifetimeRandomness"] = mainData.startLifetimeRandomness;
	mainJson["startSpeed"] = mainData.startSpeed;
	mainJson["startSpeedRandomness"] = mainData.startSpeedRandomness;
	mainJson["startSize"] = JsonManager::Vector3ToJson(mainData.startSize);
	mainJson["startSizeRandomness"] = mainData.startSizeRandomness;
	mainJson["startRotation"] = JsonManager::Vector3ToJson(mainData.startRotation);
	mainJson["startRotationRandomness"] = mainData.startRotationRandomness;
	mainJson["startColor"] = JsonManager::Vector4ToJson(mainData.startColor);
	mainJson["startColorRandomness"] = mainData.startColorRandomness;
	mainJson["gravityModifier"] = mainData.gravityModifier;
	presetData["main"] = mainJson;

	// EmissionModuleの保存
	auto& emissionModule = const_cast<ParticleSystem*>(particleSystem)->GetEmissionModule();
	auto emissionData = emissionModule.GetEmissionData();
	json emissionJson;
	emissionJson["rateOverTime"] = emissionData.rateOverTime;
	emissionJson["burstCount"] = emissionData.burstCount;
	emissionJson["burstTime"] = emissionData.burstTime;
	presetData["emission"] = emissionJson;

	// ShapeModuleの保存
	auto& shapeModule = const_cast<ParticleSystem*>(particleSystem)->GetShapeModule();
	auto shapeData = shapeModule.GetShapeData();
	json shapeJson;
	shapeJson["shapeType"] = static_cast<int>(shapeData.shapeType);
	shapeJson["scale"] = JsonManager::Vector3ToJson(shapeData.scale);
	shapeJson["radius"] = shapeData.radius;
	shapeJson["innerRadius"] = shapeData.innerRadius;
	shapeJson["height"] = shapeData.height;
	shapeJson["angle"] = shapeData.angle;
	shapeJson["randomPositionRange"] = shapeData.randomPositionRange;
	shapeJson["emitFromSurface"] = shapeData.emitFromSurface;
	shapeJson["emissionDirection"] = JsonManager::Vector3ToJson(shapeData.emissionDirection);
	shapeJson["circlePlane"] = static_cast<int>(shapeData.circlePlane);
	presetData["shape"] = shapeJson;

	// VelocityModuleの保存
	auto& velocityModule = const_cast<ParticleSystem*>(particleSystem)->GetVelocityModule();
	auto velocityData = velocityModule.GetVelocityData();
	json velocityJson;
	velocityJson["startSpeed"] = JsonManager::Vector3ToJson(velocityData.startSpeed);
	velocityJson["randomSpeedRange"] = JsonManager::Vector3ToJson(velocityData.randomSpeedRange);
	velocityJson["useRandomDirection"] = velocityData.useRandomDirection;
	presetData["velocity"] = velocityJson;

	// ColorModuleの保存
	auto& colorModule = const_cast<ParticleSystem*>(particleSystem)->GetColorModule();
	auto colorData = colorModule.GetColorData();
	json colorJson;
	colorJson["endColor"] = JsonManager::Vector4ToJson(colorData.endColor);
	colorJson["useGradient"] = colorData.useGradient;
	presetData["color"] = colorJson;

	// ForceModuleの保存
	auto& forceModule = const_cast<ParticleSystem*>(particleSystem)->GetForceModule();
	auto forceData = forceModule.GetForceData();
	json forceJson;
	forceJson["gravity"] = JsonManager::Vector3ToJson(forceData.gravity);
	forceJson["wind"] = JsonManager::Vector3ToJson(forceData.wind);
	forceJson["drag"] = forceData.drag;
	forceJson["acceleration"] = JsonManager::Vector3ToJson(forceData.acceleration);
	forceJson["areaMin"] = JsonManager::Vector3ToJson(forceData.area.min);
	forceJson["areaMax"] = JsonManager::Vector3ToJson(forceData.area.max);
	forceJson["useAccelerationField"] = forceData.useAccelerationField;
	presetData["force"] = forceJson;

	// SizeModuleの保存
	auto& sizeModule = const_cast<ParticleSystem*>(particleSystem)->GetSizeModule();
	auto sizeData = sizeModule.GetSizeData();
	json sizeJson;
	sizeJson["endSize"] = sizeData.endSize;
	sizeJson["sizeOverLifetime"] = sizeData.sizeOverLifetime;
	sizeJson["endSize3D"] = JsonManager::Vector3ToJson(sizeData.endSize3D);
	sizeJson["use3DSize"] = sizeData.use3DSize;
	sizeJson["sizeCurve"] = static_cast<int>(sizeData.sizeCurve);
	sizeJson["minSize"] = sizeData.minSize;
	sizeJson["maxSize"] = sizeData.maxSize;
	sizeJson["uniformScaling"] = sizeData.uniformScaling;
	presetData["size"] = sizeJson;

	// RotationModuleの保存
	auto& rotationModule = const_cast<ParticleSystem*>(particleSystem)->GetRotationModule();
	auto rotationData = rotationModule.GetRotationData();
	json rotationJson;
	rotationJson["rotationSpeed"] = JsonManager::Vector3ToJson(rotationData.rotationSpeed);
	rotationJson["rotationSpeedRandomness"] = JsonManager::Vector3ToJson(rotationData.rotationSpeedRandomness);
	rotationJson["use2DRotation"] = rotationData.use2DRotation;
	rotationJson["rotation2DSpeed"] = rotationData.rotation2DSpeed;
	rotationJson["rotation2DSpeedRandomness"] = rotationData.rotation2DSpeedRandomness;
	rotationJson["rotationDirection"] = static_cast<int>(rotationData.rotationDirection);
	rotationJson["rotationOverLifetime"] = rotationData.rotationOverLifetime;
	rotationJson["startRotationSpeedMultiplier"] = rotationData.startRotationSpeedMultiplier;
	rotationJson["endRotationSpeedMultiplier"] = rotationData.endRotationSpeedMultiplier;
	rotationJson["limitRotationRange"] = rotationData.limitRotationRange;
	rotationJson["minRotation"] = JsonManager::Vector3ToJson(rotationData.minRotation);
	rotationJson["maxRotation"] = JsonManager::Vector3ToJson(rotationData.maxRotation);
	rotationJson["alignToVelocity"] = rotationData.alignToVelocity;
	rotationJson["velocityAlignmentStrength"] = rotationData.velocityAlignmentStrength;
	presetData["rotation"] = rotationJson;

	// NoiseModuleの保存
	auto& noiseModule = const_cast<ParticleSystem*>(particleSystem)->GetNoiseModule();
	auto noiseData = noiseModule.GetNoiseData();
	json noiseJson;
	noiseJson["strength"] = noiseData.strength;
	noiseJson["frequency"] = noiseData.frequency;
	noiseJson["scrollSpeed"] = noiseData.scrollSpeed;
	noiseJson["damping"] = noiseData.damping;
	noiseJson["positionAmount"] = JsonManager::Vector3ToJson(noiseData.positionAmount);
	presetData["noise"] = noiseJson;

	// メタデータ
	presetData["version"] = "2.0";  // MainModule対応版

	// ファイルに保存
	bool success = JsonManager::GetInstance().SaveJson(filePath, presetData);
	if (success) {
		std::cout << "Preset saved (v2.0 - MainModule): " << filePath << std::endl;
		needUpdateFileList_ = true;

		// 保存したファイルを現在のプリセットとして設定
		currentPresetPath_ = filePath;
		currentPresetName_ = GetFileNameWithoutExtension(std::filesystem::path(filePath).filename().string());
	} else {
		std::cerr << "Failed to save preset: " << filePath << std::endl;
	}

	return success;
}

bool ParticlePresetManager::LoadPreset(ParticleSystem* particleSystem, const std::string& filePath)
{
	// ファイルが存在するかチェック
	if (!JsonManager::GetInstance().FileExists(filePath)) {
		std::cerr << "Preset file not found: " << filePath << std::endl;
		return false;
	}

	// ファイルを読み込み
	json presetData = JsonManager::GetInstance().LoadJson(filePath);
	if (presetData.empty()) {
		std::cerr << "Failed to load preset: " << filePath << std::endl;
		return false;
	}

	// バージョンチェック
	std::string version = "1.0";
if (presetData.contains("version")) {
		version = presetData["version"].get<std::string>();
	}
	std::cout << "Loading preset version: " << version << std::endl;

	// エミッター位置の読み込み
	if (presetData.contains("emitterPosition")) {
		Vector3 position = JsonManager::JsonToVector3(presetData["emitterPosition"]);
		particleSystem->SetEmitterPosition(position);
	}

	// ビルボードタイプとブレンドモードの読み込み
	if (presetData.contains("billboardType")) {
		particleSystem->SetBillboardType(static_cast<BillboardType>(presetData["billboardType"].get<int>()));
	}
	if (presetData.contains("blendMode")) {
		particleSystem->SetBlendMode(static_cast<BlendMode>(presetData["blendMode"].get<int>()));
	}

	// MainModuleの読み込み
	if (presetData.contains("main")) {
		auto mainJson = presetData["main"];
		auto& mainModule = particleSystem->GetMainModule();
		auto& mainData = mainModule.GetMainData();

		mainData.duration = JsonManager::SafeGet(mainJson, "duration", 5.0f);
		mainData.looping = JsonManager::SafeGet(mainJson, "looping", true);
		mainData.playOnAwake = JsonManager::SafeGet(mainJson, "playOnAwake", true);
		mainData.maxParticles = JsonManager::SafeGet(mainJson, "maxParticles", 1000u);
		mainData.simulationSpace = static_cast<MainModule::SimulationSpace>(
			JsonManager::SafeGet(mainJson, "simulationSpace", 0));

		mainData.startLifetime = JsonManager::SafeGet(mainJson, "startLifetime", 1.0f);
		mainData.startLifetimeRandomness = JsonManager::SafeGet(mainJson, "startLifetimeRandomness", 0.0f);

		mainData.startSpeed = JsonManager::SafeGet(mainJson, "startSpeed", 5.0f);
		mainData.startSpeedRandomness = JsonManager::SafeGet(mainJson, "startSpeedRandomness", 0.0f);

		mainData.startSize = JsonManager::SafeGetVector3(mainJson, "startSize", { 1.0f, 1.0f, 1.0f });
		mainData.startSizeRandomness = JsonManager::SafeGet(mainJson, "startSizeRandomness", 0.0f);

		mainData.startRotation = JsonManager::SafeGetVector3(mainJson, "startRotation", { 0.0f, 0.0f, 0.0f });
		mainData.startRotationRandomness = JsonManager::SafeGet(mainJson, "startRotationRandomness", 0.0f);

		mainData.startColor = JsonManager::SafeGetVector4(mainJson, "startColor", { 1.0f, 1.0f, 1.0f, 1.0f });
		mainData.startColorRandomness = JsonManager::SafeGet(mainJson, "startColorRandomness", 0.0f);

		mainData.gravityModifier = JsonManager::SafeGet(mainJson, "gravityModifier", 0.0f);
	}

	// EmissionModuleの読み込み
	if (presetData.contains("emission")) {
		auto emissionJson = presetData["emission"];
		EmissionModule::EmissionData emissionData;
		emissionData.rateOverTime = JsonManager::SafeGet(emissionJson, "rateOverTime", 10u);
		emissionData.burstCount = JsonManager::SafeGet(emissionJson, "burstCount", 0u);
		emissionData.burstTime = JsonManager::SafeGet(emissionJson, "burstTime", 0.0f);
		particleSystem->GetEmissionModule().SetEmissionData(emissionData);
	}

	// ShapeModuleの読み込み
	if (presetData.contains("shape")) {
		auto shapeJson = presetData["shape"];
		ShapeModule::ShapeData shapeData;
		shapeData.shapeType = static_cast<ShapeModule::ShapeType>(
			JsonManager::SafeGet(shapeJson, "shapeType", 0));
		shapeData.scale = JsonManager::SafeGetVector3(shapeJson, "scale", { 1.0f, 1.0f, 1.0f });
		shapeData.radius = JsonManager::SafeGet(shapeJson, "radius", 1.0f);
		shapeData.innerRadius = JsonManager::SafeGet(shapeJson, "innerRadius", 0.5f);
		shapeData.height = JsonManager::SafeGet(shapeJson, "height", 2.0f);
		shapeData.angle = JsonManager::SafeGet(shapeJson, "angle", 25.0f);
		shapeData.randomPositionRange = JsonManager::SafeGet(shapeJson, "randomPositionRange", 0.0f);
		shapeData.emitFromSurface = JsonManager::SafeGet(shapeJson, "emitFromSurface", false);
		shapeData.emissionDirection = JsonManager::SafeGetVector3(shapeJson, "emissionDirection", { 0.0f, 1.0f, 0.0f });
		shapeData.circlePlane = static_cast<ShapeModule::CirclePlane>(
			JsonManager::SafeGet(shapeJson, "circlePlane", 0));
		particleSystem->GetShapeModule().SetShapeData(shapeData);
	}

	// VelocityModuleの読み込み
	if (presetData.contains("velocity")) {
		auto velocityJson = presetData["velocity"];
		VelocityModule::VelocityData velocityData;
		velocityData.startSpeed = JsonManager::SafeGetVector3(velocityJson, "startSpeed", { 0.0f, 1.0f, 0.0f });
		velocityData.randomSpeedRange = JsonManager::SafeGetVector3(velocityJson, "randomSpeedRange", { 1.0f, 1.0f, 1.0f });
		velocityData.useRandomDirection = JsonManager::SafeGet(velocityJson, "useRandomDirection", true);
		particleSystem->GetVelocityModule().SetVelocityData(velocityData);
	}

	// ColorModuleの読み込み
	if (presetData.contains("color")) {
		auto colorJson = presetData["color"];
		ColorModule::ColorOverLifetime colorData;
		colorData.endColor = JsonManager::SafeGetVector4(colorJson, "endColor", { 1.0f, 1.0f, 1.0f, 0.0f });
		colorData.useGradient = JsonManager::SafeGet(colorJson, "useGradient", true);
		particleSystem->GetColorModule().SetColorData(colorData);
	}

	// ForceModuleの読み込み
	if (presetData.contains("force")) {
		auto forceJson = presetData["force"];
		ForceModule::ForceData forceData;
		forceData.gravity = JsonManager::SafeGetVector3(forceJson, "gravity", { 0.0f, -9.8f, 0.0f });
		forceData.wind = JsonManager::SafeGetVector3(forceJson, "wind", { 0.0f, 0.0f, 0.0f });
		forceData.drag = JsonManager::SafeGet(forceJson, "drag", 0.0f);
		forceData.acceleration = JsonManager::SafeGetVector3(forceJson, "acceleration", { 0.0f, 0.0f, 0.0f });
		forceData.area.min = JsonManager::SafeGetVector3(forceJson, "areaMin", { -1.0f, -1.0f, -1.0f });
		forceData.area.max = JsonManager::SafeGetVector3(forceJson, "areaMax", { 1.0f, 1.0f, 1.0f });
		forceData.useAccelerationField = JsonManager::SafeGet(forceJson, "useAccelerationField", false);
		particleSystem->GetForceModule().SetForceData(forceData);
	}

	// SizeModuleの読み込み
	if (presetData.contains("size")) {
		auto sizeJson = presetData["size"];
		SizeModule::SizeData sizeData;
		sizeData.endSize = JsonManager::SafeGet(sizeJson, "endSize", 0.0f);
		sizeData.sizeOverLifetime = JsonManager::SafeGet(sizeJson, "sizeOverLifetime", true);
		sizeData.endSize3D = JsonManager::SafeGetVector3(sizeJson, "endSize3D", { 0.0f, 0.0f, 0.0f });
		sizeData.use3DSize = JsonManager::SafeGet(sizeJson, "use3DSize", false);
		sizeData.sizeCurve = static_cast<SizeModule::SizeData::SizeCurve>(
			JsonManager::SafeGet(sizeJson, "sizeCurve", 0));
		sizeData.minSize = JsonManager::SafeGet(sizeJson, "minSize", 0.01f);
		sizeData.maxSize = JsonManager::SafeGet(sizeJson, "maxSize", 10.0f);
		sizeData.uniformScaling = JsonManager::SafeGet(sizeJson, "uniformScaling", true);
		particleSystem->GetSizeModule().SetSizeData(sizeData);
	}

	// RotationModuleの読み込み
	if (presetData.contains("rotation")) {
		auto rotationJson = presetData["rotation"];
		RotationModule::RotationData rotationData;
		rotationData.rotationSpeed = JsonManager::SafeGetVector3(rotationJson, "rotationSpeed", { 0.0f, 0.0f, 0.0f });
		rotationData.rotationSpeedRandomness = JsonManager::SafeGetVector3(rotationJson, "rotationSpeedRandomness", { 0.0f, 0.0f, 0.0f });
		rotationData.use2DRotation = JsonManager::SafeGet(rotationJson, "use2DRotation", true);
		rotationData.rotation2DSpeed = JsonManager::SafeGet(rotationJson, "rotation2DSpeed", 0.0f);
		rotationData.rotation2DSpeedRandomness = JsonManager::SafeGet(rotationJson, "rotation2DSpeedRandomness", 0.0f);
		rotationData.rotationDirection = static_cast<RotationModule::RotationData::RotationDirection>(
			JsonManager::SafeGet(rotationJson, "rotationDirection", 2));
		rotationData.rotationOverLifetime = JsonManager::SafeGet(rotationJson, "rotationOverLifetime", false);
		rotationData.startRotationSpeedMultiplier = JsonManager::SafeGet(rotationJson, "startRotationSpeedMultiplier", 1.0f);
		rotationData.endRotationSpeedMultiplier = JsonManager::SafeGet(rotationJson, "endRotationSpeedMultiplier", 1.0f);
		rotationData.limitRotationRange = JsonManager::SafeGet(rotationJson, "limitRotationRange", false);
		rotationData.minRotation = JsonManager::SafeGetVector3(rotationJson, "minRotation", { -180.0f, -180.0f, -180.0f });
		rotationData.maxRotation = JsonManager::SafeGetVector3(rotationJson, "maxRotation", { 180.0f, 180.0f, 180.0f });
		rotationData.alignToVelocity = JsonManager::SafeGet(rotationJson, "alignToVelocity", false);
		rotationData.velocityAlignmentStrength = JsonManager::SafeGet(rotationJson, "velocityAlignmentStrength", 1.0f);
		particleSystem->GetRotationModule().SetRotationData(rotationData);
	}

	// NoiseModuleの読み込み
	if (presetData.contains("noise")) {
		auto noiseJson = presetData["noise"];
		NoiseModule::NoiseData noiseData;
		noiseData.strength = JsonManager::SafeGet(noiseJson, "strength", 1.0f);
		noiseData.frequency = JsonManager::SafeGet(noiseJson, "frequency", 1.0f);
		noiseData.scrollSpeed = JsonManager::SafeGet(noiseJson, "scrollSpeed", 0.0f);
		noiseData.damping = JsonManager::SafeGet(noiseJson, "damping", true);
		noiseData.positionAmount = JsonManager::SafeGetVector3(noiseJson, "positionAmount", { 1.0f, 1.0f, 1.0f });
		particleSystem->GetNoiseModule().SetNoiseData(noiseData);
	}

	// 現在のプリセット情報を保存
	currentPresetPath_ = filePath;
	currentPresetName_ = GetFileNameWithoutExtension(std::filesystem::path(filePath).filename().string());

	std::cout << "Preset loaded (v" << version << "): " << filePath << std::endl;
	return true;
}

std::vector<std::string> ParticlePresetManager::GetPresetList(const std::string& directory)
{
	std::vector<std::string> fileList;

	try {
		if (!std::filesystem::exists(directory)) {
			return fileList;
		}

		for (const auto& entry : std::filesystem::directory_iterator(directory)) {
			if (entry.is_regular_file() && entry.path().extension() == ".json") {
				fileList.push_back(entry.path().filename().string());
			}
		}
	}
	catch (const std::exception& e) {
		std::cerr << "Error listing preset files: " << e.what() << std::endl;
	}

	return fileList;
}

void ParticlePresetManager::ShowImGui(ParticleSystem* particleSystem)
{
#ifdef _DEBUG
	if (ImGui::CollapsingHeader("プリセット管理")) {
		// キーボードショートカット: Ctrl+S で上書き保存
		if (!currentPresetPath_.empty() && ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_S)) {
			if (SaveCurrentPreset(particleSystem)) {
				ImGui::OpenPopup("上書き保存成功");
			} else {
				ImGui::OpenPopup("上書き保存失敗");
			}
		}

		// 現在読み込まれているプリセット情報を表示
		if (!currentPresetPath_.empty()) {
			ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "現在のプリセット: %s", currentPresetName_.c_str());
			ImGui::Text("パス: %s", currentPresetPath_.c_str());

			// 上書き保存ボタン
			if (ImGui::Button("上書き保存 (Ctrl+S)", ImVec2(200, 0))) {
				if (SaveCurrentPreset(particleSystem)) {
					ImGui::OpenPopup("上書き保存成功");
				} else {
					ImGui::OpenPopup("上書き保存失敗");
				}
			}

			ImGui::SameLine();

			// プリセットをクリア
			if (ImGui::Button("プリセットをクリア", ImVec2(150, 0))) {
				currentPresetPath_.clear();
				currentPresetName_.clear();
			}

			ImGui::Separator();
		} else {
			ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.0f, 1.0f), "現在のプリセット: なし");
			ImGui::Separator();
		}

		// 上書き保存成功ポップアップ
		if (ImGui::BeginPopupModal("上書き保存成功", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("プリセットを上書き保存しました。");
			if (ImGui::Button("OK", ImVec2(120, 0))) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// 上書き保存失敗ポップアップ
		if (ImGui::BeginPopupModal("上書き保存失敗", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("プリセットの上書き保存に失敗しました。");
			if (ImGui::Button("OK", ImVec2(120, 0))) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		ImGui::Separator();

		// ディレクトリパス設定
		ImGui::Text("保存先ディレクトリ");
		if (ImGui::InputText("##Directory", directoryPathBuffer_, sizeof(directoryPathBuffer_))) {
			needUpdateFileList_ = true;
		}

		ImGui::Separator();

		// 保存セクション
		ImGui::Text("=== 保存 ===");
		ImGui::InputText("ファイル名", saveFileNameBuffer_, sizeof(saveFileNameBuffer_));

		if (ImGui::Button("プリセットを保存", ImVec2(200, 0))) {
			std::string fileName = std::string(saveFileNameBuffer_);
			if (!fileName.empty()) {
				// 拡張子がない場合は追加
				if (fileName.find(".json") == std::string::npos) {
					fileName += ".json";
				}

			std::string fullPath = std::string(directoryPathBuffer_) + fileName;

			// ディレクトリが存在しない場合は作成
			JsonManager::GetInstance().CreateJsonDirectory(directoryPathBuffer_);

			if (SavePreset(particleSystem, fullPath)) {
					ImGui::OpenPopup("保存成功");
				} else {
					ImGui::OpenPopup("保存失敗");
				}
			}
		}

		// 保存成功ポップアップ
		if (ImGui::BeginPopupModal("保存成功", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("プリセットを保存しました。");
			if (ImGui::Button("OK", ImVec2(120, 0))) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// 保存失敗ポップアップ
		if (ImGui::BeginPopupModal("保存失敗", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("プリセットの保存に失敗しました。");
			if (ImGui::Button("OK", ImVec2(120, 0))) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		ImGui::Separator();

		// 読み込みセクション
		ImGui::Text("=== 読み込み ===");

		// ファイルリスト更新ボタン
		if (ImGui::Button("リストを更新") || needUpdateFileList_) {
			UpdatePresetFileList();
			needUpdateFileList_ = false;
		}

		// プリセットファイルリスト表示
		if (!presetFileList_.empty()) {
			ImGui::BeginChild("PresetList", ImVec2(0, 150), true);
			for (size_t i = 0; i < presetFileList_.size(); ++i) {
				bool isSelected = (selectedPresetIndex_ == static_cast<int>(i));
				if (ImGui::Selectable(GetFileNameWithoutExtension(presetFileList_[i]).c_str(), isSelected)) {
					selectedPresetIndex_ = static_cast<int>(i);
				}
			}
			ImGui::EndChild();

			// 選択中のファイルを表示
			if (selectedPresetIndex_ >= 0 && selectedPresetIndex_ < static_cast<int>(presetFileList_.size())) {
				ImGui::Text("選択: %s", presetFileList_[selectedPresetIndex_].c_str());

				if (ImGui::Button("プリセットを読み込む", ImVec2(200, 0))) {
					std::string fullPath = std::string(directoryPathBuffer_) + presetFileList_[selectedPresetIndex_];
					if (LoadPreset(particleSystem, fullPath)) {
						ImGui::OpenPopup("読み込み成功");
					} else {
						ImGui::OpenPopup("読み込み失敗");
					}
				}
			}
		} else {
			ImGui::Text("プリセットファイルが見つかりません。");
		}

		// 読み込み成功ポップアップ
		if (ImGui::BeginPopupModal("読み込み成功", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("プリセットを読み込みました。");
			if (ImGui::Button("OK", ImVec2(120, 0))) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// 読み込み失敗ポップアップ
		if (ImGui::BeginPopupModal("読み込み失敗", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("プリセットの読み込みに失敗しました。");
			if (ImGui::Button("OK", ImVec2(120, 0))) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}
#else
	(void)particleSystem; // 未使用警告を抑制
#endif
}

void ParticlePresetManager::UpdatePresetFileList()
{
	presetFileList_ = GetPresetList(directoryPathBuffer_);
	selectedPresetIndex_ = -1;
}

std::string ParticlePresetManager::GetFileNameWithoutExtension(const std::string& filename)
{
	size_t lastDot = filename.find_last_of('.');
	if (lastDot != std::string::npos) {
		return filename.substr(0, lastDot);
	}
	return filename;
}

bool ParticlePresetManager::SaveCurrentPreset(ParticleSystem* particleSystem)
{
	if (currentPresetPath_.empty()) {
		return false;
	}

	// 現在読み込まれているプリセットファイルに上書き保存
	return SavePreset(particleSystem, currentPresetPath_);
}
}
