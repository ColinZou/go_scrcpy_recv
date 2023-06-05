#include "test_svr.h"
#include "test_client.h"
#include "spdlog/spdlog.h"
#include "scrcpy_ctrl_handler.h"
#include <queue>
#include <thread>
#include <chrono>
#include <mutex>
#include "logging.h"

int test_svr_port = 20016;
scrcpy_ctrl_socket_handler *ctrl_handler = NULL;
std::string t_device_id = "unit_test_device";
std::queue<bool> *result_q = new std::queue<bool>();
std::mutex result_q_lock;
std::string t_msg_id = "msg001";
uint8_t t_msg_data[] = {0x00, 0xFF, 0x00, 0x01};
int t_msg_data_len = 4;
int counter = 0;

void receive_data_thread_method(boost::shared_ptr<tcp::socket> server_conn) {
   uint8_t data_received[4];
   auto receive_len = server_conn->receive(boost::asio::buffer(data_received, t_msg_data_len));
   std::lock_guard<std::mutex> lock(result_q_lock);
   counter += 1;
   bool ok = t_msg_data_len == receive_len && strcmp((char*) data_received, (char *) t_msg_data) == 0;
   SPDLOG_INFO("Trying to receive {} bytes data from ctrl socket,  {} bytes read. Is data correct ? {}", t_msg_data_len, receive_len, ok ? "YES":"NO");
   log_flush();
   server_conn->close();
   result_q->push(ok);
}
void msg_sender_thread_method() {
    auto tmp_msg_id = t_msg_id;
    auto tmp_msg_data = t_msg_data;
    auto tmp_data_len = t_msg_data_len;
    auto tmp_device_id = t_device_id;

    ctrl_handler->run([tmp_msg_id, tmp_msg_data, tmp_data_len, tmp_device_id](std::string device_id, std::string msg_id, int msg_len, int send_status){
            std::lock_guard<std::mutex> lock(result_q_lock);
            counter += 1;
            bool ok = msg_len == send_status && 
                tmp_msg_id._Equal(msg_id) &&
                tmp_device_id._Equal(device_id) &&
                tmp_data_len == msg_len;
            SPDLOG_INFO("Got a ctrl msg sending callback, device_id={} msg_id={} msg_len={} send_status={}", device_id, msg_id, msg_len, send_status);
            log_flush();
            result_q->push(ok);
    });
}
void on_connection_accepted(boost::shared_ptr<tcp::socket> conn) {
    ctrl_handler = new scrcpy_ctrl_socket_handler(&t_device_id, conn);
    std::thread msg_sender_thread(msg_sender_thread_method);
    msg_sender_thread.detach();
    
    // try sending a message
    std::lock_guard<std::mutex> lock(result_q_lock);
    SPDLOG_DEBUG("Trying to send a message");
    ctrl_handler->send_msg((char *)t_msg_id.c_str(), t_msg_data, t_msg_data_len);
}

void wait_for_result() {
    int max_wait_count = 100;
    bool result = false;
    bool got_result = false;
    while(max_wait_count > 0) {
        max_wait_count --;
        {
            std::lock_guard<std::mutex> lock(result_q_lock);
            if (result_q->size() >= 2) {
                got_result = true;
                result = result_q->front();
                result_q->pop();
                result = result && result_q->front();
                result_q->pop();
                break;
            }
        }
        if (!got_result) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    SPDLOG_INFO("Got result ? {}. Was it passed ? {}", got_result ? "YES":"NO", result ? "YES":"NO");
    log_flush();
    assert(got_result);
    assert(result);
    delete result_q;
}

int main() {
    // run a server
    SPDLOG_DEBUG("Try starting a test server");
    test_tcp_server *test_svr = new test_tcp_server(test_svr_port, [](boost::shared_ptr<tcp::socket> con){
            on_connection_accepted(con);
    });
    if (test_svr->startup() != 0){
        delete test_svr;
        SPDLOG_ERROR("Failed to startup test server");
        log_flush();
        assert(false);
    }
    SPDLOG_DEBUG("Try starting a test client");
    // run a client
    test_tcp_client *test_client = new test_tcp_client("127.0.0.1", test_svr_port, [](boost::shared_ptr<tcp::socket> conn){
        std::thread recv_thread(receive_data_thread_method, conn);
        recv_thread.detach();
    });

    if (test_client->connect() != 0) {
        test_svr->shutdown();
        delete test_svr;
        SPDLOG_ERROR("Failed to connect to test svr");
        log_flush();
        delete test_client;
        assert(false);
    }

    wait_for_result();

    test_svr->shutdown();

    if(NULL != ctrl_handler) {
        ctrl_handler->stop();
        ctrl_handler = NULL;
    }
    
    SPDLOG_DEBUG("Wait one second before relasing server and client");
    log_flush();
    // wait it to shutdown
    std::this_thread::sleep_for(std::chrono::seconds(1));

    SPDLOG_DEBUG("Releasing test_svr");
    log_flush();
    delete test_svr;
    SPDLOG_DEBUG("Releasing test_client");
    log_flush();
    delete test_client;
    SPDLOG_DEBUG("Done test_scrcpy_ctrl_handler");
    return 0;
}
