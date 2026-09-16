#include "pch.h"
#include "CameraSequenceLibrary.h"

#include "Camera/Sequence/CameraSequenceIO.h"

namespace CoreEngine
{
    CameraSequenceLibrary::CameraSequenceLibrary()
        : NamedAssetLibrary<CameraSequenceAsset>(
            CameraSequencePaths::kDirectory,
            "カメラのシーケンス",
            [](const std::string& path, CameraSequenceAsset& out) { return CameraSequenceIO::Load(path, out); },
            [](const std::string& directory) { return CameraSequenceIO::GetSequenceFileList(directory); })
    {
    }
}
