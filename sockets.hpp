#pragma once

#include "packets.hpp"

#include <atomic>
#include <functional>
#include <linux/in.h>
#include <memory>
#include <sys/socket.h>
#include <thread>
#include <unordered_map>
#include <vector>

typedef int SocketFd;

class BaseSocket {
private:
	SocketFd socket_fd;
	sockaddr_in socket_info;

protected:
	bool is_ready;
	bool is_closed;

public:
	BaseSocket(int port);
	BaseSocket(SocketFd socket_fd, sockaddr_in socket_info);

	virtual ~BaseSocket();

	virtual void init();
	void close();

	void send_data(bytearray& data);

	bytearray recieve_data(size_t data_size, bool ensure_full = false);
	void recieve_data(bytearray& destination, bool ensure_full = false);

	const SocketFd get_socket_fd() {
		return this->socket_fd;
	}

	const sockaddr_in get_socket_info() {
		return this->socket_info;
	}

	const bool is_socket_ready() {
		return this->is_ready;
	}

	const bool is_socket_closed() {
		return this->socket_fd < 0;
	}
};

class PollingSocket : public BaseSocket {
protected:
	std::thread polling_thread;

	std::atomic_bool should_poll = ATOMIC_VAR_INIT(false);
	std::atomic_bool poll_interrupt = ATOMIC_VAR_INIT(false);

public:
	PollingSocket(int port) : BaseSocket(port) { }
	PollingSocket(SocketFd socket_fd, sockaddr_in socket_info) : BaseSocket(socket_fd, socket_info) { }
	virtual ~PollingSocket();

	void init() override;

	void start_polling() {
		if (poll_interrupt.load()) return;
		should_poll.store(true);
	}

	void interrupt_polling() {
		if (poll_interrupt.load()) return;
		should_poll.store(false);
		poll_interrupt.store(true);
	}

protected:
	virtual void poll() = 0;
};

class ServerSocket;

class ServerClientSocket : public PollingSocket {
private:
	ServerSocket* parent;

public:
	ServerClientSocket(
		ServerSocket* parent,
		SocketFd client_fd,
		sockaddr_in client_info
	) : PollingSocket(client_fd, client_info), parent(parent) {
		this->is_ready = true;
	}

	void init() override;

protected:
	void poll() override;
};

class ServerSocket : public PollingSocket {
friend class ServerClientSocket;

private:
	std::unordered_map<SocketFd, std::unique_ptr<ServerClientSocket>> clients;
	std::vector<std::function<void(ServerClientSocket*, Packet)>> packet_listeners;
	std::unordered_map<PacketType, std::vector<std::function<void(ServerClientSocket*, Packet)>>> specific_packet_listeners;

protected:
	void poll() override;

	void dispatch_packet_event(ServerClientSocket* from, Packet packet);

public:
	ServerSocket(int port);
	~ServerSocket();

	void init() override;

	void send_data_to_all(bytearray data);

	void on(std::function<void(ServerClientSocket*, Packet)> callback);
	void on(PacketType type, std::function<void(ServerClientSocket*, Packet)> callback);

	const std::unordered_map<SocketFd, std::unique_ptr<ServerClientSocket>>* get_clients() {
		return &this->clients;
	}
};

class ClientSocket : public PollingSocket {
private:
	std::vector<std::function<void(Packet)>> packet_listeners;
	std::unordered_map<PacketType, std::vector<std::function<void(Packet)>>> specific_packet_listeners;

	void dispatch_packet_event(Packet packet);

protected:
	void poll() override;

public:
	ClientSocket(int port) : PollingSocket(port) { }
	~ClientSocket() = default;

	void connect(sockaddr_in target_info);
	void connect();

	void on(std::function<void(Packet)> callback);
	void on(PacketType type, std::function<void(Packet)> callback);
};
