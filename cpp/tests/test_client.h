#include <thread>
#include "boost/asio.hpp"
#include "test_models.h"

using boost::asio::ip::tcp;

class test_tcp_client{
public:
    test_tcp_client(std::string host, uint16_t port, on_connection_callback callback);
    ~test_tcp_client();
    int connect();
    void disconnect();

private:
    uint16_t server_port;
    std::string server_host;
    boost::shared_ptr<tcp::socket> m_connection = NULL;
    boost::shared_ptr<boost::asio::io_context> m_io_context = NULL;
    on_connection_callback m_connected_callback = NULL;
};
