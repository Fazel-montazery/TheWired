#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>

#include "ansi_colors.h"

// Simple logging
static void printError(const char* format, ...)
{
	va_list args;

	va_start(args, format);
	fprintf(stderr, RED);
	vfprintf(stderr, format, args);
	fprintf(stderr, CRESET);
	va_end(args);
}

static void printWarning(const char* format, ...)
{
	va_list args;

	va_start(args, format);
	fprintf(stderr, YEL);
	vfprintf(stderr, format, args);
	fprintf(stderr, CRESET);
	va_end(args);
}

static void printMsg(const char* format, ...)
{
	va_list args;

	va_start(args, format);
	fprintf(stderr, GRN);
	vfprintf(stderr, format, args);
	fprintf(stderr, CRESET);
	va_end(args);
}

// Return states
typedef enum {
	SUCCESS = 0,
	ERROR_SERVER_SOCKET_CREATION,
	ERROR_SERVER_SOCKET_FCNTL_GETFL,
	ERROR_SERVER_SOCKET_FCNTL_SETFL,
	ERROR_SERVER_SOCKET_BINDING,
	ERROR_SERVER_SOCKET_LISTENING,
	ERROR_SERVER_ACCEPT,
	ERROR_POLL_FAIL,
	ERROR_POLL_TIMEOUT,
	ERROR_POLL_REVENTS
} Result;

void printResult(Result s)
{
	switch (s) {
	case ERROR_SERVER_SOCKET_CREATION:
		printError("Socket creation failed! => errno:%s\n", strerror(errno));
		break;

	case ERROR_SERVER_SOCKET_FCNTL_GETFL:
		printError("Coudnl't retrive flags with fcntl()! => errno:%s\n", strerror(errno));
		break;

	case ERROR_SERVER_SOCKET_FCNTL_SETFL:
		printError("Coudnl't set flags with fcntl()! => errno:%s\n", strerror(errno));
		break;

	case ERROR_SERVER_SOCKET_BINDING:
		printError("Binding socket failed! => errno:%s\n", strerror(errno));
		break;

	case ERROR_SERVER_SOCKET_LISTENING:
		printError("Listening failed! => errno:%s\n", strerror(errno));
		break;

	case ERROR_SERVER_ACCEPT:
		printError("Accept failed => errno:%s\n", strerror(errno));
		break;

	case ERROR_POLL_FAIL:
		printError("Poll failed! => errno:%s\n", strerror(errno));
		break;

	case ERROR_POLL_TIMEOUT:
		printError("Poll timedout! => errno:%s\n", strerror(errno));
		break;

	case ERROR_POLL_REVENTS:
		printError("Error revents!\n");
		break;

	default:
		printError("Unknown Result!\n");
	}
}

// Defs
#define CHECK_RESULT(state) do {		 \
	if (state != SUCCESS) {			\
		printResult(state);		 \
		return state;			\
	}					\
} while(0)

#define PORT 8080
#define MAX_CLIENTS 4
#define BACKLOG 10
#define MAX_BUFFER_SIZE 1024
#define MAX_NAME_LEN 30

#define SERVER_FULL_STRING "[SERVERISFULL]"

// Server
static Result initServer(int* server_socket)
{
	int sock;
	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		return ERROR_SERVER_SOCKET_CREATION;

	int on = 1;
	if ((setsockopt(sock, SOL_SOCKET,  SO_REUSEADDR, (char *)&on, sizeof(on))) ==  -1) {
		close(sock);
		return ERROR_SERVER_SOCKET_CREATION;
	}


	int flags = fcntl(sock, F_GETFL, 0);
	if (flags == -1) {
		close(sock);
		return ERROR_SERVER_SOCKET_FCNTL_GETFL;
	}

	if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
		close(sock);
		return ERROR_SERVER_SOCKET_FCNTL_SETFL;
	}

	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(PORT),
		.sin_addr.s_addr = INADDR_ANY
	};

	if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		close(sock);
		return ERROR_SERVER_SOCKET_BINDING;
	}

	if (listen(sock, BACKLOG) == -1) {
		close(sock);
		return ERROR_SERVER_SOCKET_LISTENING;
	}

	printMsg("Server is listening on port %hu\n", PORT);

	*server_socket = sock;
	return SUCCESS;
}

static bool sendMsg(char* send_buffer, int socket, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	vsnprintf(send_buffer, MAX_BUFFER_SIZE, format, args);
	va_end(args);

	int snd = send(socket, send_buffer, strlen(send_buffer), 0);
	if (snd == -1) {
		printError("send error: %s\n", strerror(errno));
		return false;
	}
	return true;
}

static void sendToAll(char* send_buffer, int client_socket, int server_socket, struct pollfd* fds, int nfds, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	vsnprintf(send_buffer, MAX_BUFFER_SIZE, format, args);
	va_end(args);

	int bufferlen = strlen(send_buffer);

	for (int i = 0; i < nfds; i++) {
		if (fds[i].fd == server_socket) {
			continue;
		}

		if (fds[i].fd == client_socket) {
			continue;
		}

		if (send(fds[i].fd, send_buffer, bufferlen, 0) == -1)
			printError("send error: %s\n", strerror(errno));
	}
}

static bool acceptConnection(char* send_buffer, int server_socket, struct pollfd* fds, int* nfds) // Return false if exit condition else true
{
	int new_socket;
	do {
		new_socket = accept(server_socket, NULL, NULL);
		if (new_socket < 0) {
			if (errno != EWOULDBLOCK) {
				return false;
			}
			break;
		}

		if (*nfds > MAX_CLIENTS) { // Server is full
			sendMsg(send_buffer, new_socket, SERVER_FULL_STRING);
			close(new_socket);
			printError("Server is full!\n");
			return true;
		}

		char name[MAX_NAME_LEN + 1] = { 0 };
		int rc = recv(new_socket, name, MAX_NAME_LEN, 0);
		if (rc == -1) {
			close(new_socket);
			return false;
		}
		name[rc] = '\0';

		int flags = fcntl(new_socket, F_GETFL, 0);
		if (flags == -1) {
			printError("Coudnl't retrive flags for %d with fcntl()! => errno:%s\n", new_socket, strerror(errno));
			close(new_socket);
			return true;
		}

		if (fcntl(new_socket, F_SETFL, flags | O_NONBLOCK) == -1) {
			printError("Coudnl't set flags for %d with fcntl()! => errno:%s\n", new_socket, strerror(errno));
			close(new_socket);
			return true;
		}

		printMsg("New connection on socket %d with name %s\n", new_socket, name);

		fds[*nfds].fd = new_socket;
		fds[*nfds].events = POLLIN;
		*nfds += 1;
	} while (new_socket != -1);

	return true;
}

static bool handleConnection(char* send_buffer, char* recv_buffer, int client_socket, int server_socket, struct pollfd* fds, int nfds)
{
	int rc = 0;
	int len = 0;

	do {
		rc = recv(client_socket, recv_buffer, MAX_BUFFER_SIZE, 0);
		if (rc < 0) {
			if (errno != EWOULDBLOCK) {
				printWarning("Connection %d closed => errno: %s\n", client_socket, strerror(errno));
				return false;
			}
			break;
		}

		if (rc == 0) {
			printWarning("Connection %d closed\n", client_socket);
			return false; // connection closed
		}

		len = rc;
	} while (true);

	if (len >= MAX_BUFFER_SIZE) len = MAX_BUFFER_SIZE - 1;

	recv_buffer[len] = '\0';

	sendToAll(send_buffer, client_socket, server_socket, fds, nfds, recv_buffer);

	return true;
}

int main(int argc, char** argv)
{
	Result result = SUCCESS;

	int server_socket = 0;
	result = initServer(&server_socket);
	CHECK_RESULT(result);

	bool end_server = false, compress_array = false;
	const int timeout = (6 * 60 * 1000); // 6 min
	int    nfds = 1, current_size = 0;
	struct pollfd fds[MAX_CLIENTS + 1] = { 0 }; // +1 for server
	fds[0].fd = server_socket;
	fds[0].events = POLLIN;

	char send_buffer[MAX_BUFFER_SIZE] = { 0 };
	char recv_buffer[MAX_BUFFER_SIZE] = { 0 };

	do {
		int rc = poll(fds, nfds, timeout);

		if (rc < 0)
		{
			result = ERROR_POLL_FAIL;
			break;
		}

		if (rc == 0)
		{
			result = ERROR_POLL_TIMEOUT;
			break;
		}

		current_size = nfds;
		for (int i = 0; i < current_size; i++) {
			if(fds[i].revents == 0)
				continue;

			if(fds[i].revents != POLLIN)
			{
				result = ERROR_POLL_REVENTS;
				end_server = true;
				break;
			}

			if (fds[i].fd == server_socket) { // the shit is a new connection
				if (!acceptConnection(send_buffer, server_socket, fds, &nfds)) {
					result = ERROR_SERVER_ACCEPT;
					end_server = true;
					break;
				}
			} else {
				if (!handleConnection(send_buffer, recv_buffer, fds[i].fd, server_socket, fds, nfds)) {
					close(fds[i].fd);
					fds[i].fd = -1;
					compress_array = true;
				}
			}
		}

		if (compress_array) {
			compress_array = false;
			for (int i = 0; i < nfds; i++) {
				if (fds[i].fd == -1) {
					for(int j = i; j < nfds - 1; j++) {
						fds[j].fd = fds[j + 1].fd;
					}
					i--;
					nfds--;
				}
			}
		}
	} while (!end_server);

	for (int i = 0; i < nfds; i++) {
		if(fds[i].fd >= 0)
			close(fds[i].fd);
	}

	CHECK_RESULT(result);
}
