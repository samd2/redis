/* Copyright (c) 2018-2022 Marcelo Zimbres Silva (mzimbres@gmail.com)
 *
 * Distributed under the Boost Software License, Version 1.0. (See
 * accompanying file LICENSE.txt)
 */

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <set>
#include <iterator>
#include <string>
#include <boost/json.hpp>
#include <boost/json/src.hpp>
#include <aedis/aedis.hpp>
#include <aedis/src.hpp>
#include "print.hpp"

namespace net = boost::asio;
using aedis::command;
using aedis::resp3::request;
using connection = aedis::connection<>;
using namespace boost::json;

struct user {
   std::string name;
   std::string age;
   std::string country;
};

void tag_invoke(value_from_tag, value& jv, user const& u)
{
   jv =
   { {"name", u.name}
   , {"age", u.age}
   , {"country", u.country}
   };
}

template<class T>
void extract(object const& obj, T& t, boost::string_view key)
{
    t = value_to<T>(obj.at(key));
}

user tag_invoke(value_to_tag<user>, value const& jv)
{
    user u;
    object const& obj = jv.as_object();
    extract(obj, u.name, "name");
    extract(obj, u.age, "age");
    extract(obj, u.country, "country");
    return u;
}

// Serializes
void to_bulk(std::string& to, user const& u)
{
   aedis::resp3::to_bulk(to, serialize(value_from(u)));
}

// Deserializes
void from_bulk(user& u, boost::string_view sv, boost::system::error_code& ec)
{
   value jv = parse(sv);
   u = value_to<user>(jv);
}

std::ostream& operator<<(std::ostream& os, user const& u)
{
   os << "Name: " << u.name << "\n"
      << "Age: " << u.age << "\n"
      << "Country: " << u.country;

   return os;
}

bool operator<(user const& a, user const& b)
{
   return std::tie(a.name, a.age, a.country) < std::tie(b.name, b.age, b.country);
}

int main()
{
   net::io_context ioc;
   connection db{ioc};

   // Request that sends the containers.
   std::set<user> users
      { {"Joao", "56", "Brazil"}
      , {"Serge", "60", "France"}
      };

   request req;
   req.push_range(command::sadd, "sadd-key", users);
   req.push(command::smembers, "sadd-key");
   req.push(command::quit);

   std::tuple<int, std::set<user>, std::string> resp;

   db.async_exec("127.0.0.1", "6379", req, aedis::adapt(resp),
      [](auto ec, auto) { std::cout << ec.message() << std::endl; });

   ioc.run();

   // Print
   print(std::get<1>(resp));
}
