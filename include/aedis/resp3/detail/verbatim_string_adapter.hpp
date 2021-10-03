/* Copyright (c) 2019 - 2021 Marcelo Zimbres Silva (mzimbres at gmail dot com)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <aedis/resp3/type.hpp>
#include <aedis/resp3/response_adapter_base.hpp>
#include <aedis/resp3/detail/adapter_utils.hpp>

namespace aedis { namespace resp3 { namespace detail {

struct verbatim_string_adapter : public response_adapter_base {
   verbatim_string_type* result = nullptr;

   verbatim_string_adapter(verbatim_string_type* p) : result(p) {}

   void on_verbatim_string(std::string_view s) override
      { from_string_view(s, *result); }
};

} // detail
} // resp3
} // aedis