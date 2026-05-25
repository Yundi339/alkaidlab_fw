/**
 * Windows POSIX 兼容垫片 — 由 CMake 在 Windows 构建时通过 -include 强制注入。
 * 仅在 _WIN32 下生效；非 Windows 平台被强制包含也是 no-op。
 *
 * 当前覆盖：
 *   - localtime_r / gmtime_r：MinGW64 默认不暴露这两个 POSIX 函数。
 *     Windows CRT 的 localtime_s / gmtime_s 参数顺序与 POSIX 相反，这里做包装。
 */
#ifndef ALKAIDLAB_FW_WIN_POSIX_SHIM_HPP
#define ALKAIDLAB_FW_WIN_POSIX_SHIM_HPP

#ifdef _WIN32
#include <time.h>

static inline struct tm* localtime_r(const time_t* t, struct tm* out) {
    return ::localtime_s(out, t) == 0 ? out : nullptr;
}
static inline struct tm* gmtime_r(const time_t* t, struct tm* out) {
    return ::gmtime_s(out, t) == 0 ? out : nullptr;
}
#endif // _WIN32

#endif // ALKAIDLAB_FW_WIN_POSIX_SHIM_HPP
