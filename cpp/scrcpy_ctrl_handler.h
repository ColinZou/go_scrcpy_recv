#ifndef SCRCPY_CTRL_HANDLER
#define SCRCPY_CTRL_HANDLER

#include <string>
#include <mutex>
#include <WinSock2.h>
#include <deque>
#include <functional>

typedef struct scrcpy_ctrl_msg {
    char *data;
    int length;
    char *msg_id;
} scrcpy_ctrl_msg;
class scrcpy_ctrl_socket_handler 
{
    public:
        scrcpy_ctrl_socket_handler(std::string *dev_id, SOCKET socket);
        ~scrcpy_ctrl_socket_handler();
        void stop();
        int run(std::function<void(std::string, std::string, int, int)> callback);
        void send_msg(char* msg_id, uint8_t *data, int data_len);
    private:
        std::string *device_id = nullptr;
        SOCKET client_socket = INVALID_SOCKET;
        std::mutex stat_lock;
        std::mutex outgoing_queue_lock;
        std::deque<scrcpy_ctrl_msg*> *outgoing_queue;
        bool keep_running = true;
};
#endif //!SCRCPY_CTRL_HANDLER
