#include "pch.h"
#include "JsonManager.h"
#include "Utility/Path/ProjectPaths.h"
#include <charconv>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <system_error>


namespace CoreEngine
{
    namespace
    {
        /// @brief float で表せる数を、float として最も短い 10 進の値へ置き換える（下の階層もたどる）
        void ShortenFloats(json& value)
        {
            if (value.is_structured()) {
                for (json& child : value) {
                    ShortenFloats(child);
                }
                return;
            }
            if (!value.is_number_float()) {
                return;
            }

            const double original = value.get<double>();
            const float narrowed = static_cast<float>(original);
            if (!std::isfinite(narrowed) || static_cast<double>(narrowed) != original) {
                return;
            }

            char text[32];
            const std::to_chars_result written = std::to_chars(text, text + sizeof(text), narrowed);
            if (written.ec != std::errc{}) {
                return;
            }
            double shortest = original;
            if (std::from_chars(text, written.ptr, shortest).ec == std::errc{}) {
                value = shortest;
            }
        }
    }

    JsonManager& JsonManager::GetInstance() {
        static JsonManager instance;
        return instance;
    }

    // 読み込み失敗（ファイル無し・パースエラー）は空の json を返す。
    // 呼び出し側は empty() で判定し、既定値へフォールバックする契約
    json JsonManager::LoadJson(const std::string& filePath) {
        std::string bytes;
        if (!ReadBytes(ProjectPaths::Resolve(filePath), bytes)) {
            std::cerr << "Failed to open file: " << filePath << std::endl;
            return json{};
        }
        json j = Parse(bytes);
        if (j.is_discarded()) {
            std::cerr << "Error loading JSON from " << filePath << std::endl;
            return json{};
        }
        return j;
    }

    bool JsonManager::ReadBytes(const std::filesystem::path& path, std::string& out) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return false;
        }
        out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return !file.bad();
    }

    bool JsonManager::IsBinary(std::string_view bytes) {
        if (bytes.empty()) {
            return false;
        }
        const auto byteAt = [&bytes](size_t i) { return static_cast<unsigned char>(bytes[i]); };
        const bool bom = bytes.size() >= 3 && byteAt(0) == 0xEF && byteAt(1) == 0xBB && byteAt(2) == 0xBF;
        return byteAt(0) >= 0x80 && !bom;
    }

    json JsonManager::Parse(std::string_view bytes) {
        if (IsBinary(bytes)) {
            return json::from_msgpack(bytes.begin(), bytes.end(), /*strict=*/true, /*allow_exceptions=*/false);
        }
        return json::parse(bytes.begin(), bytes.end(), nullptr, /*allow_exceptions=*/false);
    }

    json JsonManager::ParseFile(const std::filesystem::path& path) {
        std::string bytes;
        if (!ReadBytes(path, bytes)) {
            return json(json::value_t::discarded);
        }
        return Parse(bytes);
    }

    std::vector<std::uint8_t> JsonManager::ToBinary(const json& jsonData) {
        return json::to_msgpack(jsonData);
    }

    // 保存先の親ディレクトリが無ければ作ってから書く
    bool JsonManager::SaveJson(const std::string& filePath, const json& jsonData) {
        try {
            // ディレクトリの作成
            const std::filesystem::path path = ProjectPaths::Resolve(filePath);
            if (path.has_parent_path()) {
                std::filesystem::create_directories(path.parent_path());
            }

            std::ofstream file(path);
            if (!file.is_open()) {
                std::cerr << "Failed to create file: " << filePath << std::endl;
                return false;
            }

            file << ToFileText(jsonData);
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "Error saving JSON to " << filePath << ": " << e.what() << std::endl;
            return false;
        }
    }

    std::string JsonManager::ToFileText(const json& jsonData) {
        json shortened = jsonData;
        ShortenFloats(shortened);
        return shortened.dump(4) + "\n";
    }

    bool JsonManager::CreateJsonDirectory(const std::string& dirPath) {
        try {
            return std::filesystem::create_directories(ProjectPaths::Resolve(dirPath));
        }
        catch (const std::exception& e) {
            std::cerr << "Error creating directory " << dirPath << ": " << e.what() << std::endl;
            return false;
        }
    }

    bool JsonManager::FileExists(const std::string& filePath) {
        return std::filesystem::exists(ProjectPaths::Resolve(filePath));
    }

    // ───────────────── 値型 ⇔ JSON の変換 ─────────────────
    // どれも「配列 [x, y, z, ...]」形式。要素数が足りない JSON は既定値で埋めて読む
    // （手書き・旧バージョンのシーンファイルでも落ちないようにするため）。

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
