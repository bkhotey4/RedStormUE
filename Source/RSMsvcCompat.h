// MSVC 14.40+ 與 UE 5.4 標頭相容修正：MSVC 沒有 __has_feature，舊引擎標頭直接在 #if 中呼叫它
#pragma once
#if defined(_MSC_VER) && !defined(__clang__) && !defined(__has_feature)
#define __has_feature(x) 0
#endif
