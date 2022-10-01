#pragma once

#if _DEBUG
#define USE_INS_DEBUG 1
#else
#define USE_INS_DEBUG 0
#endif

#if USE_INS_DEBUG
#define INS_DEBUG(x) x
#define INS_ASSERT(x) _ASSERT(x)
#define INS_INLINE _ASSERT(x)
#define INS_PROFILE __declspec(noinline)
#else
#define INS_DEBUG(x)
#define INS_ASSERT(x)
#define INS_PROFILE
#endif
