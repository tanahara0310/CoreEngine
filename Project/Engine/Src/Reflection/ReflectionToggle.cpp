#include "pch.h"
#include "Reflection/ReflectionToggle.h"

#include "Utility/CVar/CVar.h"

namespace CoreEngine::Reflection
{
    namespace
    {
        CVar<bool> cvUseReflectionInspector{
            "d.Editor.UseReflectionInspector", false,
            "インスペクタと保存を型記述子から組み立てる（旧 DrawInspector / OnSerialize と切り替え）" };
    }

    bool IsEnabled()
    {
        return cvUseReflectionInspector.Get();
    }
}
