#pragma once

#ifdef _DEBUG

#include "Camera/CameraStructs.h"
#include <string>
#include <vector>

namespace CoreEngine
{
    /// @brief シーケンス内ショットの遷移方式（0:カット / 1:ブレンド）
    enum class CameraSequenceTransitionType {
        Cut = 0,
        Blend = 1
    };

    /// @brief シーケンスに保存するショット情報
    struct CameraSequenceShot {
        std::string name;
        float startTime = 0.0f;
        float endTime = 1.0f;
        bool enabled = true;
        CameraSequenceTransitionType transitionType = CameraSequenceTransitionType::Cut;
        float blendDuration = 0.2f;
    };

    /// @brief シーケンスアセット1件分の保存データ
    struct CameraSequenceAsset {
        struct Keyframe {
            float time = 0.0f;
            CameraSnapshot snapshot{};
        };

        std::string version = "2.0";
        float timelineLength = 10.0f;
        int easingTypeIndex = 0;
        bool shotsEnabled = true;
        std::vector<Keyframe> keyframes;
        std::vector<CameraSequenceShot> shots;
    };

    /// @brief カメラシーケンスの保存/読み込みを担当するI/Oヘルパー
    class CameraSequenceAssetIO {
    public:
        /// @brief シーケンスディレクトリ内のjson一覧を取得
        static std::vector<std::string> GetSequenceFileList(const std::string& directoryPath);

        /// @brief シーケンスアセットをjson保存
        static bool Save(const std::string& filePath, const CameraSequenceAsset& asset);

        /// @brief jsonからシーケンスアセットを読み込み
        static bool Load(const std::string& filePath, CameraSequenceAsset& outAsset);
    };
}

#endif // _DEBUG
