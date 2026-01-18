#pragma once

#include "externals/nlohmann/single_include/nlohmann/json.hpp"
#include "MathCore.h"
#include "QuaternionTransform.h"
#include <string>
#include <filesystem>

using json = nlohmann::json;

/// @brief 汎用JSONマネージャークラス

namespace CoreEngine
{
class JsonManager {
public:
    /// @brief シングルトンインスタンスを取得
    /// @return JsonManagerのインスタンス
    static JsonManager& GetInstance();

    /// @brief JSONファイルを読み込み
    /// @param filePath ファイルパス
    /// @return 読み込んだJSONオブジェクト
    json LoadJson(const std::string& filePath);

    /// @brief JSONファイルに保存
    /// @param filePath ファイルパス
    /// @param jsonData 保存するJSONオブジェクト
    /// @return 保存に成功した場合true
    bool SaveJson(const std::string& filePath, const json& jsonData);

    /// @brief ディレクトリを作成（存在しない場合）
    /// @param dirPath ディレクトリパス
    /// @return 作成に成功した場合true
    bool CreateDirectory(const std::string& dirPath);

    /// @brief ファイルが存在するかチェック
    /// @param filePath ファイルパス
    /// @return ファイルが存在する場合true
    bool FileExists(const std::string& filePath);

    // ──────────────────────────────────────────────────────────
    // Vector3, Vector4, Transform等の変換ヘルパー関数
    // ──────────────────────────────────────────────────────────

    /// @brief Vector3をJSONに変換
    /// @param vec Vector3オブジェクト
    /// @return JSON配列
    static json Vector3ToJson(const Vector3& vec);

    /// @brief JSONからVector3に変換
    /// @param j JSONオブジェクト
    /// @return Vector3オブジェクト
    static Vector3 JsonToVector3(const json& j);

    /// @brief Vector4をJSONに変換
    /// @param vec Vector4オブジェクト
    /// @return JSON配列
    static json Vector4ToJson(const Vector4& vec);

    /// @brief JSONからVector4に変換
    /// @param j JSONオブジェクト
    /// @return Vector4オブジェクト
    static Vector4 JsonToVector4(const json& j);

    /// @brief QuaternionをJSONに変換
    /// @param q Quaternionオブジェクト
    /// @return JSON配列
    static json QuaternionToJson(const Quaternion& q);

    /// @brief JSONからQuaternionに変換
    /// @param j JSONオブジェクト
    /// @return Quaternionオブジェクト
    static Quaternion JsonToQuaternion(const json& j);

    /// @brief EulerTransformをJSONに変換
    /// @param transform EulerTransformオブジェクト
    /// @return JSONオブジェクト
    static json TransformToJson(const EulerTransform& transform);

    /// @brief JSONからEulerTransformに変換
    /// @param j JSONオブジェクト
    /// @return EulerTransformオブジェクト
    static EulerTransform JsonToTransform(const json& j);

    /// @brief QuaternionTransformをJSONに変換
    /// @param transform QuaternionTransformオブジェクト
    /// @return JSONオブジェクト
    static json QuaternionTransformToJson(const QuaternionTransform& transform);

    /// @brief JSONからQuaternionTransformに変換
    /// @param j JSONオブジェクト
    /// @return QuaternionTransformオブジェクト
    static QuaternionTransform JsonToQuaternionTransform(const json& j);

    /// @brief JSONの値を安全に取得（デフォルト値付き）
    /// @tparam T 取得する値の型
    /// @param j JSONオブジェクト
    /// @param key キー名
    /// @param defaultValue デフォルト値
    /// @return 取得した値またはデフォルト値
    template<typename T>
    static T SafeGet(const json& j, const std::string& key, const T& defaultValue) {
        if (j.contains(key) && !j[key].is_null()) {
            try {
                return j[key].get<T>();
            }
            catch (...) {
                return defaultValue;
            }
        }
        return defaultValue;
    }

    /// @brief Vector3を安全に取得
    /// @param j JSONオブジェクト
    /// @param key キー名
    /// @param defaultValue デフォルト値
    /// @return 取得したVector3またはデフォルト値
    static Vector3 SafeGetVector3(const json& j, const std::string& key, const Vector3& defaultValue = {0.0f, 0.0f, 0.0f});

    /// @brief Vector4を安全に取得
    /// @param j JSONオブジェクト
    /// @param key キー名
    /// @param defaultValue デフォルト値
    /// @return 取得したVector4またはデフォルト値
    static Vector4 SafeGetVector4(const json& j, const std::string& key, const Vector4& defaultValue = {0.0f, 0.0f, 0.0f, 0.0f});

    /// @brief Quaternionを安全に取得
    /// @param j JSONオブジェクト
    /// @param key キー名
    /// @param defaultValue デフォルト値
    /// @return 取得したQuaternionまたはデフォルト値
    static Quaternion SafeGetQuaternion(const json& j, const std::string& key, const Quaternion& defaultValue = {0.0f, 0.0f, 0.0f, 1.0f});

private:
    JsonManager() = default;
    ~JsonManager() = default;
    JsonManager(const JsonManager&) = delete;
    JsonManager& operator=(const JsonManager&) = delete;
};
}
