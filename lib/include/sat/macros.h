#pragma once

#if _DEBUG
#define USE_SAT_DEBUG 1
#else
#define USE_SAT_DEBUG 0
#endif

#if USE_SAT_DEBUG
#define SAT_DEBUG(x) x
#define SAT_ASSERT(x) _ASSERT(x)
#define SAT_INLINE _ASSERT(x)
#define SAT_PROFILE __declspec(noinline)
#else
#define SAT_DEBUG(x)
#define SAT_ASSERT(x)
#define SAT_PROFILE
#endif
