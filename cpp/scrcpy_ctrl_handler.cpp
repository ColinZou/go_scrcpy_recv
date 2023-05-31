#include "scrcpy_ctrl_handler.h"
#include <stdint.h>
#include <Windows.h>
#include <WinSock2.h>
#include <functional>
#include <winbase.h>
#include "utils.h"
#include "logging.h"

#define CTRL_LOGGER "CTRL:: "

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
        for(auto iter = this->outgoing_queue->begin(); iter != this->outgoing_queue->end(); iter ++) {
            auto item = *iter;
            this->outgoing_queue->erase(iter);
            delete item->data;
            delete item->msg_id;
            delete item;
        }
        delete this->outgoing_queue;
        this->outgoing_queue = nullptr;
    }
    if (this->outgoing_trash) {
        for(auto iter = this->outgoing_trash->begin(); iter != this->outgoing_trash->end(); iter ++) {
            if (this->outgoing_trash->empty()) {
                break;
            }
            auto item = *iter;
            this->outgoing_trash->erase(iter);
            delete item->msg->data;
            delete item->msg->msg_id;
            delete item->msg;
            delete item;
        }
        delete this->outgoing_trash;
        this->outgoing_trash = nullptr;
    }
}
void scrcpy_ctrl_socket_handler::stop() {
    std::lock_guard<std::mutex> lock(this->stat_lock);
    this->keep_running = false;
}
void scrcpy_ctrl_socket_handler::send_msg(char *msg_id, uint8_t *data, int data_len) {
    debug_logf(CTRL_LOGGER "Acquiring a lock for sending message msg_id=%s for device %s\n", msg_id, this->device_id->c_str());
    print_bytes((char *)CTRL_LOGGER, (char *)data, data_len);
    std::lock_guard<std::mutex> lock(this->outgoing_queue_lock);
    debug_logf(CTRL_LOGGER "Lock granted for sending message msg_id=%s for device %s\n", msg_id, this->device_id->c_str());
    auto msg = new scrcpy_ctrl_msg();
    char* msg_id_copy = (char*)malloc(sizeof(char) * strlen(msg_id) + 1);
    char* data_copy = (char*)malloc(sizeof(char) * data_len);
    array_copy_to(msg_id, msg_id_copy, 0, strlen(msg_id));
    array_copy_to((char*)data, data_copy, 0, data_len);
    msg->msg_id = msg_id_copy;
    msg->data = data_copy;
    msg->length = data_len;
    this->outgoing_queue->push_front(msg);
}
void scrcpy_ctrl_socket_handler::cleanup_trash() {
    int cleaned_size = 0;
    for(auto iter = this->outgoing_trash->begin(); iter != this->outgoing_trash->end(); iter++) {
        if (this->outgoing_trash->empty()) {
            break;
        }
        auto item = *iter;
        // keep the item for a while
        if (item->counter < 100) {
            item->counter ++;
            continue;
        }
        this->outgoing_trash->erase(iter);
        delete item->msg->msg_id;
        delete item->msg->data;
        delete item->msg;
        delete item;
        cleaned_size ++;
    }
    if (cleaned_size > 0) {
        debug_logf(CTRL_LOGGER "Released %d trashed ctrl msg\n", cleaned_size);
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
            debug_logf(CTRL_LOGGER "%d messages pending for device %s \n", queue_size, this->device_id->c_str());
            for (auto item = this->outgoing_queue->rbegin(); item != this->outgoing_queue->rend(); ++item) {
                auto msg = *item;
                // remove it
                this->outgoing_queue->erase(--(item.base()));
                //send it
                int status = send(this->client_socket, msg->data, msg->length, 0);
                if (status == SOCKET_ERROR) {
                    debug_logf(CTRL_LOGGER "Failed to send msg_id=%s %d bytes of ctrl msg to device %s\n", msg->msg_id, msg->length, this->device_id->c_str());
                } else if(status == msg->length) {
                    debug_logf(CTRL_LOGGER "Sent msg_id=%s to device %s with %d bytes\n", msg->msg_id, this->device_id->c_str(), msg->length);
                    print_bytes((char *)CTRL_LOGGER, (char *)msg->data, msg->length);
                } else {
                    debug_logf(CTRL_LOGGER "Unexpected status %d when trying to send msg_id=%s with %s bytes data to device %s\n", status, msg->msg_id, msg->length, this->device_id->c_str());
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
