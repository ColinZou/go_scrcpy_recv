#include "frame_img_callback.h"
#include "utils.h"
#include <thread>
#include "logging.h"

#define MAX_IMG_BUFFER_SIZE 1 * 1024 * 1024
int frame_img_processor::callback_thread(device_frame_img_callback *callback_item) {
	std::deque<frame_img_callback_params*> *frames = &callback_item->frames;
	debug_logf("Running thread for frame callback of device %s \n", callback_item->device_id);
	BOOL wait_for_next = FALSE;
	while (true) {
		if (wait_for_next) {
			wait_for_next = FALSE;
			std::this_thread::sleep_for(std::chrono::nanoseconds(50));
		}
		std::lock_guard<std::mutex> wait_lock(callback_item->lock);
		if (callback_item->stop > 0) {
			debug_logf("stopping callback for %s\n", callback_item->device_id);
			break;
		}
		frame_img_callback_params* allocated_frame = NULL;
		if (!frames->empty()) {
			//grab a frame
			for (int i = 0; i < MAX_PENDING_FRAMES; i++) {
				frame_img_callback_params* cur_frame = frames->front();
				frames->pop_front();
				if (cur_frame->status == CALLBACK_PARAM_PENDING) {
					allocated_frame = cur_frame;
					break;
				}
				else {
					frames->push_back(cur_frame);
				}
			}
		}
		if (NULL == allocated_frame) {
			wait_for_next = TRUE;
			continue;
		}
		std::lock_guard<std::mutex> allocated_frame_guard{allocated_frame->lock};
		allocated_frame->status = CALLBACK_PARAM_SENDING;
		debug_logf("Invoking frame callback device=%s frame data size=%d pointer %p total handlers = %d\n", allocated_frame->device_id.c_str(), 
			allocated_frame->frame_data_size, (uintptr_t) allocated_frame, callback_item->handler_count);
		// call the handlers
		for (int i = 0; i < callback_item->handler_count; i++) {
			frame_callback_handler callback = callback_item->handlers[i];
			scrcpy_rect img_size = scrcpy_rect{ allocated_frame->w, allocated_frame->h};
			scrcpy_rect screen_size = scrcpy_rect{ allocated_frame->raw_w, allocated_frame->raw_h };
			callback(callback_item->token, const_cast<char*>(allocated_frame->device_id.c_str()), allocated_frame->frame_data, allocated_frame->frame_data_size,
				img_size, screen_size);
			allocated_frame->status = CALLBACK_PARAM_SENT;
		}
		frames->push_back(allocated_frame);
	}
	debug_logf("Stopped thread for frame callback of device %s \n", callback_item->device_id);
	return 0;
}
frame_img_processor::frame_img_processor() :
	registry(new std::map<std::string, device_frame_img_callback*>()){  }

int frame_img_processor::start_callback_thread(char* device_id, device_frame_img_callback* handler_container) {
	// start thread
	handler_container->stop = 0;
	handler_container->device_id = device_id;
	std::thread callback_thread(&frame_img_processor::callback_thread, this, handler_container);
	callback_thread.detach();
	handler_container->thread_handle = callback_thread.native_handle();
	return 0;
}
void frame_img_processor::add(char *device_id, frame_callback_handler callback, char *token) {
	std::lock_guard<std::mutex> guard{ this->lock };
	auto entry = this->registry->find(device_id);
	auto end_item = this->registry->end();
	debug_logf("Trying to add frame image callback handler %p for device %s \n", (uintptr_t)callback, device_id);
	if (end_item == entry) {
		frame_callback_handler* handlers = (frame_callback_handler*)malloc(sizeof(frame_callback_handler) * PRE_ALLOC_CALLBASCK_SIZE);
		if (!handlers) {
			debug_logf("No enough memory for storing callbacks\n");
			return;
		}
		handlers[0] = callback;
		device_frame_img_callback* callback_item = new device_frame_img_callback();
		if (!callback_item) {
			debug_logf("No enough memory for storing callback container\n");
			return;
		}
		callback_item->device_id = device_id;
		callback_item->handler_count = 1;
		callback_item->token = token;
		callback_item->handlers = handlers;
		auto existing = this->registry->emplace(device_id, callback_item);
		auto callback_existed = existing.second;
		// if failed to start thread
		if (this->start_callback_thread(device_id, callback_item)) {
			debug_logf("Failed to start thread for handling frame image callbacks for device %s\n", device_id);
			this->registry->erase(this->registry->find(device_id));
			delete callback_item;
			return;
		}
		// just in case
		if (!callback_existed) {
			debug_logf("It should not be happened, the device %s already had callbacks\n", device_id);
		}
	}
	else {
		device_frame_img_callback* handler_container = entry->second;
		int old_count = handler_container->handler_count;
		if (old_count == 0 && NULL == handler_container->thread_handle) {
			if (!this->start_callback_thread(device_id, handler_container)) {
				debug_logf("Could not create callback thread for device %s \n", device_id);
				return;
			}
		}
		if (old_count > PRE_ALLOC_CALLBASCK_SIZE && old_count  >= handler_container->allocated_handler_space) {
			int new_count = old_count + 1;
			int multiple = new_count / PRE_ALLOC_CALLBASCK_SIZE;
			int amount = new_count % PRE_ALLOC_CALLBASCK_SIZE > 1 ? multiple + 1 : multiple;
			debug_logf("Re-alloc callback of device %s from %d to %d\n", device_id, old_count, amount);
			// save the new amount
			handler_container->allocated_handler_space = amount;
			frame_callback_handler* new_handlers = (frame_callback_handler*)malloc(sizeof(frame_callback_handler) * PRE_ALLOC_CALLBASCK_SIZE * amount);
			if (!new_handlers) {
				debug_logf("No enough ram when trying to allocate %d frame_callback_handlers\n", amount);
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
		// start the thread to handle callback
		if (handler_container->handler_count == 1) {
			this->start_callback_thread(device_id, handler_container);
		}
	}
}
void frame_img_processor::del(char* device_id, frame_callback_handler callback) {
	std::lock_guard<std::mutex> guard{ this->lock };
	auto entry = this->registry->find(device_id);
	if (entry == this->registry->end()) {
		return;
	}
	debug_logf("Trying to remove frame callback handler %p for device %s \n", (uintptr_t)callback, device_id);
	device_frame_img_callback* handler_container = entry->second;
	frame_callback_handler* handlers = handler_container->handlers;
	int start_index = 0;
	for (int i = 0; i < handler_container->handler_count; i++) {
		if (handlers[i] == callback) {
			start_index = i;
			//clear current item
			handlers[i] = NULL;
			handler_container->handler_count--;
			debug_logf("Removed frame callback handler %p for device %s\n", (uintptr_t)callback, device_id);
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
		handler_container->thread_handle = NULL;
	}
}
frame_img_processor::~frame_img_processor() {
	auto first = this->registry->begin();
	auto last = this->registry->end();
	while (first != last) {
		//remove all handles
		std::string key = first->first;
		this->del_all(const_cast<char*>(key.c_str()));
		first++;
	}
	// remove all items
	this->registry->clear();
}
void frame_img_processor::invoke(char *token, char* device_id, uint8_t* frame_data, uint32_t frame_data_size, int w, int h, int raw_w, int raw_h) {
	auto entry = this->registry->find(device_id);
	if (entry == this->registry->end()) {
		debug_logf("No callback configured for device %s \n", device_id);
		return;
	}
	device_frame_img_callback* handler_container = entry->second;
	debug_logf("Trying to add param for device %s, acquiring lock\n", device_id);
	std::lock_guard<std::mutex> lock{ handler_container->lock };
	int buffed_frames = handler_container->allocated_frames;
	debug_logf("Trying to add param for device %s, lock acquired, already had %d allocted frames\n", device_id, buffed_frames);
	frame_img_callback_params* params = NULL;
	if (buffed_frames >= MAX_PENDING_FRAMES ) {
		// release a frame from back 
		params = handler_container->frames.back();
		handler_container->frames.pop_back();
	} else {
		params = new frame_img_callback_params();
		if (!params) {
			debug_logf("No enough memory for initing CallbackParams\n");
			return;
		}
		debug_logf("Allocating %d bytes for fram cache\n", MAX_IMG_BUFFER_SIZE);
		params->frame_data = (uint8_t*)malloc(MAX_IMG_BUFFER_SIZE);
		if (!params->frame_data) {
			debug_logf("No enough memory for initing frame_data\n");
			delete params;
			return;
		}
		handler_container->allocated_frames++;
	}
	std::lock_guard<std::mutex> param_lock{ params->lock };
	params->token = token;
	params->status = CALLBACK_PARAM_PENDING;
	params->device_id = std::string(device_id);
	array_copy_to((char*)frame_data, (char*)params->frame_data, 0, frame_data_size);
	params->frame_data_size = frame_data_size;
	params->w = w;
	params->h = h;
	params->raw_w = raw_w;
	params->raw_h = raw_h;
	// push the frame to back
	handler_container->frames.push_back(params);
	debug_logf("Added frame %p for device %s to callback queue, data size %d, queue size: %d\n", (uintptr_t)params, device_id, 
		frame_data_size, handler_container->frames.size());
}
void frame_img_processor::del_all(char* device_id) {
	auto entry = this->registry->find(device_id);
	if (entry == this->registry->end()) {
		return;
	}
	device_frame_img_callback* handler_container = entry->second;
	// mark as stop
	handler_container->stop = 1;
	std::deque<frame_img_callback_params*>* frames = &handler_container->frames;
	int size = frames->size();
	while (size > 0) {
		frame_img_callback_params* item = frames->back();
		{
			std::lock_guard<std::mutex> lock{ item->lock };
			if (NULL != item->frame_data) {
				free(item->frame_data);
				item->frame_data = NULL;
			}
			frames->pop_back();
		}
		delete item;
		size--;
	}
	free(handler_container->handlers);
	delete handler_container;
}
