#ifndef ALKAIDLAB_FW_WINDOWS_COMPAT_HPP
#define ALKAIDLAB_FW_WINDOWS_COMPAT_HPP

// ============================================================================
// Windows 头文件宏污染清理
// ============================================================================
// 在包含会拉入 <windows.h> 的头文件 (libhv / spdlog 等) 之后, 在使用 fw 枚举
// 类型 (LogLevel::ERROR, Method::DELETE) 之前包含此文件, 移除冲突宏.
//
// 用法:
//   #include <hv/HttpServer.h>     // 会污染 ERROR / DELETE / IN / OUT 等
//   #include <fw/Logger.hpp>
//   #include <fw/Router.hpp>
//   #include <fw/WindowsCompat.hpp>  // 必须在使用枚举值之前
//   ...
//   LogLevel::ERROR;   // OK
//   Method::DELETE;    // OK
//
// 注: 这个头本身不包含任何东西, 只做 #undef, 安全在任意位置重复包含.
// ============================================================================

#ifdef _WIN32

#ifdef ERROR
#  undef ERROR
#endif

#ifdef DELETE
#  undef DELETE
#endif

#ifdef IN
#  undef IN
#endif

#ifdef OUT
#  undef OUT
#endif

#ifdef OPTIONAL
#  undef OPTIONAL
#endif

#ifdef VOID
#  undef VOID
#endif

#ifdef min
#  undef min
#endif

#ifdef max
#  undef max
#endif

#endif  // _WIN32

#endif  // ALKAIDLAB_FW_WINDOWS_COMPAT_HPP
