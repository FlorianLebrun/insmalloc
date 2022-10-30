#pragma once

namespace ins {

   template<typename T>
   inline T align(T offset, int alignment) { return ((-offset) & (alignment - 1)) + offset; }

}