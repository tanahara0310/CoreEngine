#include "pch.h"
#include "CameraRigLibrary.h"

#include "Camera/Rig/CameraRigIO.h"

#include <filesystem>

namespace CoreEngine
{
    CameraRigLibrary::CameraRigLibrary()
        : NamedAssetLibrary<CameraRigAsset>(
            CameraRigPaths::kDirectory,
            "カメラのリグ",
            [](const std::string& path, CameraRigAsset& out) {
                if (!CameraRigIO::Load(path, out)) {
                    return false;
                }
                // ファイル側の名前が空でも、一覧やログで区別できるようファイル名で埋める
                if (out.name.empty()) {
                    out.name = std::filesystem::path(path).stem().string();
                }
                return true;
            },
            [](const std::string& directory) { return CameraRigIO::GetRigFileList(directory); })
    {
    }
}
