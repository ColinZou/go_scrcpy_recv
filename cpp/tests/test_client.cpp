#include "test_client.h"
#include "spdlog/spdlog.h"
#include "logging.h"

test_tcp_client::test_tcp_client(std::string host, uint16_t port, on_connection_callback callback):
    server_host(host), server_port(port), m_connected_callback(callback) {
    assert(callback);
    assert(host.length() > 0);
    assert(port > 0);
}

test_tcp_client::~test_tcp_client() {
    SPDLOG_DEBUG("starting ~test_tcp_client");
    log_flush();
    if(this->m_connection) {
        this->m_connection->close();
        this->m_connection = NULL;
    }
    SPDLOG_DEBUG("finished ~test_tcp_client");
    log_flush();
}

int test_tcp_client::connect() {
    try {
        SPDLOG_DEBUG("Try connecting {}:{}", this->server_host, this->server_port);
        log_flush();
        this->m_io_context = boost::shared_ptr<boost::asio::io_context>(new boost::asio::io_context());
        tcp::resolver::query query(this->server_host, fmt::format("{}", this->server_port), tcp::resolver::query::numeric_service);
        tcp::resolver r(boost::asio::system_executor{});
        auto ep = r.resolve(query);

        this->m_connection = boost::shared_ptr<tcp::socket>(new tcp::socket(*this->m_io_context));
        boost::asio::connect(*this->m_connection, ep);
        
        if(this->m_connected_callback) {
            this->m_connected_callback(this->m_connection);
        }
    } catch(boost::system::system_error& e) {
        SPDLOG_ERROR("Failed to connect to {}:{} {}", this->server_host, this->server_port, e.what());
        log_flush();
        return 1;
    }
   return 0;
}

void test_tcp_client::disconnect() {
    if(this->m_connection && this->m_connection->is_open()) {
        this->m_connection->close();
        this->m_connection = NULL;
    }
}
