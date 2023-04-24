#pragma once

namespace ins::bit {

   template<typename T>
   inline T align(T offset, int alignment) { return ((-offset) & (alignment - 1)) + offset; }

}