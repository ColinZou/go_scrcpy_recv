#include "scrcpy_ctrl_handler.h"
#include "fmt/core.h"
#include <stdint.h>
#include <Windows.h>
#include <WinSock2.h>
#include <functional>
#include <winbase.h>
#include "utils.h"

scrcpy_ctrl_socket_handler::scrcpy_ctrl_socket_handler(std::string *dev_id, SOCKET socket): device_id(dev_id), 
    client_socket(socket), 
    outgoing_queue(new std::deque<scrcpy_ctrl_msg*>()),
    outgoing_trash(new std::deque<scrcpy_ctrl_msg_trashed*>()){
    auto dev_id_cloned = new std::string(dev_id->c_str());
    this->device_id = dev_id_cloned;
}
scrcpy_ctrl_socket_handler::~scrcpy_ctrl_socket_handler() {
    if(this->device_id) {
        delete this->device_id;
        this->device_id = nullptr;
    }
    if(this->outgoing_queue) {
        std::lock_guard<std::mutex> lock(this->outgoing_queue_lock);
        auto size = (uint64_t)this->outgoing_queue->size();
        for(int i = 0; i < size; i++) {
            auto item = this->outgoing_queue->front();
            this->outgoing_queue->pop_front();
            delete item->data;
            delete item->msg_id;
            delete item;
        }
        this->outgoing_queue->clear();
        delete this->outgoing_queue;
    }
    if (this->outgoing_trash) {
        auto size = this->outgoing_trash->size();
        for(int i = 0; i < size; i++) {
            if (this->outgoing_trash->empty()) {
                break;
            }
            auto item = this->outgoing_trash->back();
            this->outgoing_trash->pop_back();
            delete item->msg->data;
            delete item->msg->msg_id;
            delete item->msg;
            delete item;
        }
    }
}
void scrcpy_ctrl_socket_handler::stop() {
    std::lock_guard<std::mutex> lock(this->stat_lock);
    this->keep_running = false;
}
void scrcpy_ctrl_socket_handler::send_msg(char *msg_id, uint8_t *data, int data_len) {
    fmt::print("Acquiring a lock for sending message msg_id={} for device {}\n", msg_id, this->device_id->c_str());
    std::lock_guard<std::mutex> lock(this->outgoing_queue_lock);
    fmt::print("Lock granted for sending message msg_id={} for device {}\n", msg_id, this->device_id->c_str());
    auto msg = new scrcpy_ctrl_msg();
    char* msg_id_copy = (char*)malloc(sizeof(char) * strlen(msg_id) + 1);
    char* data_copy = (char*)malloc(sizeof(char) * data_len);
    array_copy_to(msg_id, msg_id_copy, 0, strlen(msg_id));
    array_copy_to(data_copy, (char*)data, 0, data_len);
    msg->msg_id = msg_id_copy;
    msg->data = data_copy;
    msg->length = data_len;
    this->outgoing_queue->push_front(msg);
}
void scrcpy_ctrl_socket_handler::cleanup_trash() {
    auto size = this->outgoing_trash->size();
    int cleaned_size = 0;
    for(int i = 0; i < size; i++) {
        if (this->outgoing_trash->empty()) {
            break;
        }
        auto item = this->outgoing_trash->back();
        // keep the item for a while
        if (item->counter < 100) {
            item->counter ++;
            continue;
        }
        this->outgoing_trash->pop_back();
        delete item->msg->msg_id;
        delete item->msg->data;
        delete item->msg;
        delete item;
        cleaned_size ++;
    }
    if (cleaned_size > 0) {
        fmt::print("Released {} trashed ctrl msg\n", cleaned_size);
    }
}
int scrcpy_ctrl_socket_handler::run(std::function<void(std::string, std::string, int, int)> callback) {
    int result = 0;
    while(true) {
        {
            std::lock_guard<std::mutex> lock(this->stat_lock);
            if (!this->keep_running) {
                break;
            }
        }
        uint64_t queue_size = 0;
        {
            std::lock_guard<std::mutex> lock(this->outgoing_queue_lock);
            queue_size = (uint64_t)this->outgoing_queue->size();
        }
        cleanup_trash();
        if (queue_size<= 0) {
            Sleep(rand() % 10);
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(this->outgoing_queue_lock);
            fmt::print("{} messages pending for device {} \n", queue_size, this->device_id->c_str());
            for (int i = 0; i < queue_size; i++) {
                auto msg = this->outgoing_queue->back();
                this->outgoing_queue->pop_back();
                //send it
                int status = send(this->client_socket, msg->data, msg->length, 0);
                if (status == SOCKET_ERROR) {
                    fmt::print("Failed to send msg_id={} {} bytes of ctrl msg to device {}\n", msg->msg_id, msg->length, this->device_id->c_str());
                } else if(status == msg->length) {
                    fmt::print("Sent msg_id={} to device {} with {} bytes\n", msg->msg_id, msg->length, this->device_id->c_str());
                } else {
                    fmt::print("Unexpected status {} when trying to send msg_id={} with {} bytes data to device {}\n", status, msg->msg_id, msg->length, this->device_id->c_str());
                }
                if(NULL != callback) {
                    callback(std::string(this->device_id->c_str()), std::string(msg->msg_id), status, msg->length);
                }
                // save to trash 
                auto trash = new scrcpy_ctrl_msg_trashed();
                trash->msg = msg;
                this->outgoing_trash->push_front(trash);
            }
        }
    }
    return result;
}
