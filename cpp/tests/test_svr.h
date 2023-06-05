#include <string>
#include <functional>
#include <thread>
#include <mutex>
#include "boost/asio.hpp"
#include <vector>
#include "test_models.h"



class test_tcp_server {
public:
    test_tcp_server(uint16_t port_no, on_connection_callback conn_callback);
    ~test_tcp_server();
    int startup();
    void shutdown();
private:
    boost::shared_ptr<tcp::acceptor> m_acceptor = NULL;
    boost::shared_ptr<boost::asio::io_context> m_io_context = NULL;
    std::mutex m_gaint_lock;

    on_connection_callback m_conn_callback = NULL;
    uint16_t m_port_no = 0;
    volatile int m_shutdown_flag = 0;

    void run();
};
