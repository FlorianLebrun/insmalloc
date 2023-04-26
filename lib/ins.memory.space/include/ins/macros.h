#pragma once

#define _INS_PROTECTION 1

#if _DEBUG || _INS_PROTECTION
#define _INS_ASSERT(x) {if(!(x)) __debugbreak();}
#define _INS_PROTECT_CONDITION(x) _INS_ASSERT(x)
#define _INS_PROFILE __declspec(noinline)
#else
#define _INS_ASSERT(x)
#define _INS_PROTECT_CONDITION(x) if (!x) throw;
#define _INS_PROFILE
#endif

#if 0
#define _INS_TRACE(x) x
#else
#define _INS_TRACE(x)
#endif
