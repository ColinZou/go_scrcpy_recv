#ifndef SCRCPY_TEST_MODELS
#define SCRCPY_TEST_MODELS 1
#include <functional>
#include "boost/asio.hpp"
using boost::asio::ip::tcp;

typedef std::function<void(boost::shared_ptr<tcp::socket>)> on_connection_callback;
#endif // !SCRCPY_TEST_MODELS
