#include "scrcpy_ctrl_handler.h"
#include <stdint.h>
#include <functional>
#include <winbase.h>
#include "utils.h"
#include "logging.h"

scrcpy_ctrl_socket_handler::scrcpy_ctrl_socket_handler(std::string *dev_id, boost::shared_ptr<tcp::socket> socket): device_id(dev_id), 
    client_socket(socket), 
    outgoing_queue(new std::queue<scrcpy_ctrl_msg*>()),
    outgoing_trash(new std::queue<scrcpy_ctrl_msg_trashed*>()){
        auto dev_id_cloned = new std::string(dev_id->c_str());
        this->device_id = dev_id_cloned;
    }
scrcpy_ctrl_socket_handler::~scrcpy_ctrl_socket_handler() {
    if(this->outgoing_queue) {
        std::lock_guard<std::mutex> lock(this->outgoing_queue_lock);
        SPDLOG_INFO("Cleaning outgoing ctrl msg queue for {}", this->device_id->c_str());
        while(!this->outgoing_queue->empty()) {
            auto item = this->outgoing_queue->front();
            this->outgoing_queue->pop();
            delete item->data;
            delete item->msg_id;
            delete item;
        }
        delete this->outgoing_queue;
        this->outgoing_queue = NULL;
    }
    if (this->outgoing_trash) {
        std::lock_guard<std::mutex> lock(this->outgoing_queue_lock);
        SPDLOG_INFO("Cleaning outgoing msg trash queue for {}", this->device_id->c_str());
        while(!this->outgoing_trash->empty()) {
            auto item = this->outgoing_trash->front();
            this->outgoing_trash->pop();
            delete item->msg->data;
            delete item->msg->msg_id;
            delete item->msg;
            delete item;
        }
        delete this->outgoing_trash;
        this->outgoing_trash = NULL;
    }
    if(this->device_id) {
        delete this->device_id;
        this->device_id = NULL;
    }
}
void scrcpy_ctrl_socket_handler::stop() {
    std::lock_guard<std::mutex> lock(this->stat_lock);
    this->keep_running = false;
}
void scrcpy_ctrl_socket_handler::send_msg(char *msg_id, uint8_t *data, int data_len) {
    SPDLOG_DEBUG("Acquiring a lock for sending message msg_id={} for device {}", msg_id, this->device_id->c_str());
    std::lock_guard<std::mutex> lock(this->outgoing_queue_lock);
    SPDLOG_DEBUG("Lock granted for sending message msg_id={} for device {}", msg_id, this->device_id->c_str());
    log_flush();

    // create a copy
    auto msg_id_len = (int)strlen(msg_id) + 1;
    char* msg_id_copy = (char*)malloc(sizeof(char) * msg_id_len);
    char* data_copy = (char*)malloc(sizeof(char) * data_len);
    array_copy_to(msg_id, msg_id_copy, 0, msg_id_len);
    array_copy_to((char*)data, data_copy, 0, data_len);

    SPDLOG_DEBUG("Preparing new message for msg_id={}", msg_id_copy);
    auto msg = new scrcpy_ctrl_msg();
    msg->msg_id = msg_id_copy;
    msg->data = data_copy;
    msg->length = data_len;
    SPDLOG_DEBUG("Pusing new message to queue {} size is {}", (uintptr_t)this->outgoing_queue, this->outgoing_queue->size());
    log_flush();
    this->outgoing_queue->push(msg);
}
void scrcpy_ctrl_socket_handler::cleanup_trash() {
    int cleaned_size = 0;
    int total = (int)this->outgoing_trash->size();
    while(!this->outgoing_trash->empty() && total > 0) {
        auto item = this->outgoing_trash->front();
        this->outgoing_trash->pop();
        total --;
        // keep the item for a while
        if (item->counter < 100) {
            item->counter ++;
            this->outgoing_trash->push(item);
            continue;
        }
        delete item->msg->msg_id;
        delete item->msg->data;
        delete item->msg;
        delete item;
        cleaned_size ++;
    }
    if (cleaned_size > 0) {
        SPDLOG_DEBUG("Released {} trashed ctrl msg", cleaned_size);
    }
}
int scrcpy_ctrl_socket_handler::run(std::function<void(std::string, std::string, int, int)> callback) {
    int result = 0;
    while(true) {
        {
            std::lock_guard<std::mutex> lock(this->stat_lock);
            if (!this->keep_running) {
                SPDLOG_WARN("Will stop running loop for {}'s ctrl socket", this->device_id->c_str());
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
            Sleep(rand() % 10 + 5);
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(this->outgoing_queue_lock);
            SPDLOG_DEBUG("{} messages pending for device {} ", queue_size, this->device_id->c_str());
            auto q = this->outgoing_queue;
            while(!q->empty()) {
                auto msg = q->front();
                // remove it
                q->pop();
                //send it
                int status = 0;
                try {
                    status = (int)this->client_socket->send(boost::asio::buffer(msg->data, msg->length));
                } catch(boost::system::system_error& e) {
                    SPDLOG_ERROR("Failed to send msg id {}: {}", msg->msg_id, e.what());
                    status = -1;
                }
                if(status == msg->length) {
                    // do noting
                } else {
                    SPDLOG_DEBUG("Unexpected status {} when trying to send msg_id={} with {} bytes data to device {}", status, msg->msg_id, msg->length, this->device_id->c_str());
                }
                if(NULL != callback) {
                    callback(std::string(this->device_id->c_str()), std::string(msg->msg_id), status, msg->length);
                }
                // save to trash 
                auto trash = new scrcpy_ctrl_msg_trashed();
                trash->msg = msg;
                this->outgoing_trash->push(trash);
            }
        }
    }
    SPDLOG_INFO("Ctrl message sender loop end for {}", this->device_id->c_str());
    log_flush();
    delete this;
    return result;
}
