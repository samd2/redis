/* Copyright (c) 2018-2022 Marcelo Zimbres Silva (mzimbres@gmail.com)
 *
 * Distributed under the Boost Software License, Version 1.0. (See
 * accompanying file LICENSE.txt)
 */

#include <boost/asio.hpp>
#if defined(BOOST_ASIO_HAS_CO_AWAIT)
#include <boost/redis.hpp>
#include "common/common.hpp"

namespace net = boost::asio;
using boost::redis::operation;
using boost::redis::request;
using boost::redis::response;

auto run(std::shared_ptr<connection> conn, std::string host, std::string port) -> net::awaitable<void>
{
   co_await connect(conn, host, port);
   co_await conn->async_run();
}

auto hello(std::shared_ptr<connection> conn) -> net::awaitable<void>
{
   request req;
   req.push("HELLO", 3);

   co_await conn->async_exec(req);
}

auto ping(std::shared_ptr<connection> conn) -> net::awaitable<void>
{
   request req;
   req.push("PING", "Hello world");

   response<std::string> resp;
   co_await conn->async_exec(req, resp);

   std::cout << "PING: " << std::get<0>(resp).value() << std::endl;
}

auto quit(std::shared_ptr<connection> conn) -> net::awaitable<void>
{
   request req;
   req.push("QUIT");

   co_await conn->async_exec(req);
}

// Called from the main function (see main.cpp)
auto co_main(std::string host, std::string port) -> net::awaitable<void>
{
   auto ex = co_await net::this_coro::executor;
   auto conn = std::make_shared<connection>(ex);
   net::co_spawn(ex, run(conn, host, port), net::detached);
   co_await hello(conn);
   co_await ping(conn);
   co_await quit(conn);
}

#endif // defined(BOOST_ASIO_HAS_CO_AWAIT)