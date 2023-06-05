#include "test_svr.h"
#include "spdlog/spdlog.h"
#include <chrono>
#include "logging.h"
std::string socket_to_string(boost::shared_ptr<tcp::socket> conn) {
    auto ep = conn->remote_endpoint();
    return fmt::format("{}:{}", ep.address().to_string(), ep.port());
}
test_tcp_server::test_tcp_server(uint16_t port_no, on_connection_callback conn_callback): 
    m_conn_callback(conn_callback), m_port_no(port_no) {
}

test_tcp_server::~test_tcp_server() {
    SPDLOG_DEBUG("starting ~test_tcp_server");
    log_flush();
    this->m_conn_callback = NULL;
    this->m_acceptor = NULL;
    this->m_shutdown_flag = 1;
    SPDLOG_DEBUG("finished ~test_tcp_server");
    log_flush();
}

int test_tcp_server::startup() {
    try {
        {
            std::lock_guard<std::mutex> lock(this->m_gaint_lock);
            this->m_io_context = boost::shared_ptr<boost::asio::io_context>(new boost::asio::io_context()); 
            this->m_acceptor = boost::shared_ptr<tcp::acceptor>(new tcp::acceptor(*this->m_io_context, tcp::endpoint(tcp::v4(), this->m_port_no)));
            this->m_acceptor->set_option(tcp::acceptor::reuse_address(true));
            std::thread t(&test_tcp_server::run, this);
            t.detach();
        }
        return 0;
    } catch(boost::system::system_error& e) {
        SPDLOG_ERROR("Failed to startup a server: {}", e.what());
        log_flush();
        return 1;
    }
    return 1;
}

void test_tcp_server::run() {
    while(true) {
        {
            {
                std::lock_guard<std::mutex> lock(this->m_gaint_lock);
                if(this->m_shutdown_flag > 0) {
                    SPDLOG_WARN("Got shutdown singal");
                    log_flush();
                    break;
                }
            }
            try {
                boost::shared_ptr<tcp::socket> shared_socket_ptr = boost::shared_ptr<tcp::socket>(new tcp::socket(*this->m_io_context));
                this->m_acceptor->accept(*shared_socket_ptr);
                SPDLOG_INFO("Got a connection: {}", socket_to_string(shared_socket_ptr));
                log_flush();
                if(NULL != this->m_conn_callback) {
                    this->m_conn_callback(shared_socket_ptr);
                }
            } catch (boost::system::system_error& e) {
                SPDLOG_ERROR("Failed to accept new connection: {}", e.what());
                log_flush();
                continue;
            }
        }
    }
    SPDLOG_INFO("Sever is shutting down");
    log_flush();
    this->m_acceptor->close();
    this->m_acceptor = NULL;
    SPDLOG_INFO("Acceptor closed");
    log_flush();
}

void test_tcp_server::shutdown() {
    std::lock_guard<std::mutex> lock(this->m_gaint_lock);
    SPDLOG_WARN("Setting m_shutdown_flag to 1");
    log_flush();
    this->m_shutdown_flag = 1;
    try {
        this->m_acceptor->cancel();
    }catch(boost::system::system_error& e) {
        SPDLOG_WARN("Failed to cannccel acceptor {}", e.what());
        log_flush();
    }
}
