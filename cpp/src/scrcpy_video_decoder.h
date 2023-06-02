#include "model.h"
#include <WinSock2.h>
/*
 * decoding data from the socket
 * @param			socket				socket connection for video data
 * @param			callback				callback instance for video	frame and meta data
 * @param			buffer_cfg			socket/decoder buffer config
 * @param			keep_running			a pointer of keep running flag. The decoder will stop receiving data if the flag become 0
 * @return			decoder status, 0 means ok
 */
int socket_decode(SOCKET socket, video_decode_callback *callback, 
        connection_buffer_config *buffer_cfg, int *keep_running);
