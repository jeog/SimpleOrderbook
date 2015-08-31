
#include "simple_orderbook.hpp"

namespace NativeLayer{

namespace SimpleOrderbook{

std::string
QueryInterface::timestamp_to_str(const QueryInterface::time_stamp_type& tp)
{
  std::time_t t = clock_type::to_time_t(tp);
  std::string ts = std::ctime(&t);
  ts.resize(ts.size() -1);
  return ts;
}

};
};

