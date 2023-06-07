#include "logging.h"
#include "scrcpy_recv/scrcpy_recv.h"
#include <mutex>
#include <thread>
#include <queue>
#include <chrono>
#include <stdio.h>
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
#define TEST_MSG_ID "msg001"
#define TEST_RECV_PORT 20017
#define TEST_RECV_STATUS_RUNNING 1
#define TEST_RECV_STATUS_STOPPED 0
#define TEST_RECV_WAIT_LISTENER_START 1
#define TEST_SCREEN_WIDTH  900
#define TEST_SCREEN_HEIGHT  600
#define TEST_IMG_WIDTH 300
#define TEST_IMG_HEIGHT 200

// device info callback
std::mutex s_device_info_result_lock;
std::queue<bool> *s_device_info_result_q; 
// device disconnected callback
std::mutex s_device_disconnected_result_lock;
std::queue<bool> *s_device_disconnected_result_q;
// ctrl msg callback
std::mutex s_device_ctrl_msg_result_lock;
std::queue<bool> *s_device_ctrl_msg_result_q;
// frame img callback
std::mutex s_device_frame_img_result_lock;
std::queue<bool> *s_device_frame_img_result_q;
scrcpy_listener_t s_svr = NULL;


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

                SPDLOG_INFO("Testing ctrl socket");
                log_flush();
                register_all_events();
                // connect ctrl socket
                auto ctrl_socket_ok = connect_ctrl_socket([this](boost::shared_ptr<tcp::socket> conn){
                        uint8_t t_msg_data[] = {0x00, 0xFF, 0x00, 0x01};
                        try {
                            SPDLOG_DEBUG("Sending ctrl data to server");
                            scrcpy_device_send_ctrl_msg(this->listener, (char*)TEST_RECV_DEVICE_ID, (char*)TEST_MSG_ID, t_msg_data, 4);
                        } catch(boost::system::system_error& e) {
                            SPDLOG_ERROR("Failed to send ctrl msg to server: {}", e.what()); 
                        }
                        log_flush();
                }, true, 10, 1);
                SPDLOG_INFO("ctrl socket  test passed? {} ", ctrl_socket_ok ? "YES" : "NO");
                log_flush();
                assert(ctrl_socket_ok);
        });
        // step 03: start a receiver and wait for frame image callbacks
        std::queue<bool> *disconnected_callback_q = new std::queue<bool>();
        s_device_disconnected_result_q = disconnected_callback_q;
        do_test_template([this](){
                register_all_events();
                SPDLOG_INFO("Trying to send video data");
                log_flush();
                // wait the decocders to be ready
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                auto device_info_calback_ok = connect_video_socket([this](boost::shared_ptr<tcp::socket> conn){
                        this->send_video_bin(conn);
                }, 10);
                assert(device_info_calback_ok);
        });
        auto disconnected_result = wait_for_result((char *)"device disconnected callback", disconnected_callback_q, s_device_disconnected_result_lock);
        // check disconnected callback
        s_device_disconnected_result_q = NULL;
        delete disconnected_callback_q;
        assert(disconnected_result);
    }

private:
    scrcpy_listener_t listener = NULL;
    std::mutex m_svr_lock;
    std::string m_video_file_path;
    volatile int m_svr_status = TEST_RECV_STATUS_STOPPED;
    bool wait_for_result(char *label, std::queue<bool> *q, std::mutex& lock, int time_seconds = 10, int min_size = 1) {
        int sleep_ms = 100;
        int sleep_times = time_seconds * 1000 / sleep_ms;
        bool got_result = false;
        bool is_correct = false;
        while(sleep_times > 0) {
            sleep_times --;
            int item_size = 0;
            {
                std::lock_guard<std::mutex> locker(lock);
                item_size = (int) q->size();
                got_result = item_size >= min_size;
            }
            if (!got_result) {
                std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
                continue;
            }
            std::lock_guard<std::mutex> locker(lock);
            for(int i = 0; i < item_size; i++) {
                auto v = q->front();
                q->pop();
                if(i == 0) {
                    is_correct = v;
                }
                is_correct = v && is_correct;
            }
            break;
        }
        SPDLOG_INFO("{} got_result = {}, is_correct = {}", label, got_result ? "YES": "NO", is_correct ? "YES":"NO");
        log_flush();
        return is_correct;
    } 
    void send_video_bin(boost::shared_ptr<tcp::socket> conn, bool check_frame_img_callback = true, int wait_timeout = 10, int at_least_frame_count = 1) {
        std::queue<bool> *frame_img_callback_q = new std::queue<bool>();
        {
            std::lock_guard<std::mutex> lock(s_device_frame_img_result_lock);
            s_device_frame_img_result_q = frame_img_callback_q;
        }
        SPDLOG_INFO("Trying to send video data {} to sever", this->m_video_file_path);
        log_flush();
        FILE *fp;
        fopen_s(&fp, this->m_video_file_path.c_str(), "rb");
        if(!fp) {
            SPDLOG_ERROR("Failed to open video file {}", this->m_video_file_path);
            s_device_frame_img_result_q = NULL;
            delete frame_img_callback_q;
            return;
        }
        int buffer_size = 2 * 1024 * 1024;
        char* buffer = (char *)malloc(buffer_size * sizeof(char));
        int data_send_cf[] = {
            38, 65495, 27473, 31720,
            6641, 131148, 10021, 14491,
            10178, 12, 16755, 13491,
            10001, 17657, 60437, 31573
        };
        for(int i = 0; i < 12; i++) {
            int read_bytes = fread(buffer, sizeof(char), data_send_cf[i], fp);
            // send data
            if(read_bytes > 0) {
                try{
                    int sent_size = (int)conn->send(boost::asio::buffer(buffer, read_bytes));
                    SPDLOG_DEBUG("Sent {}/{} video data to server.", sent_size, read_bytes);
                    log_flush();
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                } catch(boost::system::system_error& e) {
                    SPDLOG_WARN("Failed to send {} bytes of video data: {}", read_bytes, e.what());
                }
            }
            if (read_bytes < buffer_size) {
                SPDLOG_INFO("Done reading {}", this->m_video_file_path);
                break;
            }
        }
        fclose(fp);
        fp = NULL;
        free(buffer);
        if (!check_frame_img_callback) {
            delete frame_img_callback_q;
            s_device_frame_img_result_q = NULL;
            return;
        }
        // could not done decode test yet
        auto result = wait_for_result((char *)"check frame_img_callback called times", frame_img_callback_q, s_device_frame_img_result_lock, 10);
        delete frame_img_callback_q;
        s_device_frame_img_result_q = NULL;
    }
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
        // update global
        s_svr = this->listener;
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
        auto ok = strcmp(token, (char*)TEST_RECV_TOKEN) == 0 
            && strcmp(device_id, (char*)TEST_RECV_DEVICE_ID) == 0 
            && img_data 
            && img_data_len > 0 
            && img_size.width == TEST_IMG_WIDTH 
            && img_size.height == TEST_IMG_HEIGHT 
            && screen_size.width == TEST_SCREEN_WIDTH 
            && screen_size.height == TEST_SCREEN_HEIGHT;
        SPDLOG_INFO("frame_img_callback is {}. token={} device_id={} "
                "img_data_len={} img_size.width={} img_size.height={} "
                "screen_size.width={} screen_size.height={}", 
                ok ? "correc" : "wrong", token, device_id,
                img_data_len, img_size.width, img_size.height,
                screen_size.width, screen_size.height);
        if(s_device_frame_img_result_q){
            std::lock_guard<std::mutex> lock(s_device_frame_img_result_lock);
        }else {
            SPDLOG_ERROR("Reference to s_device_frame_img_result_q was NULL.");
        }
    }
    static void device_info_callback(char *token, char *device_id, int screen_width, int screen_height) {
        auto ok = strcmp(token, (char *)TEST_RECV_TOKEN) == 0 && 
            strcmp(device_id, (char *)TEST_RECV_DEVICE_ID) == 0 &&
            screen_width == TEST_SCREEN_WIDTH && screen_height == TEST_SCREEN_HEIGHT;
        SPDLOG_INFO("device_info_callback is {}. Invoking with token={} device_id={} screen_width={} screen_height={}", 
                ok ? "correct":"wrong",
                token, device_id, screen_width, screen_height);
        if(s_device_info_result_q) {
            std::lock_guard<std::mutex> lock(s_device_info_result_lock);
            s_device_info_result_q->push(ok);
        }
        if(s_svr) {
            SPDLOG_INFO("Setting frame image size");
            scrcpy_set_image_size(s_svr, (char*)TEST_RECV_DEVICE_ID, TEST_IMG_WIDTH, TEST_IMG_HEIGHT);
        } else {
            SPDLOG_ERROR("Reference to scrcpy_recv was NULL.");
        }
    }
    static void ctrl_msg_callback(char *token, char *device_id, char *msg_id, int status, int data_len) {
        auto ok = strcmp(token, (char*)TEST_RECV_TOKEN) == 0 
            && strcmp(device_id, (char*)TEST_RECV_DEVICE_ID) == 0
            && strcmp(msg_id, (char*)TEST_MSG_ID) == 0 
            && status == data_len;
        SPDLOG_INFO("ctrl_msg_callback is {}. token={} device_id={} msg_id={} status={} data_len={}", ok ? "correct": "wrong",
                token, device_id, msg_id, status, data_len);
        if(s_device_ctrl_msg_result_q){
            std::lock_guard<std::mutex> lock(s_device_ctrl_msg_result_lock);
            s_device_ctrl_msg_result_q->push(ok);
        }else {
            SPDLOG_ERROR("s_device_ctrl_msg_result_q was NULL");
        }
    }

    static void client_disconnected(char *token, char *device_id, char *con_type) {
        auto ok = strcmp(token, (char *)TEST_RECV_TOKEN) == 0 
            && strcmp(device_id, (char *)TEST_RECV_DEVICE_ID) == 0
            && (
                    strcmp(con_type, (char *)TEST_SOCKET_TYPE_VIDEO) == 0
                    || strcmp(con_type, (char *)TEST_SOCKET_TYPE_CTRL) == 0 
                );
        SPDLOG_INFO("client_disconnected is {}. token={} device_id={} con_type ={}.", ok ? "correct":"wrong", token, device_id, con_type);
        if (s_device_disconnected_result_q) {
            std::lock_guard<std::mutex> lock(s_device_disconnected_result_lock);
            s_device_disconnected_result_q->push(ok);
        } else {
            SPDLOG_ERROR("s_device_disconnected_result_q was NULL");
        }
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
    bool connect_video_socket(std::function<void(boost::shared_ptr<tcp::socket>)> after_headers_sent, uint32_t wait_seconds) {
        std::queue<bool> *device_info_q = new std::queue<bool>();
        s_device_info_result_q = device_info_q;
        SPDLOG_INFO("Trying to connect a video socket to port {}", TEST_RECV_PORT);
        boost::shared_ptr<tcp::socket> socket_ptr;
        // connect a socket
        auto client = new test_tcp_client((char *)"127.0.0.1", (uint16_t) TEST_RECV_PORT, 
                [this, &socket_ptr](boost::shared_ptr<tcp::socket> conn){
                SPDLOG_INFO("Video socket connected to server {}", conn->remote_endpoint().port());
                std::lock_guard<std::mutex> lock(s_device_info_result_lock);
                // send device info header
                auto header_sent = this->send_socket_type(conn, (char *)TEST_SOCKET_TYPE_VIDEO) && this->send_device_info(conn);
                // send device info
                if (header_sent) {
                    SPDLOG_INFO("Invoking callback after both video socket header and device info sent");
                    log_flush();
                    socket_ptr = conn;
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
        auto result = wait_for_result((char *)"device info callback", device_info_q, s_device_info_result_lock);
        if(result && after_headers_sent) {
            after_headers_sent(socket_ptr);
        }
        delete device_info_q;
        s_device_info_result_q = NULL;
        client->disconnect();
        return result;
    }
    bool connect_ctrl_socket(std::function<void(boost::shared_ptr<tcp::socket>)> callback, int wait_timeout_seconds, 
            bool wait_for_ctrl_msg_callback_result, int callback_times=1) {
        std::queue<bool> *ctrl_msg_q = new std::queue<bool>();
        s_device_ctrl_msg_result_q = ctrl_msg_q;
        SPDLOG_INFO("Trying to connect a ctrl socket to server");
        log_flush();

        auto client = new test_tcp_client((char*)"127.0.0.1", (uint16_t)TEST_RECV_PORT, 
                [callback, this](boost::shared_ptr<tcp::socket> conn){
                    SPDLOG_INFO("Connected a ctrl socket to server {}", TEST_RECV_PORT);
                   this->send_socket_type(conn, (char *) TEST_SOCKET_TYPE_CTRL); 
                   if(callback) {
                        callback(conn);
                   }
                });
        auto connected = client->connect() == 0;
        SPDLOG_INFO("Ctrl socket connected ok ? {}", connected ? "YES" : "NO");
        log_flush();
        if(!connected) {
            delete ctrl_msg_q;
            s_device_ctrl_msg_result_q = NULL;
            delete client;
        }
        if (!wait_for_ctrl_msg_callback_result) {
            delete ctrl_msg_q;
            s_device_ctrl_msg_result_q = NULL;
            client->disconnect();
            return true;
        }
        SPDLOG_INFO("Now waitting ctrl msg callbck for {} seconds", wait_timeout_seconds);
        bool result = wait_for_result((char *)"ctrl msg callback", ctrl_msg_q, s_device_ctrl_msg_result_lock);

        client->disconnect();
        delete ctrl_msg_q;
        s_device_ctrl_msg_result_q = NULL;
        return result;
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

    // wait for main thread to shutdown
    std::this_thread::sleep_for(std::chrono::seconds(2));
    delete tester;

    SPDLOG_INFO("Finished running test_scrcpy_support");
    log_flush();
    return 0;
}
