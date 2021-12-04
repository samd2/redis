#include <aedis/aedis.hpp>

#include <iostream>

#include "client_base.hpp"

namespace net = aedis::net;

using aedis::command;
using aedis::resp3::request;
using aedis::resp3::type;
using aedis::resp3::response;
using aedis::resp3::response_base;
using aedis::resp3::client_base;

using tcp_socket = aedis::net::use_awaitable_t<>::as_default_on_t<aedis::net::ip::tcp::socket>;
using tcp_resolver = aedis::net::use_awaitable_t<>::as_default_on_t<aedis::net::ip::tcp::resolver>;
using timer = aedis::net::use_awaitable_t<>::as_default_on_t<aedis::net::steady_timer>;
using tcp_acceptor = aedis::net::use_awaitable_t<>::as_default_on_t<aedis::net::ip::tcp::acceptor>;
using namespace aedis::net::experimental::awaitable_operators;

struct user_session_base {
  virtual ~user_session_base() {}
  virtual void on_event(command cmd) = 0;
};

struct queue_elem {
  command cmd = command::unknown;
  response_base* resp = nullptr;
  std::weak_ptr<user_session_base> session = std::shared_ptr<user_session_base>{nullptr};
};

auto get_command(queue_elem const& e)
  { return e.cmd; }

class my_redis_client : public client_base<queue_elem> {
private:
   void on_event(queue_elem qe) override
   {
      if (auto session = qe.session.lock()) {
         session->on_event(qe.cmd);
      } else {
         std::cout << "Session expired." << std::endl;
      }
   }

public:
   my_redis_client(net::any_io_executor ex)
   : client_base<queue_elem>(ex)
   {}
};

class user_session:
   public user_session_base,
   public std::enable_shared_from_this<user_session> {
public:
   user_session(tcp_socket socket, std::shared_ptr<my_redis_client> rclient)
   : socket_(std::move(socket))
   , timer_(socket_.get_executor())
   , rclient_{rclient}
   {
     timer_.expires_at(std::chrono::steady_clock::time_point::max());
   }

   void start()
   {
     co_spawn(socket_.get_executor(),
         [self = shared_from_this()]{ return self->reader(); },
         net::detached);

     co_spawn(socket_.get_executor(),
         [self = shared_from_this()]{ return self->writer(); },
         net::detached);
   }

   void on_event(command cmd) override
   {
      assert(cmd == command::ping);
      deliver(resp_.raw().back().data);
      resp_.clear();
   }

private:
   net::awaitable<void> reader()
   {
     try {
       for (std::string msg;;) {
         auto const n =
            co_await net::async_read_until(socket_, net::dynamic_buffer(msg, 1024), "\n");

         auto filler = [self = shared_from_this(), &msg](auto& req)
            { req.push({command::ping, &self->resp_, self}, msg); };

         rclient_->send(filler);

         msg.erase(0, n);
       }
     } catch (std::exception&) {
       stop();
     }
   }

   net::awaitable<void> writer()
   {
     try {
       while (socket_.is_open()) {
         if (write_msgs_.empty()) {
           boost::system::error_code ec;
           co_await timer_.async_wait(redirect_error(net::use_awaitable, ec));
         } else {
           co_await net::async_write(socket_, net::buffer(write_msgs_.front()));
           write_msgs_.pop_front();
         }
       }
     } catch (std::exception&) {
       stop();
     }
   }

   void deliver(const std::string& msg)
   {
     write_msgs_.push_back(msg);
     timer_.cancel_one();
   }

   void stop()
   {
     socket_.close();
     timer_.cancel();
   }

   tcp_socket socket_;
   net::steady_timer timer_;
   std::deque<std::string> write_msgs_;
   std::shared_ptr<my_redis_client> rclient_;
   response resp_;
};

// Start this after the connection to the database has been stablished.
net::awaitable<void> listener()
{
   auto ex = co_await net::this_coro::executor;
   tcp_acceptor acceptor(ex, {net::ip::tcp::v4(), 55555});
   
   auto rclient = std::make_shared<my_redis_client>(ex);
   rclient->start();

   for (;;)
     std::make_shared<user_session>(co_await acceptor.async_accept(), rclient)->start();
}

int main()
{
  try {
    net::io_context io_context(1);

    net::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto){ io_context.stop(); });

    co_spawn(io_context, listener(), net::detached);

    io_context.run();
  } catch (std::exception& e) {
    std::printf("Exception: %s\n", e.what());
  }
}

/// \example echo_server.cpp