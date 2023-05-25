#include <stdint.h>
#include "socket_lib.h"
#include "scrcpy_video_decoder.h"
#include <stdlib.h>
#include <thread>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <mutex>
#include <map>
#include "model.h"
#include "utils.h"
#include "frame_img_callback.h"
#include "scrcpy_ctrl_handler.h"
#include "logging.h"

#ifndef SCRCPY_CTRL_SOCKET_NAME
#define SCRCPY_CTRL_SOCKET_NAME "ctrl"
#define SCRCPY_SOCKET_HEADER_SIZE 80 // 64 bytes name, 16 bytes type
#define SCRCPY_HEADER_DEVICE_ID_LEN 64                                
#define SCRCPY_HEADER_TYPE_LEN 16
#endif //!SCRCPY_CTRL_SOCKET_NAME

socket_lib::socket_lib(std::string token) : 
	image_size_dict(new std::map<std::string, image_size*>()), 
	original_image_size_dict(new std::map<std::string, image_size*>()),
	device_info_callback_dict(new std::map<std::string, std::vector<scrcpy_device_info_callback>*>()),
	m_token(token), 
    ctrl_socket_handler_map(new std::map<std::string, scrcpy_ctrl_socket_handler*>()),
    ctrl_sending_callback_map(new std::map<std::string, scrcpy_device_ctrl_msg_send_callback>()){}

void socket_lib::on_video_callback(char* device_id, uint8_t* frame_data, uint32_t frame_data_size, int w, int h, int raw_w, int raw_h) {
	this->internal_video_frame_callback(device_id, frame_data, frame_data_size, w, h, raw_w, raw_h);
}

image_size* socket_lib::get_configured_img_size(char* device_id) {
	return internal_get_image_size(this->image_size_dict, device_id);
}
void socket_lib::on_device_info(char* device_id, int screen_width, int screen_height) {
	auto size_obj = new image_size{ screen_width, screen_height };
	auto result = this->original_image_size_dict->emplace(device_id, size_obj);
	if (!result.second) {
		// try update value
		auto item = this->original_image_size_dict->find(device_id);
		delete item->second;
		item->second = size_obj;
	}
	this->invoke_device_info_callbacks(device_id, screen_width, screen_height);
}

int socket_lib::register_callback(char* device_id, frame_callback_handler callback) {
	debug_logf("Trying to register frame image callback for device %s \n", device_id);
	this->callback_handler->add(device_id, callback, (char *)this->m_token.c_str());
	return 0;
}

void socket_lib::unregister_callback(char* device_id, frame_callback_handler callback) {
	callback_handler->del(device_id, callback);
}

void socket_lib::config_image_size(char* device_id, int width, int height) {
	std::lock_guard<std::mutex> guard{ image_size_lock };
	debug_logf("Trying to set image width=%d height=%d for device %s\n", width, height, device_id);
	image_size* size_obj = new image_size();
	size_obj->width = width;
	size_obj->height = height;
	debug_logf("image_size_dict address is %p \n", (uintptr_t)this->image_size_dict);
	auto add_result = this->image_size_dict->emplace(device_id, size_obj);
	if (!add_result.second) {
		auto find = this->image_size_dict->find(device_id);
		delete find->second;
		find->second = size_obj;
	}
	debug_logf("There're %lu items inside image_size_dict\n", (long)(this->image_size_dict->size()));
}

std::string* socket_lib::read_socket_type(ClientConnection* connection) {
    int buf_size = SCRCPY_SOCKET_HEADER_SIZE;
    char data[SCRCPY_SOCKET_HEADER_SIZE];
    char *device_id = (char*) malloc(SCRCPY_HEADER_DEVICE_ID_LEN * sizeof(char));
    char *socket_type = (char*) malloc(SCRCPY_HEADER_TYPE_LEN * sizeof(char));
    int received = recv(connection->client_socket, data, buf_size, 0);
    debug_logf("received %d/%d bytes header\n", received, SCRCPY_SOCKET_HEADER_SIZE);
    if (received != buf_size) {
        return nullptr;
    }

    array_copy_to2(data, device_id, 0, 0, SCRCPY_HEADER_DEVICE_ID_LEN);
    array_copy_to2(data, socket_type, SCRCPY_HEADER_DEVICE_ID_LEN, 0, SCRCPY_HEADER_TYPE_LEN);
    
    connection->device_id = new std::string(device_id);

    connection->connection_type = new std::string(socket_type);
    
    debug_logf("Received device id=%s, socket type=%s\n", connection->device_id->c_str(), connection->connection_type->c_str());
    return connection->connection_type;
}

bool socket_lib::is_controll_socket(ClientConnection* connection) {
    auto type = this->read_socket_type(connection);
    bool result = strcmp(type->c_str(), SCRCPY_CTRL_SOCKET_NAME) == 0;
    debug_logf("Is received socket type %s == %s for socket %p ? %s\n", type->c_str(), SCRCPY_CTRL_SOCKET_NAME, connection->client_socket, result ? "true":"false");
    return result;
}

int socket_lib::handle_connetion(ClientConnection* connection) {
	SOCKET client_socket = connection->client_socket;
	int result = 0;
    bool is_ctrl_socket = this->is_controll_socket(connection);
    //check if it is a controll socket
    if (!is_ctrl_socket) {
        debug_logf("%p is a video socket for device %s \n", connection->client_socket, connection->device_id->c_str());
	    result = socket_decode(client_socket, this, connection->buffer_cfg, &(this->keep_accept_connection));
    } else {
        debug_logf("%p is a ctrl socket for device %s \n", connection->client_socket, connection->device_id->c_str());
        auto handler = new scrcpy_ctrl_socket_handler(connection->device_id, connection->client_socket);
        {
            std::lock_guard<std::mutex> lock(this->ctrl_socket_handler_map_lock);
            this->ctrl_socket_handler_map->emplace(std::string(connection->device_id->c_str()), handler);
        }
        std::function<void(std::string, std::string, int, int)> callback = [this](std::string device_id, std::string msg_id, int status, int data_len) {
            this->internal_on_ctrl_msg_sent_callback(device_id, msg_id, status, data_len);
        };
        result = handler->run(callback);
        delete handler;
    }
	goto end;
end:
	if (client_socket != INVALID_SOCKET) {
        debug_logf("Shutdown %p\n", client_socket);
		result = shutdown(client_socket, SD_SEND);
		if (result == SOCKET_ERROR) {
			debug_logf("Failed to close client connection: %d\n", WSAGetLastError());
		}
	}
    if (connection->connection_type) {
        debug_logf("Cleaning connection type data of connection_type=%s\n", connection->connection_type->c_str());
        delete connection->connection_type;
        connection->connection_type = nullptr;
    }
    if(connection->device_id) {
        std::string device_id_str = std::string(connection->device_id->c_str());
        if (is_ctrl_socket) {
            std::lock_guard<std::mutex> lock(this->ctrl_socket_handler_map_lock);
            auto item = this->ctrl_socket_handler_map->find(device_id_str);
            if (item != this->ctrl_socket_handler_map->end()) {
                debug_logf("Removing ctrl socket of %s  from map\n", connection->device_id->c_str());
                this->ctrl_socket_handler_map->erase(item);
            }
        } else {
            // tell ctrl socket to stop
            auto item = this->ctrl_socket_handler_map->find(device_id_str);
            if (item != this->ctrl_socket_handler_map->end()) {
                debug_logf("Telling ctrl socket of %s  to stop \n", connection->device_id->c_str());
                item->second->stop();
            }
        }
        debug_logf("Cleaning device id data of device_id=%s\n", connection->device_id->c_str());
        delete connection->device_id;
        connection->device_id = nullptr;
    }
    debug_logf("Deleting connection\n");
	delete connection;
    debug_logf("Cleaned up connection\n");
	return result;
}

int socket_lib::accept_new_connection(connection_buffer_config* cfg) {
	int result = 0;
	do {
		debug_logf("Trying to accepting a new connection for listener %p\n", listen_socket);
		// accept a connection
		SOCKET client_socket = accept(listen_socket, NULL, NULL);
		debug_logf("Got new client connection %p\n", client_socket);
		if (client_socket == INVALID_SOCKET) {
			result = 1;
			debug_logf("Failed to accept a client connection: %d\n", WSAGetLastError());
			continue;
		}
		debug_logf("New connection accpeted:%p\n", client_socket);
		ClientConnection* connection = NULL;
		connection = new ClientConnection();
		if (!connection) {
			debug_logf("No enough memory to accepting incoming connection %p\n", client_socket);
			shutdown(client_socket, SD_SEND);
			continue;
		}
		connection->client_socket = client_socket;
		connection->buffer_cfg = cfg;
		std::thread connection_thread(&socket_lib::handle_connetion, this, connection);
		connection_thread.detach();
	} while (keep_accept_connection > 0);
	// free this listener
	delete this;
	return result;
}
int socket_lib::startup(char* address, int network_buffer_size_kb, int video_packet_buffer_size_kb) {
	WSADATA wsaData;
	int result;
	struct addrinfo* addr_result = NULL;
	struct addrinfo addr_hints;
	struct connection_buffer_config cfg = connection_buffer_config{
		network_buffer_size_kb,
		video_packet_buffer_size_kb
	};
    debug_logf("WSAStartup\n");
	result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != 0) {
		debug_logf("Failed to init winsock: %d\n", result);
		return 1;
	}
	ZeroMemory(&addr_hints, sizeof(addr_hints));
	addr_hints.ai_family = AF_INET;
	addr_hints.ai_socktype = SOCK_STREAM;
	addr_hints.ai_protocol = IPPROTO_TCP;
	addr_hints.ai_flags = AI_PASSIVE;
	debug_logf("getaddrinfo\n");
	result = getaddrinfo(NULL, address, &addr_hints, &addr_result);
	if (result != 0) {
		debug_logf("Failed to call getaddrinfo for address: %s err_no=%d\n", address, result);
		WSACleanup();
		return 1;
	}
	debug_logf("socket\n");
	listen_socket = socket(addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol);
	if (listen_socket == INVALID_SOCKET) {
		debug_logf("Failed to create socket with error: %d\n", WSAGetLastError());
		freeaddrinfo(addr_result);
		WSACleanup();
		return 1;
	}
	result = bind(listen_socket, addr_result->ai_addr, (int)addr_result->ai_addrlen);
	if (result == SOCKET_ERROR) {
		debug_logf("Failed to bind with error: %d\n", WSAGetLastError());
		freeaddrinfo(addr_result);
		closesocket(listen_socket);
		WSACleanup();
		return 1;
	}
	freeaddrinfo(addr_result);
	result = listen(listen_socket, SOMAXCONN);
	if (result == SOCKET_ERROR) {
		debug_logf("Failed to start listening: %d\n", WSAGetLastError());
		if (listen_socket != INVALID_SOCKET) {
			closesocket(listen_socket);
		}
		WSACleanup();
		return result;
	}
	std::thread server_thread(&socket_lib::accept_new_connection, this, &cfg);
	debug_logf("Started new thread accepting new connection for listener %p\n", (uintptr_t)listen_socket);
	server_thread.join();
	// close the listen socket
	closesocket(listen_socket);
	listen_socket = INVALID_SOCKET;
	if (listen_socket != INVALID_SOCKET) {
		closesocket(listen_socket);
	}
	WSACleanup();
	return result;
}
image_size* socket_lib::get_original_screen_size(char* device_id) {
	return this->internal_get_image_size(this->original_image_size_dict, device_id);
}
void socket_lib::shutdown_svr() {
	std::lock_guard<std::mutex> guard{ keep_accept_connection_lock };
	if (keep_accept_connection == 0) {
		return;
	}
	keep_accept_connection = 0;
}

void socket_lib::remove_all_callbacks(char* device_id) {
	callback_handler->del_all(device_id);
}
socket_lib::~socket_lib() {
	if (this->callback_handler) {
		delete this->callback_handler;
		this->callback_handler = nullptr;
	}
	// free image size
	free_image_size_dict(this->image_size_dict);
	free_image_size_dict(this->original_image_size_dict);
	if (this->device_info_callback_dict) {
        std::lock_guard<std::mutex> lock(this->device_info_callback_dict_lock);
		auto dict = this->device_info_callback_dict;
		auto first = dict->begin();
		while (first != dict->end()) {
			// clear callbacks
			first->second->clear();
			delete first->second;
			first++;
		}
		delete this->device_info_callback_dict;
		this->device_info_callback_dict = nullptr;
	}
    if (this->ctrl_socket_handler_map) {
        this->ctrl_socket_handler_map->clear();
        delete this->ctrl_socket_handler_map;
        this->ctrl_socket_handler_map = nullptr;
    }
    if (this->ctrl_sending_callback_map) {
        std::lock_guard<std::mutex> lock(this->ctrl_sending_callback_map_lock);
        this->ctrl_sending_callback_map->clear();
        delete this->ctrl_sending_callback_map;
        this->ctrl_sending_callback_map = nullptr;
    }
}

image_size* socket_lib::internal_get_image_size(std::map<std::string, image_size*>* dict, std::string device_id) {
	std::string id_str(device_id);
	auto item = dict->find(device_id);
	if (item != dict->end()) {
		return item->second;
	}
	return nullptr;
}
void socket_lib::free_image_size_dict(std::map<std::string, image_size*>* dict) {
	auto first_one = dict->begin();
	while (first_one != dict->end()) {
		free(first_one->second);
		first_one++;
	}
	delete dict;
}
void socket_lib::internal_video_frame_callback(std::string device_id, uint8_t* frame_data, uint32_t frame_data_size, int w, int h, int raw_w, int raw_h) {
	debug_logf("Got video frame for device = %s data size = %d\n", device_id.c_str(), frame_data_size);
	callback_handler->invoke((char *)this->m_token.c_str(), const_cast<char*>(device_id.c_str()), frame_data, frame_data_size, w, h, raw_w, raw_h);
}

void socket_lib::register_device_info_callback(char* device_id, scrcpy_device_info_callback callback) {
	std::lock_guard<std::mutex> guard(this->device_info_callback_dict_lock);
	debug_logf("registering device info callback for %s, callback pointer is %p\n", device_id, (uintptr_t) callback);
	auto dict = this->device_info_callback_dict;
	auto find = this->device_info_callback_dict->find(std::string(device_id));
	if (find == dict->end()) {
		std::vector<scrcpy_device_info_callback> *callbacks = new std::vector<scrcpy_device_info_callback>();
		callbacks->push_back(callback);
		dict->emplace(std::string(device_id), callbacks);
		debug_logf("there're %lu callbacks for device %s\n", callbacks->size(), device_id);
	} else {
		find->second->push_back(callback);
		debug_logf("there're %lu callbacks for device %s\n", find->second->size(), device_id);
	}
}

void socket_lib::unregister_all_device_info_callbacks(char* device_id) {
	std::lock_guard<std::mutex> guard(this->device_info_callback_dict_lock);
	debug_logf("unregistering all callbacks for device %s\n", device_id);
	auto dict = this->device_info_callback_dict;
	auto find = this->device_info_callback_dict->find(std::string(device_id));
	if (find == dict->end()) {
		return;
	}
	find->second->clear();
	delete find->second;
	dict->erase(find);
}

void socket_lib::invoke_device_info_callbacks(char* device_id, int screen_width, int screen_height) {
	debug_logf("invoking all device info callbacks for device %s w=%d h=%d\n", device_id, screen_width, screen_height);
	auto dict = this->device_info_callback_dict;
	auto find = this->device_info_callback_dict->find(std::string(device_id));
	if (find == dict->end()) {
		debug_logf("no device info callback handler found for device %s\n", device_id);
		return;
	}
	auto callback_handlers = find->second;
	auto first_one = callback_handlers->begin();
	debug_logf("calling function pointers of device info callback for device %s \n", device_id);
	while (first_one != callback_handlers->end()) {
		(**first_one)((char *)this->m_token.c_str(), device_id, screen_width, screen_height);
		first_one++;
	}
}

void socket_lib::set_ctrl_msg_send_callback(char *device_id, scrcpy_device_ctrl_msg_send_callback callback) {
    std::lock_guard<std::mutex> lock(this->ctrl_sending_callback_map_lock);
    auto result = this->ctrl_sending_callback_map->emplace(std::string(device_id), callback);
    if (!result.second) {
        result.first->second = callback;
    }
    debug_logf("Set ctrl msg sending handler for device %s, alrady existed? %s (will update if already existed)\n", device_id, result.second ? "no":"yes");
}

void socket_lib::send_ctrl_msg(char *device_id, char *msg_id, uint8_t* data, int data_len) {
   auto device_id_str = std::string(device_id);
   auto msg_id_str = std::string(msg_id);
   auto entry = this->ctrl_socket_handler_map->find(std::string(device_id));
   if(entry == this->ctrl_socket_handler_map->end()) {
       debug_logf("No control socket connected for device %s\n", device_id);
       this->internal_on_ctrl_msg_sent_callback(device_id_str, msg_id_str, -9999, data_len);
       return;
   }
   debug_logf("Sending control msg id=%s, data_len=%d for device=%s\n", msg_id, data_len, device_id);
   entry->second->send_msg(msg_id, data, data_len);
}

void socket_lib::internal_on_ctrl_msg_sent_callback(std::string device_id, std::string msg_id, int status, int data_len) {
    auto callback_entry = this->ctrl_sending_callback_map->find(device_id);
    if (callback_entry == this->ctrl_sending_callback_map->end()) {
        debug_logf("Could not find a callback handler for device %s's ctrl sending callback\n'", device_id.c_str());
        return;
    }
    debug_logf("Invoking ctrl sending callback, device_id=%s, msg_id=%s, data_len=%d, sending_status=%d\n", device_id.c_str(), msg_id.c_str(), data_len, status);
    callback_entry->second((char *)this->m_token.c_str(), (char *)device_id.c_str(), (char *)msg_id.c_str(), status, data_len);
}

