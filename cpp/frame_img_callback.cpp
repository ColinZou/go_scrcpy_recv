#include "frame_img_callback.h"
#include "utils.h"
#include <thread>
#include "logging.h"
#define FIP_LOGGER "FIP::"
#define MAX_IMG_BUFFER_SIZE 1 * 1024 * 1024

int frame_img_processor::callback_thread(device_frame_img_callback *callback_item) {
	SPDLOG_DEBUG(FIP_LOGGER "CB:: Running thread for frame callback of device {}", callback_item->device_id);
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
			debug_logf(FIP_LOGGER "CB:: stopping callback for %s\n", callback_item->device_id);
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
		debug_logf(FIP_LOGGER "CB:: Invoking frame callback device=%s frame data size=%d pointer %p total handlers = %d\n", allocated_frame->device_id.c_str(), 
			allocated_frame->frame_data_size, (uintptr_t) allocated_frame, callback_item->handler_count);
		// call the handlers
		for (int i = 0; i < callback_item->handler_count; i++) {
			frame_callback_handler callback = callback_item->handlers[i];
            debug_logf(FIP_LOGGER "CB:: Invoking callback handler %p for device_id=%s callback param is %p\n", (uintptr_t)callback, 
                    callback_item->device_id, (uintptr_t) allocated_frame);
			scrcpy_rect img_size = scrcpy_rect{ allocated_frame->w, allocated_frame->h};
			scrcpy_rect screen_size = scrcpy_rect{ allocated_frame->raw_w, allocated_frame->raw_h };
			callback(callback_item->token, callback_item->device_id, 
                    allocated_frame->frame_data, allocated_frame->frame_data_size,
                    img_size, screen_size);
		}
        debug_logf(FIP_LOGGER "CB:: Setting status to CALLBACK_PARAM_SENT for %p\n", (uintptr_t) allocated_frame);
        allocated_frame->status = CALLBACK_PARAM_SENT;
        debug_logf(FIP_LOGGER "CB:: Put the frame %p back to %p\n", (uintptr_t) allocated_frame, (uintptr_t) frames);
		frames->push_front(allocated_frame);
	}
	debug_logf(FIP_LOGGER "CB:: Stopped thread for frame callback of device %s \n", callback_item->device_id);
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
    debug_logf(FIP_LOGGER "%p Trying to add frame image callback handler %p for device %s existing? %s \n",(uintptr_t)this, (uintptr_t)callback, 
            device_id, end_item == entry ? "no":"yes");
    if (end_item == entry) {
        debug_logf(FIP_LOGGER "Need to create a new callback container for device %s\n", device_id);
        frame_callback_handler* handlers = (frame_callback_handler*)malloc(sizeof(frame_callback_handler) * PRE_ALLOC_CALLBASCK_SIZE);
        if (!handlers) {
            debug_logf(FIP_LOGGER "No enough memory for storing callbacks\n");
            return;
        }
        handlers[0] = callback;
        device_frame_img_callback* callback_item = new device_frame_img_callback();
        if (!callback_item) {
            debug_logf(FIP_LOGGER "No enough memory for storing callback container\n");
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
        debug_logf(FIP_LOGGER "Creating new frame image callback handler holder for device=%s ok ? %s devices registered %llu\n", 
                device_id_cpy, callback_added ? "YES":"NO", this->registry->size());
        if (callback_added) {
            // if failed to start thread
            this->start_callback_thread(device_id_cpy, callback_item);
        } else {
            // just in case
            debug_logf(FIP_LOGGER "It should not be happened, the device %s already had callbacks\n", device_id_cpy);
            release_device_img_callback(callback_item);
        }
    } else {
        device_frame_img_callback* handler_container = entry->second;
        debug_logf("Trying to add callback %p to exsiting callbacks(%d) for device %s\n", (uintptr_t)callback,
                handler_container->handler_count, device_id);
        //lock the callback item
        std::lock_guard<std::mutex> lock { handler_container->lock };
        int old_count = handler_container->handler_count;
        if (old_count > PRE_ALLOC_CALLBASCK_SIZE && old_count  >= handler_container->allocated_handler_space) {
            int new_count = old_count + 1;
            int multiple = new_count / PRE_ALLOC_CALLBASCK_SIZE;
            int amount = new_count % PRE_ALLOC_CALLBASCK_SIZE > 1 ? multiple + 1 : multiple;
            debug_logf(FIP_LOGGER "Re-alloc callback of device %s from %d to %d\n", handler_container->device_id, old_count, amount);
            // save the new amount
            handler_container->allocated_handler_space = amount;
            frame_callback_handler* new_handlers = (frame_callback_handler*)malloc(sizeof(frame_callback_handler) * PRE_ALLOC_CALLBASCK_SIZE * amount);
            if (!new_handlers) {
                debug_logf(FIP_LOGGER "No enough ram when trying to allocate %d frame_callback_handlers\n", amount);
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
    debug_logf(FIP_LOGGER "Trying to remove frame callback handler %p for device %s \n", (uintptr_t)callback, device_id);
    if (entry == this->registry->end()) {
        debug_logf(FIP_LOGGER "Device %s was not register of callback %p\n", device_id, (uintptr_t) callback);
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
            debug_logf(FIP_LOGGER "Removed frame callback handler %p for device %s\n", (uintptr_t)callback, device_id);
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
        debug_logf(FIP_LOGGER "Also remove dict entry for device %s\n", device_id);
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
    debug_logf(FIP_LOGGER "PREP:: Trying to add param for device %s, lock acquired, already had %d allocted frames. data size is %d\n", 
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
            debug_logf(FIP_LOGGER "PREP:: No enough memory for initing CallbackParams\n");
            return;
        }
        auto buffer_size = calc_buffer_size(frame_data_size, MAX_IMG_BUFFER_SIZE);
        debug_logf(FIP_LOGGER "PREP:: Allocating %d bytes for fram cache\n", buffer_size);
        params->frame_data = (uint8_t*)malloc(buffer_size);
        params->buffer_size = buffer_size;
        debug_logf(FIP_LOGGER "PREP:: Allocated %d bytes for fram cache\n", buffer_size);
        if (!params->frame_data) {
            debug_logf(FIP_LOGGER "PREP:: No enough memory for initing frame_data\n");
            delete params;
            return;
        }
        handler_container->allocated_frames++;
    }
    if (params == nullptr) {
        debug_logf(FIP_LOGGER "PREP:: FATAL: Could not allocate a param for sending callback\n");
        return;
    }
    debug_logf(FIP_LOGGER "PREP:: Current callback param is %p\n", (uintptr_t)params);
    // realloc ram
    if (params->buffer_size < frame_data_size) {
        free(params->frame_data);
        auto buffer_size = calc_buffer_size(frame_data_size, MAX_IMG_BUFFER_SIZE);
        debug_logf(FIP_LOGGER "PREP:: Re-allocating %d bytes for fram cache\n", buffer_size);
        params->frame_data = (uint8_t*)malloc(buffer_size);
        params->buffer_size = buffer_size;
        debug_logf(FIP_LOGGER "PREP:: Re-allocated %d bytes for fram cache\n", buffer_size);
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
    debug_logf(FIP_LOGGER "PREP:: Added frame %p for device %s to callback queue, data size %d, queue size: %d\n", (uintptr_t)params, 
            handler_container->device_id, 
            frame_data_size, handler_container->frames->size());
}

void frame_img_processor::clean_device_img_callback_state(std::string key, bool remove_from_registry) {
    auto entry = this->registry->find(key);
    if (entry == this->registry->end()) {
        debug_logf(FIP_LOGGER "%p Could not find callback container for device %s from %llu keys \n", 
                uintptr_t(this), 
                key.c_str(), this->registry->size());
        for(auto first = this->registry->begin(); first != this->registry->end(); first++) {
            debug_logf(FIP_LOGGER "Found key %s\n", first->first.c_str());
        }
        return;
    }
    device_frame_img_callback* handler_container = entry->second;
    // must lock first
    std::lock_guard<std::mutex> lock(handler_container->lock);
    debug_logf(FIP_LOGGER "Marking callback container %p to shutdown for device %s\n",(uintptr_t)handler_container, handler_container->device_id);
    // mark as stop
    handler_container->stop = 1;
    if (remove_from_registry) {
        this->registry->erase(entry);
    }
}
void frame_img_processor::del_all(char* device_id) {
    debug_logf(FIP_LOGGER "%p Trying to remove all frame image callbacks for %s\n", (uintptr_t) this, device_id);
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
