#ifndef SCRCPY_CTRL_HANDLER
#define SCRCPY_CTRL_HANDLER

#include <string>
#include <mutex>
#include "boost/asio/ip/tcp.hpp"
#include <queue>
#include <functional>

using boost::asio::ip::tcp;

typedef struct scrcpy_ctrl_msg {
    char *data;
    int length;
    char *msg_id;
} scrcpy_ctrl_msg;

typedef struct scrcpy_ctrl_msg_trashed {
    struct scrcpy_ctrl_msg* msg;
    int counter = 0;
} scrcpy_ctrl_msg_trashed;

/**
 * scrcpy_ctrl_socket_handler
 * let it clean itself after call stop if you want a clear shutdown
 */
class scrcpy_ctrl_socket_handler 
{
    public:
        scrcpy_ctrl_socket_handler(std::string *dev_id, boost::shared_ptr<tcp::socket> socket);
        ~scrcpy_ctrl_socket_handler();
        void stop();
        int run(std::function<void(std::string, std::string, int, int)> callback);
        void send_msg(char* msg_id, uint8_t *data, int data_len);
    private:
        std::string *device_id = NULL;
        boost::shared_ptr<tcp::socket> client_socket;
        std::mutex stat_lock;
        std::mutex outgoing_queue_lock;
        std::queue<scrcpy_ctrl_msg*> *outgoing_queue;
        std::queue<scrcpy_ctrl_msg_trashed*> *outgoing_trash;
        bool keep_running = true;

        void cleanup_trash();
};
#endif //!SCRCPY_CTRL_HANDLER
