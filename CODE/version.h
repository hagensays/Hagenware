#pragma once

namespace Version {

inline constexpr unsigned int kMajor = 0;
inline constexpr unsigned int kMinor = 3;
inline constexpr unsigned int kPatch = 9;
inline constexpr unsigned int kPacked = (kMajor * 1000000u) + (kMinor * 1000u) + kPatch;
inline constexpr wchar_t kNumber[] = L"v0.3.9";

} // namespace Version
