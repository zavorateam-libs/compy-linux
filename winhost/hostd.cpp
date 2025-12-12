#include <iostream>
#include <fstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#define HOST_LISTEN_SOCKET "/tmp/compy_hostd.sock"
#define QEMU_VIRTIO_SOCKET "/tmp/win_comm.sock"

void send_command_to_agent(const std::string& command) {
	int qemu_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    	struct sockaddr_un qemu_addr;
	qemu_addr.sun_family = AF_UNIX;
    	strncpy(qemu_addr.sun_path, QEMU_VIRTIO_SOCKET, sizeof(qemu_addr.sun_path) - 1);

    	if (connect(qemu_fd, (struct sockaddr*)&qemu_addr, sizeof(qemu_addr)) == -1) {
        	std::cerr << "HOSTD: ERROR: Can not connect to QEMU Virtio-sock." << std::endl;
        	// Здесь должна быть логика перезапуска QEMU, если он упал
        	close(qemu_fd);
        	return;
    	}

	std::string msg = command + "\n";
    	send(qemu_fd, msg.c_str(), msg.length(), 0);
    	std::cout << "HOSTD: DEBUG: Send command to Windows agent: " << command << std::endl;

	close(qemu_fd);
}

int main(int argc, char *argv[]) {
    system("/opt/compy/winsys/hostd/run_qemu.sh"); 

    int master_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un server_addr;

    unlink(HOST_LISTEN_SOCKET);
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, HOST_LISTEN_SOCKET, sizeof(server_addr.sun_path) - 1);

    if (bind(master_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("HOSTD: ERROR: failed to bind");
        return 1;
    }

    if (listen(master_socket, 5) == -1) {
        perror("HOSTD: ERROR: failed to listen");
        return 1;
    }

    std::cout << "HOSTD: DEBUG: Daemon started. Waiting for commands on: " << HOST_LISTEN_SOCKET << std::endl;

    while (true) {
        int client_socket = accept(master_socket, NULL, NULL);
        if (client_socket == -1) {
            perror("HOSTD: ERROR: failed to accept");
            continue;
        }

        char buffer[256];
        int bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        buffer[bytes_read] = '\0';

        std::string command(buffer);
        std::cout << "HOSTD: DEBUG: Command accepted: " << command << std::endl;

        if (command.rfind("RUN ", 0) == 0) {
            send_command_to_agent(command.substr(4));
        }

        close(client_socket);
    }

    close(master_socket);
    return 0;
}
