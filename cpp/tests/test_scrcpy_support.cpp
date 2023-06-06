#include "logging.h"
#include "scrcpy_recv/scrcpy_recv.h"
#include <mutex>
#include <thread>
#include <queue>
#include <chrono>
#include <fstream>
#include <functional>
#include <sys/stat.h>
#include "boost/asio.hpp"
#include "utils.h"
#include "test_client.h"

using boost::asio::ip::tcp;

#define TEST_RECV_TOKEN "test_receiver"
#define TEST_RECV_DEVICE_ID "test001"
#define TEST_SOCKET_TYPE_VIDEO "video"
#define TEST_SOCKET_TYPE_CTRL "ctrl"
#define TEST_RECV_PORT 20017
#define TEST_RECV_STATUS_RUNNING 1
#define TEST_RECV_STATUS_STOPPED 0
#define TEST_RECV_WAIT_LISTENER_START 1
#define TEST_SCREEN_WIDTH  900
#define TEST_SCREEN_HEIGHT  600
#define TEST_IMG_WIDTH 300
#define TEST_IMG_HEIGHT 200

std::mutex s_device_info_result_lock;
std::queue<bool> *s_device_info_result_q; 

class scrcpy_support_tester{
public:
    scrcpy_support_tester(std::string video_file):m_video_file_path(video_file) {

    }
    ~scrcpy_support_tester() {
        if(NULL != this->listener) {
            scrcpy_free_receiver(this->listener);
            this->listener = NULL;
        }
    }
    void do_test() {
        validate_video_file();
        // step 00: simple startup and shutdown
        do_test_template(NULL);

        // step 01: start receiver, register callbacks, then shutdown
        do_test_template([this](){
                register_all_events();
                unregister_all_events();
                unregister_all_events();
        });

        // step 02: start receiver, register calblack, 
        // connect client video socket and then check device connected, 
        // disconnected client, unregister all callback, then shutdown
        do_test_template([this](){
                register_all_events();

                SPDLOG_INFO("Connect a video socket and wait for device info callback");
                log_flush();

                auto device_info_got_ok = connect_video_socket(NULL, 10);
                assert(device_info_got_ok);
                unregister_all_events();

                SPDLOG_INFO("unregister_all_events, then connect a video socket and wait for device info callback");
                log_flush();
                // should not receive any device info callback
                device_info_got_ok = connect_video_socket(NULL, 5);
                SPDLOG_INFO("Should not receive any device info callback, is it behave ok? {}", device_info_got_ok ? "NO":"YES");
                log_flush();
                assert(!device_info_got_ok);
        });
    }

private:
    scrcpy_listener_t listener = NULL;
    std::mutex m_svr_lock;
    std::string m_video_file_path;
    volatile int m_svr_status = TEST_RECV_STATUS_STOPPED;
    
    void receiver_thread_entry() {
        auto port = fmt::format("{}", TEST_RECV_PORT);
        {
            std::lock_guard<std::mutex> lock(this->m_svr_lock);
            this->m_svr_status = TEST_RECV_STATUS_RUNNING;
        }
        int kb2048 = 2048;
        SPDLOG_DEBUG("Receiver will be started");
        log_flush();

        scrcpy_start_receiver(this->listener, (char *)port.c_str(), kb2048, kb2048 * 2);

        SPDLOG_DEBUG("Receiver thread just stopped");
        log_flush();

        {
            SPDLOG_INFO("Locking m_svr_lock and set m_svr_status to stopped");
            log_flush();
            std::lock_guard<std::mutex> lock(this->m_svr_lock);
            this->m_svr_status = TEST_RECV_STATUS_STOPPED;
        }
        SPDLOG_INFO("receiver_thread_entry will end soon");
        log_flush();
    }
    void start_receiver() {
        auto token = (char *)TEST_RECV_TOKEN;
        this->listener = scrcpy_new_receiver(token);
        assert(this->listener);
        std::thread t(&scrcpy_support_tester::receiver_thread_entry, this);
        t.detach();

        SPDLOG_INFO("Wait {} second(s) for receiver startup", TEST_RECV_WAIT_LISTENER_START);
        log_flush();

        std::this_thread::sleep_for(std::chrono::seconds(TEST_RECV_WAIT_LISTENER_START));

        std::lock_guard<std::mutex> lock(this->m_svr_lock);
        auto is_receiver_running = this->m_svr_status == TEST_RECV_STATUS_RUNNING;
        SPDLOG_INFO("Is receiver running? {}", is_receiver_running ? "YES":"NO");
        log_flush();

        assert(is_receiver_running);
    }

    char *encode_socket_header(char *device_id, char *socket_type) {
        char *result =  (char *) malloc(80 * sizeof(char));
        int device_id_len = strlen(device_id);
        int socket_type_len = strlen(socket_type);
        array_copy_to2(device_id, result, 0, 0, device_id_len);
        array_copy_to2(socket_type, result, 0, 64, socket_type_len);
        return result;
    }
    uint8_t *tiny_int_bytes(int value) {
        uint8_t *result = (uint8_t *)malloc(2 * sizeof(uint8_t));
        result[0] = value >> 8;
        result[1] = value;
        return result;
    }
    char *encode_device_info(char *device_id, int screen_width, int screen_height) {
        // device_id 64 bytes, width 2 bytes, height 2 bytes
        char *result = (char *)malloc(68 * sizeof(char));
        // copy device_id into result
        array_copy_to(device_id, result, 0, strlen(device_id));
        char *width_bytes = (char *)tiny_int_bytes(screen_width);
        char *height_bytes = (char *)tiny_int_bytes(screen_height);
        array_copy_to2(width_bytes, result, 0, 64, 2);
        array_copy_to2(height_bytes, result, 0, 66, 2);
        free(width_bytes);
        free(height_bytes);
        return result;
    }
    void validate_video_file() {
        assert(this->m_video_file_path.length() > 0);
        struct stat buffer;
        auto stat_ok = stat(this->m_video_file_path.c_str(), &buffer) == 0;
        SPDLOG_INFO("Will use video file {} for testing, is it existed? {}", this->m_video_file_path, stat_ok ? "YES":"NO");
        log_flush();
    }
    void shutdown_receiver_thread_method() {
        scrcpy_shutdown_receiver(this->listener);
        this->listener = NULL;
    }
    void shutdown_receiver() {
        assert(this->m_svr_status == TEST_RECV_STATUS_RUNNING);
        assert(this->listener);
        SPDLOG_INFO("Trying to shutdown the receiver in thread");
        log_flush();

        std::thread t(&scrcpy_support_tester::shutdown_receiver_thread_method, this);
        t.detach();
        int wait_time = TEST_RECV_WAIT_LISTENER_START * 10;
        SPDLOG_INFO("Wait {} second(s) at most before checking if server is stopped.", wait_time);
        log_flush();
        
        auto is_receiver_stopped = this->m_svr_status == TEST_RECV_STATUS_STOPPED;
        auto sleep_times = wait_time * 1000 / 100;
        while(sleep_times > 0) {
            SPDLOG_INFO("Wait time left {}.", sleep_times);
            log_flush();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            sleep_times --;
            {
                std::lock_guard<std::mutex> lock(this->m_svr_lock);
                is_receiver_stopped = this->m_svr_status == TEST_RECV_STATUS_STOPPED;
            }
            if(is_receiver_stopped) {
                break;
            }
        }
        SPDLOG_INFO("Is receiver stopped? {}", is_receiver_stopped ? "YES":"NO");
        log_flush();
        assert(is_receiver_stopped);
    }
    static void frame_img_callback(char *token, char *device_id, uint8_t *img_data, uint32_t img_data_len, scrcpy_rect img_size, scrcpy_rect screen_size) {
    }
    static void device_info_callback(char *token, char *device_id, int screen_width, int screen_height) {
        std::lock_guard<std::mutex> lock(s_device_info_result_lock);
        auto ok = strcmp(token, (char *)TEST_RECV_TOKEN) == 0 && 
            strcmp(device_id, (char *)TEST_RECV_DEVICE_ID) == 0 &&
            screen_width == TEST_SCREEN_WIDTH && screen_height == TEST_SCREEN_HEIGHT;
        SPDLOG_INFO("device_info_callback is {}. Invoking with token={} device_id={} screen_width={} screen_height={}", 
                ok ? "correct":"wrong",
                token, device_id, screen_width, screen_height);
        s_device_info_result_q->push(ok);
    }
    static void ctrl_msg_callback(char *token, char *device_id, char *msg_id, int status, int data_len) {
    }
    static void client_disconnected(char *token, char *device_id, char *con_type) {
    }
    void register_all_events() {
        SPDLOG_INFO("register_all_events callbacks");
        log_flush();
        assert(this->listener);
        scrcpy_frame_register_callback(this->listener, (char *)TEST_RECV_DEVICE_ID, &scrcpy_support_tester::frame_img_callback);
        scrcpy_device_info_register_callback(this->listener, (char *)TEST_RECV_DEVICE_ID, &scrcpy_support_tester::device_info_callback);
        scrcpy_device_set_ctrl_msg_send_callback(this->listener, (char *)TEST_RECV_DEVICE_ID, &scrcpy_support_tester::ctrl_msg_callback);
        scrcpy_set_device_disconnected_callback(this->listener, &scrcpy_support_tester::client_disconnected);
    }
    void unregister_all_events() {
        SPDLOG_INFO("unregister_all_events callbacks");
        log_flush();
        assert(this->listener);
        scrcpy_frame_unregister_all_callbacks(this->listener, (char *)TEST_RECV_DEVICE_ID);
        scrcpy_device_info_unregister_all_callbacks(this->listener, (char *)TEST_RECV_DEVICE_ID);
        scrcpy_device_set_ctrl_msg_send_callback(this->listener, (char *)TEST_RECV_DEVICE_ID, NULL);
        scrcpy_set_device_disconnected_callback(this->listener, NULL);
    }
    bool send_socket_type(boost::shared_ptr<tcp::socket> conn, char *socket_type) {
        auto header_info = this->encode_socket_header((char *)TEST_RECV_DEVICE_ID, socket_type);
        bool header_sent = false;
        try {
            auto sent_bytes = conn->send(boost::asio::buffer(header_info, 80));
            SPDLOG_INFO("Already sent {} bytes video socket header data to server", sent_bytes);
            log_flush();
            header_sent = sent_bytes == 80;
        } catch(boost::system::system_error& e) {
            SPDLOG_ERROR("Failed to send video socket header to server port {}: {}", TEST_RECV_PORT, e.what());
            log_flush();
            header_sent = false;
        }
        free(header_info);
        return header_sent;
    }
    bool send_device_info(boost::shared_ptr<tcp::socket> conn) {
        auto device_info = this->encode_device_info((char *)TEST_RECV_DEVICE_ID, TEST_SCREEN_WIDTH, TEST_SCREEN_HEIGHT);
        bool header_sent = false;
        try {
            auto bytes_sent = conn->send(boost::asio::buffer(device_info, 68));
            header_sent = bytes_sent == 68;
            SPDLOG_INFO("Sent {} bytes of device info to {}, sent ok? {}", bytes_sent, conn->remote_endpoint().port(), 
                    header_sent ? "YES" : "NO");
            log_flush();
        } catch(boost::system::system_error& e) {
            SPDLOG_ERROR("Failed to send device info to {}: {}", conn->remote_endpoint().port(), e.what());
            log_flush();
        }
        free(device_info);
        return header_sent;
    }
    bool connect_video_socket(std::function<void(void)> after_headers_sent, uint32_t wait_seconds) {
        std::queue<bool>  *device_info_q = new std::queue<bool>();
        s_device_info_result_q = device_info_q;
        SPDLOG_INFO("Trying to connect a video socket to port {}", TEST_RECV_PORT);
        // connect a socket
        auto client = new test_tcp_client((char *)"127.0.0.1", (uint16_t) TEST_RECV_PORT, 
                [this, after_headers_sent](boost::shared_ptr<tcp::socket> conn){
                SPDLOG_INFO("Video socket connected to server {}", conn->remote_endpoint().port());
                std::lock_guard<std::mutex> lock(s_device_info_result_lock);
                // send device info header
                auto header_sent = this->send_socket_type(conn, (char *)TEST_SOCKET_TYPE_VIDEO) && this->send_device_info(conn);
                // send device info
                if (header_sent && after_headers_sent) {
                    SPDLOG_INFO("Invoking callback after both video socket header and device info sent");
                    log_flush();
                    after_headers_sent();
                }
        });
        bool client_connected = client->connect() == 0;
        SPDLOG_INFO("Video socket connected ok ? {}", client_connected ? "YES" : "NO");
        log_flush();
        if (!client_connected){
            delete device_info_q;
            s_device_info_result_q = NULL;
            return false;
        }
        // wait for callback
        int sleep_ms = 100;
        int max_wait = wait_seconds * 1000 / sleep_ms;
        bool got_result = false;
        bool result_data = false;
        while(max_wait > 0) {
            max_wait --;
            {
                std::lock_guard<std::mutex> lock(s_device_info_result_lock);
                got_result = !device_info_q->empty();
            }
            if (!got_result) {
                // wait for 100 ms
                std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
                continue;
            }
            result_data = device_info_q->front();
            device_info_q->pop();
            break;
        }
        delete device_info_q;
        s_device_info_result_q = NULL;
        SPDLOG_INFO("Got device info callback data ? {} Is device info callback correct ? {}", 
                got_result ? "YES":"NO", result_data ? "YES":"NO");
        client->disconnect();
        return got_result && result_data;
    }
    void connect_ctrl_socket() {

    }
    void do_test_template(std::function<void(void)> after_startup) {
        start_receiver();
        if(after_startup) {
            after_startup();
        }
        shutdown_receiver();
    }
};

int main(int argc, char *argv[]) {
    if (argc < 2) {
        SPDLOG_ERROR("A h264 file is needed as first argument");
        return 1;
    }
    SPDLOG_INFO("Start running test_scrcpy_support");
    log_flush();
    char *video_file = argv[1];
    assert(strlen(video_file) > 0);
    auto tester = new scrcpy_support_tester(std::string(video_file));
    tester->do_test();
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    delete tester;

    SPDLOG_INFO("Finished running test_scrcpy_support");
    log_flush();
    return 0;
}
