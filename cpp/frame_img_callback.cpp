#include "frame_img_callback.h"
#include "utils.h"
#include <thread>
#include "logging.h"
#define FIP_LOGGER "FIP::"
#define MAX_IMG_BUFFER_SIZE 1 * 1024 * 1024

int frame_img_processor::callback_thread(device_frame_img_callback *callback_item) {
	spdlog::debug(FIP_LOGGER "CB:: Running thread for frame callback of device {}", callback_item->device_id);
	BOOL wait_for_next = FALSE;
	while (true) {
		if (wait_for_next) {
			wait_for_next = FALSE;
			std::this_thread::sleep_for(std::chrono::nanoseconds(50));
            continue;
		}
        std::deque<frame_img_callback_params*> *frames = callback_item->frames;
        // gaint lock for whole callback_item
		std::lock_guard<std::mutex> wait_lock(callback_item->lock);
		if (callback_item->stop > 0) {
			spdlog::debug(FIP_LOGGER "CB:: stopping callback for {}", callback_item->device_id);
			break;
		}
		frame_img_callback_params* allocated_frame = NULL;
		if (!frames->empty()) {
			//grab a frame from front
			for(auto item = frames->rbegin(); item != frames->rend(); ++item) {
				frame_img_callback_params* cur_frame = *item;
				if (cur_frame->status == CALLBACK_PARAM_PENDING) {
					allocated_frame = cur_frame;
                    // remove it from deque for now
                    frames->erase(--(item.base()));
                    break;
				} 
			}
		}
		if (NULL == allocated_frame) {
			wait_for_next = TRUE;
			continue;
		}
        // tiny lock for the allocated_frame
		std::lock_guard<std::mutex> allocated_frame_guard{allocated_frame->lock};
		allocated_frame->status = CALLBACK_PARAM_SENDING;
        // put it back to queue
        if (callback_item->handler_count <= 0) {
            allocated_frame->status = CALLBACK_PARAM_SENT;
            frames->push_back(allocated_frame);
            continue;
        }
		spdlog::debug(FIP_LOGGER "CB:: Invoking frame callback device={} frame data size={} pointer {} total handlers = {}", allocated_frame->device_id.c_str(), 
			allocated_frame->frame_data_size, (uintptr_t) allocated_frame, callback_item->handler_count);
		// call the handlers
		for (int i = 0; i < callback_item->handler_count; i++) {
			frame_callback_handler callback = callback_item->handlers[i];
            spdlog::debug(FIP_LOGGER "CB:: Invoking callback handler {} for device_id={} callback param is {}", (uintptr_t)callback, 
                    callback_item->device_id, (uintptr_t) allocated_frame);
			scrcpy_rect img_size = scrcpy_rect{ allocated_frame->w, allocated_frame->h};
			scrcpy_rect screen_size = scrcpy_rect{ allocated_frame->raw_w, allocated_frame->raw_h };
			callback(callback_item->token, callback_item->device_id, 
                    allocated_frame->frame_data, allocated_frame->frame_data_size,
                    img_size, screen_size);
		}
        spdlog::debug(FIP_LOGGER "CB:: Setting status to CALLBACK_PARAM_SENT for {}", (uintptr_t) allocated_frame);
        allocated_frame->status = CALLBACK_PARAM_SENT;
        spdlog::debug(FIP_LOGGER "CB:: Put the frame {} back to {}", (uintptr_t) allocated_frame, (uintptr_t) frames);
		frames->push_front(allocated_frame);
	}
	spdlog::debug(FIP_LOGGER "CB:: Stopped thread for frame callback of device {}", callback_item->device_id);
    release_device_img_callback(callback_item);
	return 0;
}
void frame_img_processor::release_device_img_callback(device_frame_img_callback* callback_item) {
    std::lock_guard<std::mutex> lock(callback_item->lock);
    if (callback_item->device_id) {
        free(callback_item->device_id);
        callback_item->device_id = nullptr;
    }
    if (callback_item->handlers) {
        free(callback_item->handlers);
        callback_item->handlers = nullptr;
    }
    if  (callback_item->token) {
        free(callback_item->token);
        callback_item->token = nullptr;
    }
    if (callback_item->thread_handle){
        callback_item->thread_handle = nullptr;
    }
    if(callback_item->frames) {
        std::deque<frame_img_callback_params*> *frames = callback_item->frames;
        for(auto first = frames->begin(); first != frames->end(); first ++) {
            auto item = *first;
            std::lock_guard<std::mutex> lock{ item->lock };
            if (NULL != item->frame_data) {
                free(item->frame_data);
                item->frame_data = NULL;
            }
        }
        delete callback_item->frames;
        callback_item->frames = nullptr;
    }
    delete callback_item;
}
frame_img_processor::frame_img_processor() :
    registry(new std::map<std::string, device_frame_img_callback*>()){  }
    int frame_img_processor::start_callback_thread(char* device_id, device_frame_img_callback* handler_container) {
        // start thread
        handler_container->stop = 0;
        std::thread callback_thread(&frame_img_processor::callback_thread, this, handler_container);
        callback_thread.detach();
        handler_container->thread_handle = callback_thread.native_handle();
        return 0;
    }
void frame_img_processor::add(char *device_id, frame_callback_handler callback, char *token) {
    // lock global
    std::lock_guard<std::mutex> guard{ this->lock };
    auto entry = this->registry->find(std::string(device_id));
    auto end_item = this->registry->end();
    spdlog::debug(FIP_LOGGER "{} Trying to add frame image callback handler {} for device {} existing? {}",(uintptr_t)this, (uintptr_t)callback, 
            device_id, end_item == entry ? "no":"yes");
    if (end_item == entry) {
        spdlog::debug(FIP_LOGGER "Need to create a new callback container for device {}", device_id);
        frame_callback_handler* handlers = (frame_callback_handler*)malloc(sizeof(frame_callback_handler) * PRE_ALLOC_CALLBASCK_SIZE);
        if (!handlers) {
            spdlog::debug(FIP_LOGGER "No enough memory for storing callbacks");
            return;
        }
        handlers[0] = callback;
        device_frame_img_callback* callback_item = new device_frame_img_callback();
        if (!callback_item) {
            spdlog::debug(FIP_LOGGER "No enough memory for storing callback container");
            return;
        }
        char *device_id_cpy = (char*)malloc(sizeof(char) * strlen(device_id) + 1);
        char *token_cpy = (char*)malloc(sizeof(char) * strlen(token) + 1);
        array_copy_to(device_id, device_id_cpy, 0, strlen(device_id) + 1);
        array_copy_to(token, token_cpy, 0, strlen(token) + 1);
        callback_item->device_id = device_id_cpy;
        callback_item->handler_count = 1;
        callback_item->token = token_cpy;
        callback_item->handlers = handlers;
        callback_item->frames = new std::deque<frame_img_callback_params*>();
        auto existing = this->registry->emplace(std::string(device_id_cpy), callback_item);
        auto callback_added = existing.second;
        spdlog::debug(FIP_LOGGER "Creating new frame image callback handler holder for device={} ok ? {} devices registered {}", 
                device_id_cpy, callback_added ? "YES":"NO", this->registry->size());
        if (callback_added) {
            // if failed to start thread
            this->start_callback_thread(device_id_cpy, callback_item);
        } else {
            // just in case
            spdlog::debug(FIP_LOGGER "It should not be happened, the device {} already had callbacks", device_id_cpy);
            release_device_img_callback(callback_item);
        }
    } else {
        device_frame_img_callback* handler_container = entry->second;
        spdlog::debug("Trying to add callback {} to exsiting callbacks({}) for device {}", (uintptr_t)callback,
                handler_container->handler_count, device_id);
        //lock the callback item
        std::lock_guard<std::mutex> lock { handler_container->lock };
        int old_count = handler_container->handler_count;
        if (old_count > PRE_ALLOC_CALLBASCK_SIZE && old_count  >= handler_container->allocated_handler_space) {
            int new_count = old_count + 1;
            int multiple = new_count / PRE_ALLOC_CALLBASCK_SIZE;
            int amount = new_count % PRE_ALLOC_CALLBASCK_SIZE > 1 ? multiple + 1 : multiple;
            spdlog::debug(FIP_LOGGER "Re-alloc callback of device {} from {} to {}", handler_container->device_id, old_count, amount);
            // save the new amount
            handler_container->allocated_handler_space = amount;
            frame_callback_handler* new_handlers = (frame_callback_handler*)malloc(sizeof(frame_callback_handler) * PRE_ALLOC_CALLBASCK_SIZE * amount);
            if (!new_handlers) {
                spdlog::debug(FIP_LOGGER "No enough ram when trying to allocate {} frame_callback_handlers", amount);
                return;
            }
            for (int i = 0; i < old_count; i++) {
                new_handlers[i] = handler_container->handlers[i];
            }
            // release old handlers
            free(handler_container->handlers);
            handler_container->handlers = new_handlers;
        }
        handler_container->handlers[old_count] = callback;
        handler_container->handler_count++;
    }
}
void frame_img_processor::del(char* device_id, frame_callback_handler callback) {
    // global lock
    std::lock_guard<std::mutex> guard{ this->lock };
    auto entry = this->registry->find(std::string(device_id));
    spdlog::debug(FIP_LOGGER "Trying to remove frame callback handler {} for device {}", (uintptr_t)callback, device_id);
    if (entry == this->registry->end()) {
        spdlog::debug(FIP_LOGGER "Device {} was not register of callback {}", device_id, (uintptr_t) callback);
        return;
    }
    device_frame_img_callback* handler_container = entry->second;
    // lock for the container
    std::lock_guard<std::mutex> lock{ handler_container->lock };
    frame_callback_handler* handlers = handler_container->handlers;
    int start_index = 0;
    for (int i = 0; i < handler_container->handler_count; i++) {
        if (handlers[i] == callback) {
            start_index = i;
            //clear current item
            handlers[i] = NULL;
            handler_container->handler_count--;
            spdlog::debug(FIP_LOGGER "Removed frame callback handler {} for device {}", (uintptr_t)callback, device_id);
            continue;
        }
        if (i > start_index) {
            // move current item to previous slot
            handlers[i - 1] = handlers[i];
            //clear current item
            handlers[i] = NULL;
        }
    }
    if (handler_container->handler_count == 0) {
        handler_container->stop = 1;
        // remove from register
        this->registry->erase(entry);
        spdlog::debug(FIP_LOGGER "Also remove dict entry for device {}", device_id);
    }
}
frame_img_processor::~frame_img_processor() {
    std::lock_guard<std::mutex> lock(this->lock);
    for(auto first = this->registry->begin(); first != this->registry->end(); first++) {
        //remove all handles
        auto key = first->first;
        this->clean_device_img_callback_state(std::string(key), false);
    }
    // remove all items
    this->registry->clear();
}
void frame_img_processor::invoke(char *token, char* device_id, uint8_t* frame_data, uint32_t frame_data_size, int w, int h, int raw_w, int raw_h) {
    auto entry = this->registry->find(std::string(device_id));
    if (entry == this->registry->end()) {
        return;
    }
    device_frame_img_callback* handler_container = entry->second;
    // callback item lock
    std::lock_guard<std::mutex> lock{ handler_container->lock };
    if (handler_container->handler_count == 0) {
        return;
    }
    int buffed_frames = handler_container->allocated_frames;
    spdlog::debug(FIP_LOGGER "PREP:: Trying to add param for device {}, lock acquired, already had {} allocted frames. data size is {}", 
            device_id, buffed_frames, frame_data_size);
    frame_img_callback_params* params = nullptr;
    if (buffed_frames >= MAX_PENDING_FRAMES ) {
        auto frames = handler_container->frames;
        // get a frame from back 
        for (auto first = frames->begin(); first != frames->end(); first++) {
            auto temp_frame = *first;
            if (temp_frame->status < CALLBACK_PARAM_PENDING) {
                params = temp_frame;
                // remove it from the deque
                frames->erase(first);
                break;
            } 
        }
    }
    if(nullptr == params) {
        params = new frame_img_callback_params();
        if (!params) {
            spdlog::debug(FIP_LOGGER "PREP:: No enough memory for initing CallbackParams");
            return;
        }
        auto buffer_size = calc_buffer_size(frame_data_size, MAX_IMG_BUFFER_SIZE);
        spdlog::debug(FIP_LOGGER "PREP:: Allocating {} bytes for fram cache", buffer_size);
        params->frame_data = (uint8_t*)malloc(buffer_size);
        params->buffer_size = buffer_size;
        spdlog::debug(FIP_LOGGER "PREP:: Allocated {} bytes for fram cache", buffer_size);
        if (!params->frame_data) {
            spdlog::debug(FIP_LOGGER "PREP:: No enough memory for initing frame_data");
            delete params;
            return;
        }
        handler_container->allocated_frames++;
    }
    if (params == nullptr) {
        spdlog::debug(FIP_LOGGER "PREP:: FATAL: Could not allocate a param for sending callback");
        return;
    }
    spdlog::debug(FIP_LOGGER "PREP:: Current callback param is {}", (uintptr_t)params);
    // realloc ram
    if (params->buffer_size < frame_data_size) {
        free(params->frame_data);
        auto buffer_size = calc_buffer_size(frame_data_size, MAX_IMG_BUFFER_SIZE);
        spdlog::debug(FIP_LOGGER "PREP:: Re-allocating {} bytes for fram cache", buffer_size);
        params->frame_data = (uint8_t*)malloc(buffer_size);
        params->buffer_size = buffer_size;
        spdlog::debug(FIP_LOGGER "PREP:: Re-allocated {} bytes for fram cache", buffer_size);
    }
    // the callback param lock
    std::lock_guard<std::mutex> param_lock{ params->lock };
    params->token = handler_container->token;
    params->status = CALLBACK_PARAM_PENDING;
    params->device_id = std::string(handler_container->device_id);
    array_copy_to((char*)frame_data, (char*)params->frame_data, 0, frame_data_size);
    params->frame_data_size = frame_data_size;
    params->w = w;
    params->h = h;
    params->raw_w = raw_w;
    params->raw_h = raw_h;
    // push the frame to back
    handler_container->frames->push_back(params);
    spdlog::debug(FIP_LOGGER "PREP:: Added frame {} for device {} to callback queue, data size {}, queue size: {}", (uintptr_t)params, 
            handler_container->device_id, 
            frame_data_size, handler_container->frames->size());
}

void frame_img_processor::clean_device_img_callback_state(std::string key, bool remove_from_registry) {
    auto entry = this->registry->find(key);
    if (entry == this->registry->end()) {
        spdlog::debug(FIP_LOGGER "{} Could not find callback container for device {} from {} keys ", 
                uintptr_t(this), 
                key.c_str(), this->registry->size());
        for(auto first = this->registry->begin(); first != this->registry->end(); first++) {
            spdlog::debug(FIP_LOGGER "Found key {}", first->first.c_str());
        }
        return;
    }
    device_frame_img_callback* handler_container = entry->second;
    // must lock first
    std::lock_guard<std::mutex> lock(handler_container->lock);
    spdlog::debug(FIP_LOGGER "Marking callback container {} to shutdown for device {}",(uintptr_t)handler_container, handler_container->device_id);
    // mark as stop
    handler_container->stop = 1;
    if (remove_from_registry) {
        this->registry->erase(entry);
    }
}
void frame_img_processor::del_all(char* device_id) {
    spdlog::debug(FIP_LOGGER "{} Trying to remove all frame image callbacks for {}", (uintptr_t) this, device_id);
    clean_device_img_callback_state(std::string(device_id), true);
}
int frame_img_processor::calc_buffer_size(int frame_data_size, int current_buffer_size) {
    if (frame_data_size > current_buffer_size) {
        int half = MAX_IMG_BUFFER_SIZE / 2;
        auto multiple = frame_data_size * 1.0 / (half * 1.0);
        auto value = (int)ceil(multiple);
        return half * value;
    }
    return current_buffer_size;
}
