/* Copyright (c) 2018-2022 Marcelo Zimbres Silva (mzimbres@gmail.com)
 *
 * Distributed under the Boost Software License, Version 1.0. (See
 * accompanying file LICENSE.txt)
 */

#include <map>
#include <vector>
#include <iostream>
#include <aedis/aedis.hpp>
#include <aedis/src.hpp>
#include "print.hpp"

namespace net = boost::asio;
using boost::optional;
using aedis::adapt;
using aedis::command;
using aedis::resp3::request;
using connection = aedis::connection<>;

int main()
{
   std::vector<int> vec
      {1, 2, 3, 4, 5, 6};

   std::map<std::string, int> map
      {{"key1", 10}, {"key2", 20}, {"key3", 30}};

   request req;
   req.push_range(command::rpush, "rpush-key", vec);
   req.push_range(command::hset, "hset-key", map);
   req.push(command::multi);
   req.push(command::lrange, "rpush-key", 0, -1);
   req.push(command::hgetall, "hset-key");
   req.push(command::exec);
   req.push(command::quit);

   std::tuple<
      std::string, // rpush
      std::string, // hset
      std::string, // multi
      std::string, // lrange
      std::string, // hgetall
      std::tuple<optional<std::vector<int>>, optional<std::map<std::string, int>>>, // exec
      std::string // quit
   > resp;

   net::io_context ioc;
   connection db{ioc};
   db.async_exec("127.0.0.1", "6379", req, aedis::adapt(resp),
      [](auto ec, auto) { std::cout << ec.message() << std::endl; });
   ioc.run();

   print(std::get<0>(std::get<5>(resp)).value());
   print(std::get<1>(std::get<5>(resp)).value());
}
