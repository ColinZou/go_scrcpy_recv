#include <stdint.h>
#include "fmt/core.h"
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

#ifndef SCRCPY_CTRL_SOCKET_NAME
#define SCRCPY_CTRL_SOCKET_NAME "ctrl"
#define SCRCPY_SOCKET_HEADER_SIZE 80 // 64 bytes name, 16 bytes type
#define SCRCPY_HEADER_DEVICE_ID_LEN 64                                
#define SCRCPY_HEADER_TYPE_LEN 16
#endif //!SCRCPY_CTRL_SOCKET_NAME

using namespace std;

socket_lib::socket_lib(std::string token) : 
	image_size_dict(new std::map<std::string, image_size*>()), 
	original_image_size_dict(new std::map<std::string, image_size*>()),
	device_info_callback_dict(new std::map<std::string, std::vector<scrcpy_device_info_callback>*>()),
	m_token(token) {}
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
	fmt::print("Trying to register frame image callback for device {} \n", device_id);
	this->callback_handler->add(device_id, callback, (char *)this->m_token.c_str());
	return 0;
}

void socket_lib::unregister_callback(char* device_id, frame_callback_handler callback) {
	callback_handler->del(device_id, callback);
}

void socket_lib::config_image_size(char* device_id, int width, int height) {
	std::lock_guard<std::mutex> guard{ image_size_lock };
	fmt::print("Trying to set image width={} height={} for device {}\n", width, height, device_id);
	image_size* size_obj = new image_size();
	size_obj->width = width;
	size_obj->height = height;
	fmt::print("image_size_dict address is {} \n", (uintptr_t)this->image_size_dict);
	auto add_result = this->image_size_dict->emplace(device_id, size_obj);
	if (!add_result.second) {
		auto find = this->image_size_dict->find(device_id);
		delete find->second;
		find->second = size_obj;
	}
	fmt::print("There're {} items inside image_size_dict\n", (long)(this->image_size_dict->size()));
}

std::string* socket_lib::read_socket_type(ClientConnection* connection) {
    int buf_size = SCRCPY_SOCKET_HEADER_SIZE;
    char data[SCRCPY_SOCKET_HEADER_SIZE];
    char device_id[SCRCPY_HEADER_DEVICE_ID_LEN];
    char socket_type[SCRCPY_HEADER_TYPE_LEN];
    int received = recv(connection->client_socket, data, buf_size, 0);
    fmt::print("received {}/{} bytes header\n", received, SCRCPY_SOCKET_HEADER_SIZE);
    if (received != buf_size) {
        return nullptr;
    }
    fmt::print("*************HEADER RECEIVED*************");
    print_bytes(data, SCRCPY_SOCKET_HEADER_SIZE);

    array_copy_to2(data, device_id, 0, 0, SCRCPY_HEADER_DEVICE_ID_LEN);
    array_copy_to2(data, socket_type, SCRCPY_HEADER_DEVICE_ID_LEN, 0, SCRCPY_HEADER_TYPE_LEN);
    fmt::print("checking device id = {}, socket_type = {}\n", device_id, socket_type);

    connection->device_id = new std::string(device_id);
    connection->device_id->copy(device_id, 64);

    connection->connection_type = new std::string(socket_type);
    connection->connection_type->copy(socket_type, 16);


    fmt::print("Trying to read socket type, want {} bytes, got {} bytes. device_id={} socket_type={} \n", buf_size, 
            received, connection->device_id->c_str(), connection->connection_type->c_str());
    return connection->connection_type;
}

bool socket_lib::is_controll_socket(ClientConnection* connection) {
    auto type = this->read_socket_type(connection);
    bool result = strcmp(type->c_str(), SCRCPY_CTRL_SOCKET_NAME) == 0;
    fmt::print("Is received socket type {} == {} for socket {} ? {}\n", type->c_str(), SCRCPY_CTRL_SOCKET_NAME, connection->client_socket, result);
    delete type;
    return result;
}

int socket_lib::handle_connetion(ClientConnection* connection) {
	SOCKET client_socket = connection->client_socket;
	int result = 0;
    //check if it is a controll socket
    if (!this->is_controll_socket(connection)) {
	    result = socket_decode(client_socket, this, connection->buffer_cfg, &(this->keep_accept_connection));
    } else {
        fmt::print("{} is a ctrl socket \n", connection->client_socket);
    }
	goto end;
end:
	if (client_socket != INVALID_SOCKET) {
		result = shutdown(client_socket, SD_SEND);
		if (result == SOCKET_ERROR) {
			fmt::print("Failed to close client connection: {}\n", WSAGetLastError());
		}
	}
    if (connection->connection_type) {
        delete connection->connection_type;
    }
    if(connection->device_id) {
        delete connection->device_id;
    }
	free(connection);
	return result;
}

int socket_lib::accept_new_connection(connection_buffer_config* cfg) {
	int result = 0;
	do {
		fmt::print("Trying to accepting a new connection for listener {}\n", (int)listen_socket);
		// accept a connection
		SOCKET client_socket = accept(listen_socket, NULL, NULL);
		fmt::print("Got new client connection {}\n", client_socket);
		if (client_socket == INVALID_SOCKET) {
			result = 1;
			fmt::print("Failed to accept a client connection: {}\n", WSAGetLastError());
			continue;
		}
		fmt::print("New connection accpeted:{}\n", client_socket);
		ClientConnection* connection = NULL;
		connection = new ClientConnection();
		if (!connection) {
			fmt::print("No enough memory to accepting incoming connection {}\n", client_socket);
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
	fmt::print("WSAStartup\n");
	result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != 0) {
		fmt::print("Failed to init winsock: {}\n", result);
		return 1;
	}
	ZeroMemory(&addr_hints, sizeof(addr_hints));
	addr_hints.ai_family = AF_INET;
	addr_hints.ai_socktype = SOCK_STREAM;
	addr_hints.ai_protocol = IPPROTO_TCP;
	addr_hints.ai_flags = AI_PASSIVE;
	fmt::print("getaddrinfo\n");
	result = getaddrinfo(NULL, address, &addr_hints, &addr_result);
	if (result != 0) {
		fmt::print("Failed to call getaddrinfo for address: {} err_no={}\n", address, result);
		WSACleanup();
		return 1;
	}
	fmt::print("socket\n");
	listen_socket = socket(addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol);
	if (listen_socket == INVALID_SOCKET) {
		fmt::print("Failed to create socket with error: {}\n", WSAGetLastError());
		freeaddrinfo(addr_result);
		WSACleanup();
		return 1;
	}
	result = bind(listen_socket, addr_result->ai_addr, (int)addr_result->ai_addrlen);
	if (result == SOCKET_ERROR) {
		fmt::print("Failed to bind with error: {}\n", WSAGetLastError());
		freeaddrinfo(addr_result);
		closesocket(listen_socket);
		WSACleanup();
		return 1;
	}
	freeaddrinfo(addr_result);
	result = listen(listen_socket, SOMAXCONN);
	if (result == SOCKET_ERROR) {
		fmt::print("Failed to start listening: {}\n", WSAGetLastError());
		if (listen_socket != INVALID_SOCKET) {
			closesocket(listen_socket);
		}
		WSACleanup();
		return result;
	}
	std::thread server_thread(&socket_lib::accept_new_connection, this, &cfg);
	fmt::print("Started new thread accepting new connection for listener {}\n", (uintptr_t)listen_socket);
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
	fmt::print("Got video frame for device = {} data size = {}\n", device_id, frame_data_size);
	callback_handler->invoke((char *)this->m_token.c_str(), const_cast<char*>(device_id.c_str()), frame_data, frame_data_size, w, h, raw_w, raw_h);
}

void socket_lib::register_device_info_callback(char* device_id, scrcpy_device_info_callback callback) {
	std::lock_guard<std::mutex> guard(this->device_info_callback_dict_lock);
	fmt::print("registering device info callback for {}, callback pointer is {}\n", device_id, (uintptr_t) callback);
	auto dict = this->device_info_callback_dict;
	auto find = this->device_info_callback_dict->find(std::string(device_id));
	if (find == dict->end()) {
		std::vector<scrcpy_device_info_callback> *callbacks = new std::vector<scrcpy_device_info_callback>();
		callbacks->push_back(callback);
		dict->emplace(std::string(device_id), callbacks);
		fmt::print("there're {} callbacks for device {}\n", callbacks->size(), device_id);
	} else {
		find->second->push_back(callback);
		fmt::print("there're {} callbacks for device {}\n", find->second->size(), device_id);
	}
}

void socket_lib::unregister_all_device_info_callbacks(char* device_id) {
	std::lock_guard<std::mutex> guard(this->device_info_callback_dict_lock);
	fmt::print("unregistering all callbacks for device {}\n", device_id);
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
	fmt::print("invoking all device info callbacks for device {} w={} h={}\n", device_id, screen_width, screen_height);
	auto dict = this->device_info_callback_dict;
	auto find = this->device_info_callback_dict->find(std::string(device_id));
	if (find == dict->end()) {
		fmt::print("no device info callback handler found for device {}\n", device_id);
		return;
	}
	auto callback_handlers = find->second;
	auto first_one = callback_handlers->begin();
	fmt::print("calling function pointers of device info callback for device {} \n", device_id);
	while (first_one != callback_handlers->end()) {
		(**first_one)((char *)this->m_token.c_str(), device_id, screen_width, screen_height);
		first_one++;
	}
}


