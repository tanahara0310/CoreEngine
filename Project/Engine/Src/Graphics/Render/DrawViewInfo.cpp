#include "pch.h"
#include "DrawViewInfo.h"

#include "Camera/View/ViewInfo.h"

namespace CoreEngine
{
    const ICamera* DrawViewInfo::GetCamera() const
    {
        return view ? view->camera : nullptr;
    }
}
