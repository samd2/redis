/* Copyright (c) 2019 Marcelo Zimbres Silva (mzimbres@gmail.com)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <aedis/resp3/type.hpp>

#include <cassert>

namespace aedis {
namespace resp3 {

char const* to_string(type t)
{
   static char const* table[] =
   { "array"
   , "push"
   , "set"
   , "map"
   , "attribute"
   , "simple_string"
   , "simple_error"
   , "number"
   , "doublean"
   , "boolean"
   , "big_number"
   , "null"
   , "blob_error"
   , "verbatim_string"
   , "blob_string"
   , "streamed_string_part"
   , "invalid"
   };

   return table[static_cast<int>(t)];
}

std::ostream& operator<<(std::ostream& os, type t)
{
   os << to_string(t);
   return os;
}

bool is_aggregate(type t)
{
   switch (t) {
      case type::array:
      case type::push:
      case type::set:
      case type::map:
      case type::attribute: return true;
      default: return false;
   }
}

std::size_t element_multiplicity(type t)
{
   switch (t) {
      case type::map:
      case type::attribute: return 2ULL;
      default: return 1ULL;
   }
}

} // resp3
} // aedis