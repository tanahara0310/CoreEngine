#include "JsonManager.h"
#include <fstream>
#include <iostream>


namespace CoreEngine
{
    JsonManager& JsonManager::GetInstance() {
        static JsonManager instance;
        return instance;
    }

    json JsonManager::LoadJson(const std::string& filePath) {
        try {
            std::ifstream file(filePath);
            if (!file.is_open()) {
                std::cerr << "Failed to open file: " << filePath << std::endl;
                return json{};
            }

            json j;
            file >> j;
            return j;
        }
        catch (const std::exception& e) {
            std::cerr << "Error loading JSON from " << filePath << ": " << e.what() << std::endl;
            return json{};
        }
    }

    bool JsonManager::SaveJson(const std::string& filePath, const json& jsonData) {
        try {
            // ディレクトリの作成
            std::filesystem::path path(filePath);
            if (path.has_parent_path()) {
                CreateJsonDirectory(path.parent_path().string());
            }

            std::ofstream file(filePath);
            if (!file.is_open()) {
                std::cerr << "Failed to create file: " << filePath << std::endl;
                return false;
            }

            file << jsonData.dump(4); // インデント4でフォーマット
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "Error saving JSON to " << filePath << ": " << e.what() << std::endl;
            return false;
        }
    }

    bool JsonManager::CreateJsonDirectory(const std::string& dirPath) {
        try {
            return std::filesystem::create_directories(dirPath);
        }
        catch (const std::exception& e) {
            std::cerr << "Error creating directory " << dirPath << ": " << e.what() << std::endl;
            return false;
        }
    }

    bool JsonManager::FileExists(const std::string& filePath) {
        return std::filesystem::exists(filePath);
    }

    json JsonManager::Vector3ToJson(const Vector3& vec) {
        return json::array({ vec.x, vec.y, vec.z });
    }

    Vector3 JsonManager::JsonToVector3(const json& j) {
        if (j.is_array() && j.size() >= 3) {
            return Vector3{
                j[0].get<float>(),
                j[1].get<float>(),
                j[2].get<float>()
            };
        }
        return Vector3{ 0.0f, 0.0f, 0.0f };
    }

    json JsonManager::Vector4ToJson(const Vector4& vec) {
        return json::array({ vec.x, vec.y, vec.z, vec.w });
    }

    Vector4 JsonManager::JsonToVector4(const json& j) {
        if (j.is_array() && j.size() >= 4) {
            return Vector4{
                j[0].get<float>(),
                j[1].get<float>(),
                j[2].get<float>(),
                j[3].get<float>()
            };
        }
        return Vector4{ 0.0f, 0.0f, 0.0f, 0.0f };
    }

    json JsonManager::QuaternionToJson(const Quaternion& q) {
        return json::array({ q.x, q.y, q.z, q.w });
    }

    Quaternion JsonManager::JsonToQuaternion(const json& j) {
        if (j.is_array() && j.size() >= 4) {
            return Quaternion{
                j[0].get<float>(),
                j[1].get<float>(),
                j[2].get<float>(),
                j[3].get<float>()
            };
        }
        return Quaternion{ 0.0f, 0.0f, 0.0f, 1.0f };
    }

    json JsonManager::TransformToJson(const EulerTransform& transform) {
        json j;
        j["scale"] = Vector3ToJson(transform.scale);
        j["rotate"] = Vector3ToJson(transform.rotate);
        j["translate"] = Vector3ToJson(transform.translate);
        return j;
    }

    EulerTransform JsonManager::JsonToTransform(const json& j) {
        EulerTransform transform;
        if (j.contains("scale")) {
            transform.scale = JsonToVector3(j["scale"]);
        }
        if (j.contains("rotate")) {
            transform.rotate = JsonToVector3(j["rotate"]);
        }
        if (j.contains("translate")) {
            transform.translate = JsonToVector3(j["translate"]);
        }
        return transform;
    }

    json JsonManager::QuaternionTransformToJson(const QuaternionTransform& transform) {
        json j;
        j["scale"] = Vector3ToJson(transform.scale);
        j["rotate"] = QuaternionToJson(transform.rotate);
        j["translate"] = Vector3ToJson(transform.translate);
        return j;
    }

    QuaternionTransform JsonManager::JsonToQuaternionTransform(const json& j) {
        QuaternionTransform transform;
        if (j.contains("scale")) {
            transform.scale = JsonToVector3(j["scale"]);
        }
        if (j.contains("rotate")) {
            transform.rotate = JsonToQuaternion(j["rotate"]);
        }
        if (j.contains("translate")) {
            transform.translate = JsonToVector3(j["translate"]);
        }
        return transform;
    }

    Vector3 JsonManager::SafeGetVector3(const json& j, const std::string& key, const Vector3& defaultValue) {
        if (j.contains(key) && !j[key].is_null()) {
            try {
                return JsonToVector3(j[key]);
            }
            catch (...) {
                return defaultValue;
            }
        }
        return defaultValue;
    }

    Vector4 JsonManager::SafeGetVector4(const json& j, const std::string& key, const Vector4& defaultValue) {
        if (j.contains(key) && !j[key].is_null()) {
            try {
                return JsonToVector4(j[key]);
            }
            catch (...) {
                return defaultValue;
            }
        }
        return defaultValue;
    }

    Quaternion JsonManager::SafeGetQuaternion(const json& j, const std::string& key, const Quaternion& defaultValue) {
        if (j.contains(key) && !j[key].is_null()) {
            try {
                return JsonToQuaternion(j[key]);
            }
            catch (...) {
                return defaultValue;
            }
        }
        return defaultValue;
    }
}
