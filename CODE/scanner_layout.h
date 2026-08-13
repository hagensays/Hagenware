#pragma once

namespace ScannerLayout {

inline constexpr int kWindowWidth = 640;
inline constexpr int kWindowHeight = 420;
inline constexpr int kOuterMargin = 24;
inline constexpr int kTitleTop = 14;
inline constexpr int kTitleHeight = 28;
inline constexpr int kFirstRowTop = 52;
inline constexpr int kRowStep = 40;
inline constexpr int kControlHeight = 28;
inline constexpr int kRefreshSize = 28;
inline constexpr int kControlGap = 8;

inline constexpr int RowTop(int row_index) {
    return kFirstRowTop + (row_index * kRowStep);
}

} // namespace ScannerLayout
