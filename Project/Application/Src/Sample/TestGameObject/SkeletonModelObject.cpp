#include "SkeletonModelObject.h"
void SkeletonModelObject::OnInitialize() {
    transform_.translate = { -3.0f, 0.0f, 0.0f };  // 左側に配置
    transform_.scale = { 1.0f, 1.0f, 1.0f };
    transform_.rotate = { 0.0f, 0.0f, 0.0f };
}

