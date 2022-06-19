/* Copyright (c) 2018-2022 Marcelo Zimbres Silva (mzimbres@gmail.com)
 *
 * Distributed under the Boost Software License, Version 1.0. (See
 * accompanying file LICENSE.txt)
 */

#include <map>
#include <set>
#include <vector>
#include <iostream>
#include <boost/asio.hpp>
#include <aedis/aedis.hpp>
#include <aedis/src.hpp>
#include "print.hpp"

namespace net = boost::asio;
namespace adapter = aedis::adapter;
using aedis::command;
using aedis::resp3::request;
using connection = aedis::connection<>;
using node_type = aedis::resp3::node<boost::string_view>;
using error_code = boost::system::error_code;

auto handler =[](auto ec, auto...)
   { std::cout << ec.message() << std::endl; };

int main()
{
   request req;
   req.push(command::ping);
   req.push(command::incr, "some-key");
   req.push(command::quit);

   std::string r0;
   int r1;

   auto adapter =
      [ a0 = adapter::adapt(r0)
      , a1 = adapter::adapt(r1)
      ](std::size_t, command cmd, node_type const& nd, error_code& ec) mutable
   {
      switch (cmd) {
         case command::ping: a0(nd, ec); break;
         case command::incr: a1(nd, ec); break;
         default:;
      }
   };

   net::io_context ioc;
   connection db{ioc};
   db.async_exec("127.0.0.1", "6379", req, adapter, handler);
   ioc.run();

   std::cout
      << "ping: " << r0 << "\n"
      << "incr: " << r1 << "\n";
}