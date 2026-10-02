#pragma once

#ifdef __cplusplus

#include <cstdint>

namespace liba32android::compat {

// Android/Linux guest errno numbers shared by compatibility services.
inline constexpr std::int32_t kA32AndroidEsrch = 3;
inline constexpr std::int32_t kA32AndroidEagain = 11;
inline constexpr std::int32_t kA32AndroidEnomem = 12;
inline constexpr std::int32_t kA32AndroidEbusy = 16;
inline constexpr std::int32_t kA32AndroidEinval = 22;
inline constexpr std::int32_t kA32AndroidErange = 34;
inline constexpr std::int32_t kA32AndroidEdeadlk = 35;

}  // namespace liba32android::compat

#endif
