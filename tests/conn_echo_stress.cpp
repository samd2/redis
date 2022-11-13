/* Copyright (c) 2018-2022 Marcelo Zimbres Silva (mzimbres@gmail.com)
 *
 * Distributed under the Boost Software License, Version 1.0. (See
 * accompanying file LICENSE.txt)
 */

#include <iostream>
#include <boost/asio.hpp>
#ifdef BOOST_ASIO_HAS_CO_AWAIT
#include <boost/system/errc.hpp>

#define BOOST_TEST_MODULE low level
#include <boost/test/included/unit_test.hpp>

#include <aedis.hpp>
#include <aedis/src.hpp>

#include "common.hpp"

namespace net = boost::asio;
using error_code = boost::system::error_code;
using tcp_socket = net::use_awaitable_t<>::as_default_on_t<net::ip::tcp::socket>;

using aedis::resp3::request;
using aedis::operation;
using aedis::adapt;
using connection = aedis::connection<tcp_socket>;

#include <boost/asio/experimental/awaitable_operators.hpp>
using namespace net::experimental::awaitable_operators;

net::awaitable<void> push_consumer(std::shared_ptr<connection> conn, int expected)
{
   int c = 0;
   for (;;) {
      co_await conn->async_receive(adapt(), net::use_awaitable);
      if (++c == expected)
         break;
   }

   request req;
   req.push("HELLO", 3);
   req.push("QUIT");
   co_await conn->async_exec(req, adapt());
}

auto echo_session(std::shared_ptr<connection> conn, std::string id, int n) -> net::awaitable<void>
{
   auto ex = co_await net::this_coro::executor;

   request req;
   std::tuple<aedis::ignore, std::string> resp;

   for (auto i = 0; i < n; ++i) {
      auto const msg = id + "/" + std::to_string(i);
      //std::cout << msg << std::endl;
      req.push("HELLO", 3);
      req.push("PING", msg);
      req.push("SUBSCRIBE", "channel");
      boost::system::error_code ec;
      co_await conn->async_exec(req, adapt(resp), net::redirect_error(net::use_awaitable, ec));
      BOOST_TEST(!ec);
      BOOST_CHECK_EQUAL(msg, std::get<1>(resp));
      req.clear();
      std::get<1>(resp).clear();
   }
}

auto async_echo_stress() -> net::awaitable<void>
{
   auto ex = co_await net::this_coro::executor;
   auto conn = std::make_shared<connection>(ex);

   int const sessions = 1000;
   int const msgs = 100;
   int total = sessions * msgs;

   net::co_spawn(ex, push_consumer(conn, total), net::detached);

   for (int i = 0; i < sessions; ++i) 
      net::co_spawn(ex, echo_session(conn, std::to_string(i), msgs), net::detached);

   auto const addrs = resolve();
   co_await net::async_connect(conn->next_layer(), addrs);
   co_await conn->async_run();
}

BOOST_AUTO_TEST_CASE(echo_stress)
{
   net::io_context ioc;
   net::co_spawn(ioc, async_echo_stress(), net::detached);
   ioc.run();
}

#else
int main(){}
#endif
