#include "pch.h"
#include "Text/MsdfAtlasAllocator.h"

#include <algorithm>

namespace CoreEngine
{
    void MsdfAtlasAllocator::Initialize(int width, int height, int pageCount, int padding)
    {
        width_ = width;
        height_ = height;
        pageCount_ = (std::max)(pageCount, 1);
        padding_ = (std::max)(padding, 1);

        pageIndex_ = 0;
        cursorX_ = padding_;
        cursorY_ = padding_;
        shelfHeight_ = 0;
    }

    void MsdfAtlasAllocator::Restore(const State& state)
    {
        pageIndex_ = std::clamp(state.pageIndex, 0, pageCount_ - 1);
        cursorX_ = state.cursorX;
        cursorY_ = state.cursorY;
        shelfHeight_ = state.shelfHeight;
    }

    bool MsdfAtlasAllocator::Allocate(int width, int height, int& outPage, int& outX, int& outY)
    {
        if (width <= 0 || height <= 0) { return false; }
        // どの枚にも入らない大きさ（アトラスより広いグリフ）は最初に弾く
        if (width + padding_ * 2 > width_ || height + padding_ * 2 > height_) {
            return false;
        }

        for (;;) {
            // いまの段に入らなければ次の段へ移る
            if (cursorX_ + width + padding_ > width_) {
                cursorX_ = padding_;
                cursorY_ += shelfHeight_ + padding_;
                shelfHeight_ = 0;
            }

            if (cursorY_ + height + padding_ <= height_) {
                outPage = pageIndex_;
                outX = cursorX_;
                outY = cursorY_;

                cursorX_ += width + padding_;
                shelfHeight_ = (std::max)(shelfHeight_, height);
                return true;
            }

            // この枚は使い切った。次の枚の先頭から続ける
            if (pageIndex_ + 1 >= pageCount_) {
                return false; // 全部使い切った
            }
            ++pageIndex_;
            cursorX_ = padding_;
            cursorY_ = padding_;
            shelfHeight_ = 0;
        }
    }

    float MsdfAtlasAllocator::GetOccupancy() const
    {
        if (height_ <= 0 || pageCount_ <= 0) { return 0.0f; }

        // 埋まりきった枚 + いまの枚の進み具合
        const float filledPages = static_cast<float>(pageIndex_);
        const float currentPageUsed =
            static_cast<float>(cursorY_ + shelfHeight_) / static_cast<float>(height_);

        return (std::min)((filledPages + currentPageUsed) / static_cast<float>(pageCount_), 1.0f);
    }
}
