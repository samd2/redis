/* Copyright (c) 2019 - 2021 Marcelo Zimbres Silva (mzimbres at gmail dot com)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <queue>
#include <string>
#include <cstdio>
#include <utility>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <iostream>
#include <algorithm>
#include <functional>
#include <type_traits>
#include <string_view>
#include <charconv>

#include "type.hpp"
#include "parser.hpp"
#include "response.hpp"
#include "request.hpp"

namespace aedis { namespace resp {

inline
void print_command_raw(std::string const& data, int n)
{
  for (int i = 0; i < n; ++i) {
    if (data[i] == '\n') {
      std::cout << "\\n";
      continue;
    }
    if (data[i] == '\r') {
      std::cout << "\\r";
      continue;
    }
    std::cout << data[i];
  }
}

// The parser supports up to 5 levels of nested structures. The first
// element in the sizes stack is a sentinel and must be different from
// 1.
template <
  class AsyncReadStream,
  class Storage,
  class Response>
class parse_op {
private:
   AsyncReadStream& stream_;
   Storage* buf_ = nullptr;
   parser<Response> parser_;
   int start_ = 1;

public:
   parse_op(AsyncReadStream& stream, Storage* buf, Response* res)
   : stream_ {stream}
   , buf_ {buf}
   , parser_ {res}
   { }

   template <class Self>
   void operator()( Self& self
                  , boost::system::error_code ec = {}
                  , std::size_t n = 0)
   {
      switch (start_) {
         for (;;) {
            if (parser_.bulk() == bulk_type::none) {
               case 1:
               start_ = 0;
               net::async_read_until(
                  stream_,
                  net::dynamic_buffer(*buf_),
                  "\r\n",
                  std::move(self));

               return;
            }

	    // On a bulk read we can't read until delimiter since the
	    // payload may contain the delimiter itself so we have to
	    // read the whole chunk. However if the bulk blob is small
	    // enough it may be already on the buffer buf_ we read
	    // last time. If it is, there is no need of initiating
	    // another async op otherwise we have to read the
	    // missing bytes.
            if (std::ssize(*buf_) < (parser_.bulk_length() + 2)) {
               start_ = 0;
	       auto const s = std::ssize(*buf_);
	       auto const l = parser_.bulk_length();
	       auto const to_read = static_cast<std::size_t>(l + 2 - s);
               buf_->resize(l + 2);
               net::async_read(
                  stream_,
                  net::buffer(buf_->data() + s, to_read),
                  net::transfer_all(),
                  std::move(self));
               return;
            }

            default:
	    {
	       if (ec)
		  return self.complete(ec);

	       n = parser_.advance(buf_->data(), n);
	       buf_->erase(0, n);
	       if (parser_.done())
		  return self.complete({});
	    }
         }
      }
   }
};

template <
   class SyncReadStream,
   class Storage,
   class Response>
auto read(
   SyncReadStream& stream,
   Storage& buf,
   Response& res,
   boost::system::error_code& ec)
{
   parser<Response> p {&res};
   std::size_t n = 0;
   do {
      if (p.bulk() == bulk_type::none) {
	 n = net::read_until(stream, net::dynamic_buffer(buf), "\r\n", ec);
	 if (ec || n < 3)
	    return n;
      } else {
	 auto const s = std::ssize(buf);
	 auto const l = p.bulk_length();
	 if (s < (l + 2)) {
	    buf.resize(l + 2);
	    auto const to_read = static_cast<std::size_t>(l + 2 - s);
	    n = net::read(stream, net::buffer(buf.data() + s, to_read));
	    assert(n >= to_read);
	    if (ec)
	       return n;
	 }
      }

      n = p.advance(buf.data(), n);
      buf.erase(0, n);
   } while (!p.done());

   return n;
}

template<
   class SyncReadStream,
   class Storage,
   class Response>
std::size_t
read(
   SyncReadStream& stream,
   Storage& buf,
   Response& res)
{
   boost::system::error_code ec;
   auto const n = read(stream, buf, res, ec);

   if (ec)
       BOOST_THROW_EXCEPTION(boost::system::system_error{ec});

   return n;
}

template <
   class AsyncReadStream,
   class Storage,
   class Response,
   class CompletionToken =
      net::default_completion_token_t<typename AsyncReadStream::executor_type>
   >
auto async_read(
   AsyncReadStream& stream,
   Storage& buffer,
   Response& res,
   CompletionToken&& token =
      net::default_completion_token_t<typename AsyncReadStream::executor_type>{})
{
   return net::async_compose
      < CompletionToken
      , void(boost::system::error_code)
      >(parse_op<AsyncReadStream, Storage, Response> {stream, &buffer, &res},
        token,
        stream);
}

template <
  class AsyncReadStream,
  class Storage>
class type_op {
private:
   AsyncReadStream& stream_;
   Storage* buf_ = nullptr;
   type* t_;

public:
   type_op(AsyncReadStream& stream, Storage* buf, type* t)
   : stream_ {stream}
   , buf_ {buf}
   , t_ {t}
   { }

   template <class Self>
   void operator()( Self& self
                  , boost::system::error_code ec = {}
                  , std::size_t n = 0)
   {
      if (std::empty(*buf_)) {
	 net::async_read_until(
	    stream_,
	    net::dynamic_buffer(*buf_),
	    "\r\n",
	    std::move(self));
      } else {
	 *t_ = to_type(buf_->front());
	 return self.complete(ec);
      }
   }
};

template <
   class AsyncReadStream,
   class Storage,
   class CompletionToken =
      net::default_completion_token_t<typename AsyncReadStream::executor_type>
   >
auto async_read_type(
   AsyncReadStream& stream,
   Storage& buffer,
   type& t,
   CompletionToken&& token =
      net::default_completion_token_t<typename AsyncReadStream::executor_type>{})
{
   return net::async_compose
      < CompletionToken
      , void(boost::system::error_code)
      >(type_op<AsyncReadStream, Storage> {stream, &buffer, &t},
        token,
        stream);
}

template <
   class AsyncReadStream,
   class Receiver>
net::awaitable<void>
async_read_responses(
   AsyncReadStream& socket,
   Receiver& recv)
{
   using response_id_type = response_id<typename Receiver::event_type>;

   std::string buffer;

   // Used to queue the events of a transaction.
   std::queue<response_id_type> trans;

   for (;;) {
      type t;
      co_await async_read_type(socket, buffer, t);
      auto& req = recv.reqs.front();
      auto const cmd = t == type::push ? command::none : req.events.front().first;

      auto const is_multi = cmd == command::multi;
      auto const is_exec = cmd == command::exec;
      auto const trans_empty = std::empty(trans);

      // The next two ifs are used to deal with transactions.
      if (is_multi || (!trans_empty && !is_exec)) {
         // The multi commands always gets a "OK" response and all other
         // commands get QUEUED unless the user is e.g. using wrong data types.
	 auto const* res = cmd == command::multi ? "OK" : "QUEUED";

         response_static_string<char, 6> tmp;
	 co_await async_read(socket, buffer, tmp);

         // Failing to QUEUE a command inside a trasaction is considered an
         // application error.
	 assert (tmp.result == res);

         // Pushes the command in the transction command queue that will be
         // processed when exec arrives.
	 trans.push({req.events.front().first, type::invalid, req.events.front().second});
	 req.events.pop();
	 continue;
      }

      if (cmd == command::exec) {
	 assert(trans.front().cmd == command::multi);

         // The exec response is an array where each element is the response of
         // one command in the transaction. This requires a special response
         // buffer, that can deal with recursive data types.
         response_id_type id;
         id.cmd = command::exec;
         id.t = t;
         id.event = req.events.front().second;

         auto* tmp = recv.get_response_buffer().get(id);
	 co_await async_read(socket, buffer, *tmp);

	 trans.pop(); // Removes multi.
         recv.receive_transaction(std::move(trans));
	 trans = {};

	 req.events.pop(); // exec
	 if (std::empty(req.events)) {
	    recv.reqs.pop();
	    if (!std::empty(recv.reqs)) {
	       co_await async_write(
		  socket,
		  recv.reqs.front());
	    }
	 }
	 continue;
      }

      response_id_type id{cmd, t, req.events.front().second}; 
      auto* tmp = recv.get_response_buffer().get(id);
      co_await async_read(socket, buffer, *tmp);
      recv.receive(id);

      if (t != type::push)
	 req.events.pop();

      if (std::empty(req.events)) {
	 recv.reqs.pop();
	 if (!std::empty(recv.reqs)) {
	    co_await async_write(
	       socket,
	       recv.reqs.front());
	 }
      }
   }
}

template <class Event>
class receiver_base {
private:
   responses<Event> resps_;

public:
   responses<Event>& get_response_buffer() { return resps_; }
   responses<Event> const& get_response_buffer() const noexcept { return resps_; }

   std::queue<request<Event>> reqs;

   bool add(request<Event> req)
   {
      auto const empty = std::empty(reqs);
      reqs.push(std::move(req));
      return empty;
   }

   // NOTE: The ids in the queue parameter have an unspecified message type.
   virtual void receive_transaction(std::queue<response_id<Event>> ids)
   {
      while (!std::empty(ids)) {
        std::cout << ids.front() << std::endl;
        ids.pop();
      }

      get_response_buffer().clear_transaction();
   }

   virtual void receive(response_id<Event> const& id)
   {
      //std::cout << id << ": " << v.back() << std::endl;
      std::cout << id << std::endl;
      get_response_buffer().clear();
   }
};

} // resp
} // aedis
