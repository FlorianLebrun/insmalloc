#pragma once

#define _INS_PROTECTION 1

#if _DEBUG
#define _USE_INS_DEBUG 0
#else
#define _USE_INS_DEBUG 0
#endif

#if _USE_INS_DEBUG
#define _INS_DEBUG(x) x
#define _INS_ASSERT(x) _ASSERT(x)
#define _INS_INLINE _ASSERT(x)
#define _INS_PROFILE __declspec(noinline)
#else
#define _INS_DEBUG(x)
#define _INS_ASSERT(x)
#define _INS_PROFILE
#endif

#if _INS_PROTECTION
#define _INS_PROTECT_CONDITION(x) _ASSERT(x)
#else
#define _INS_PROTECT_CONDITION(x) if (!x) throw;
#endif
