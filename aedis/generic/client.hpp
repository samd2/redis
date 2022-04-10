/* Copyright (c) 2019 Marcelo Zimbres Silva (mzimbres@gmail.com)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <vector>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>

#include <aedis/resp3/type.hpp>
#include <aedis/generic/detail/client_ops.hpp>
#include <aedis/redis/command.hpp>

// TODO: What to do if users send a discard command not contained in a
// transaction. The client object will try to pop the queue until a
// multi is found.

namespace aedis {
namespace generic {

/** \brief A high level Redis client.
 *  \ingroup any
 *
 *  This class represents a connection to the Redis server. Some of
 *  its most important features are
 *
 *  1. Automatic management of commands. The implementation will send
 *     commands and read responses automatically for the user.
 *  2. Memory reuse. Dynamic memory allocations will decrease with time.
 *
 *  For more details, please see the documentation of each individual
 *  function.
 */
template <class AsyncReadWriteStream, class Command>
class client {
public:
   using stream_type = AsyncReadWriteStream;
   using executor_type = typename stream_type::executor_type;
   using default_completion_token_type = boost::asio::default_completion_token_t<executor_type>;

   /** \brief Constructor.
    *
    *  \param ex The executor.
    */
   client(boost::asio::any_io_executor ex)
   : socket_{ex}
   , timer_{ex}
   {
      timer_.expires_at(std::chrono::steady_clock::time_point::max());
   }

   /// Returns the executor.
   auto get_executor() {return socket_.get_executor();}

   /** @brief Adds a command to the output command queue.
    *
    *  Adds a command to the output command queue and signals the write
    *  operation there are new messages awaiting to be sent to Redis.
    *
    *  @sa serializer.hpp
    *
    *  @param cmd The command to send.
    *  @param args Arguments to commands.
    */
   template <class... Ts>
   void send(Command cmd, Ts const&... args)
   {
      auto const can_write = prepare_next();

      serializer<std::string> sr(requests_);
      auto const before = requests_.size();
      sr.push(cmd, args...);
      auto const after = requests_.size();
      assert(after - before != 0);
      req_info_.front().size += after - before;;

      if (!has_push_response(cmd)) {
         commands_.push_back(cmd);
         ++req_info_.front().cmds;
      }

      if (can_write)
         timer_.cancel_one();
   }

   /** @brief Adds a command to the output command queue.
    *
    *  Adds a command to the output command queue and signals the write
    *  operation there are new messages awaiting to be sent to Redis.
    *
    *  @sa serializer.hpp
    *
    *  @param cmd The command.
    *  @param key The key the commands refers to
    *  @param begin Begin of the range.
    *  @param end End of the range.
    */
   template <class Key, class ForwardIterator>
   void send_range2(Command cmd, Key const& key, ForwardIterator begin, ForwardIterator end)
   {
      if (begin == end)
         return;

      auto const can_write = prepare_next();

      serializer<std::string> sr(requests_);
      auto const before = requests_.size();
      sr.push_range2(cmd, key, begin, end);
      auto const after = requests_.size();
      assert(after - before != 0);
      req_info_.front().size += after - before;;

      if (!has_push_response(cmd)) {
         commands_.push_back(cmd);
         ++req_info_.front().cmds;
      }

      if (can_write)
         timer_.cancel_one();
   }

   /** @brief Adds a command to the output command queue.
    *
    *  Adds a command to the output command queue and signals the write
    *  operation there are new messages awaiting to be sent to Redis.
    *
    *  @sa serializer.hpp
    *
    *  @param cmd The command.
    *  @param begin Begin of the range.
    *  @param end End of the range.
    */
   template <class ForwardIterator>
   void send_range2(Command cmd, ForwardIterator begin, ForwardIterator end)
   {
      if (begin == end)
         return;

      auto const can_write = prepare_next();

      serializer<std::string> sr(requests_);
      auto const before = requests_.size();
      sr.push_range2(cmd, begin, end);
      auto const after = requests_.size();
      assert(after - before != 0);
      req_info_.front().size += after - before;;

      if (!has_push_response(cmd)) {
         commands_.push_back(cmd);
         ++req_info_.front().cmds;
      }

      if (can_write)
         timer_.cancel_one();
   }

   /** @brief Adds a command to the output command queue.
    *
    *  Adds a command to the output command queue and signals the write
    *  operation there are new messages awaiting to be sent to Redis.
    *
    *  @sa serializer.hpp
    *
    *  @param cmd The command.
    *  @param key The key the commands refers to.
    *  @param range Range of elements to send.
    */
   template <class Key, class Range>
   void send_range(Command cmd, Key const& key, Range const& range)
   {
      using std::begin;
      using std::end;
      send_range2(cmd, key, begin(range), end(range));
   }

   /** @brief Adds a command to the output command queue.
    *
    *  Adds a command to the output command queue and signals the write
    *  operation there are new messages awaiting to be sent to Redis.
    *
    *  @sa serializer.hpp
    *
    *  @param cmd The command.
    *  @param range End of the range.
    */
   template <class Range>
   void send_range(Command cmd, Range const& range)
   {
      using std::begin;
      using std::end;
      send_range2(cmd, begin(range), end(range));
   }

   /** @brief Starts communication with the Redis server asynchronously.
    *
    *  This class performs the following steps
    *
    *  @li Connect to the endpoint passed in the function parameter.
    *  @li Start the async read operation that keeps reading responses to commands and server pushes.
    *  @li Start the async write operation that keeps sending commands to Redis.
    *
    *  \param recv The receiver (see below)
    *  \param ep The address of the Redis server.
    *  \param token The completion token (ASIO jargon)
    *
    *  The receiver is a class that privides the following member functions
    *
    *  @code
    *  class receiver {
    *  public:
    *     // Called when a new chunck of user data becomes available.
    *     void on_resp3(command cmd, node<boost::string_view> const& nd, boost::system::error_code& ec);
    *
    *     // Called when a response becomes available.
    *     void on_read(command cmd);
    *
    *     // Called when a request has been writen to the socket.
    *     void on_write(std::size_t n);
    *
    *     // Called when a server push is received.
    *     void on_push();
    *  };
    *  @endcode
    *
    */
   template <
     class Receiver,
     class CompletionToken = default_completion_token_type
   >
   auto
   async_run(
      Receiver& recv,
      boost::asio::ip::tcp::endpoint ep = {boost::asio::ip::make_address("127.0.0.1"), 6379},
      CompletionToken token = CompletionToken{})
   {
      endpoint_ = ep;
      return boost::asio::async_compose
         < CompletionToken
         , void(boost::system::error_code)
         >(run_op<client, Receiver>{this, &recv}, token, socket_, timer_);
   }

private:
   template <class T, class U, class V> friend struct read_op;
   template <class T, class U> friend struct writer_op;
   template <class T, class U> friend struct read_write_op;
   template <class T, class U> friend struct run_op;

   struct request_info {
      // Request size in bytes.
      std::size_t size = 0;

      // The number of commands it contains excluding commands that
      // have push types as responses, see has_push_response.
      std::size_t cmds = 0;
   };

   // Buffer used in the read operations.
   std::string read_buffer_;

   // Requests payload.
   std::string requests_;

   // The commands contained in the requests.
   std::vector<Command> commands_;

   // Info about the requests.
   std::vector<request_info> req_info_;

   // The stream.
   stream_type socket_;

   // Timer used to inform the write coroutine that it can write the
   // next message in the output queue.
   boost::asio::steady_timer timer_;

   // Redis endpoint.
   boost::asio::ip::tcp::endpoint endpoint_;

   bool stop_writer_ = false;

   /* Prepares the back of the queue to receive further commands. 
    *
    * If true is returned the request in the front of the queue can be
    * sent to the server. See async_write_some.
    */
   bool prepare_next()
   {
      if (req_info_.empty()) {
         req_info_.push_back({});
         return true;
      }

      if (req_info_.front().size == 0) {
         // It has already been written and we are waiting for the
         // responses.
         req_info_.push_back({});
         return false;
      }

      return false;
   }

   // Returns true when the next request can be writen.
   bool on_cmd(Command)
   {
      // TODO: If the response to a discard is received we have to
      // remove all commands up until multi.

      assert(!req_info_.empty());
      assert(!commands_.empty());

      commands_.erase(std::begin(commands_));

      if (--req_info_.front().cmds != 0)
         return false;

      req_info_.erase(std::begin(req_info_));

      return !req_info_.empty();
   }

   // Reads messages asynchronously.
   template <
      class Receiver,
      class CompletionToken = default_completion_token_type>
   auto
   async_reader(
      Receiver* recv,
      CompletionToken&& token = default_completion_token_type{})
   {
      return boost::asio::async_compose
         < CompletionToken
         , void(boost::system::error_code)
         >(read_op<client, Receiver, Command>{this, recv}, token, socket_);
   }

   template <
      class Receiver,
      class CompletionToken = default_completion_token_type>
   auto
   async_writer(
      Receiver* recv,
      CompletionToken&& token = default_completion_token_type{})
   {
      return boost::asio::async_compose
         < CompletionToken
         , void(boost::system::error_code)
         >(writer_op<client, Receiver>{this, recv}, token, socket_, timer_);
   }

   template <
      class Receiver,
      class CompletionToken = default_completion_token_type>
   auto
   async_read_write(
      Receiver* recv,
      CompletionToken&& token = default_completion_token_type{})
   {
      return boost::asio::async_compose
         < CompletionToken
         , void(boost::system::error_code)
         >(read_write_op<client, Receiver>{this, recv}, token, socket_, timer_);
   }
};

} // generic
} // aedis