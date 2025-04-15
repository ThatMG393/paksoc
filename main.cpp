#include "logging.hpp"
#include "packets.hpp"
#include "sockets.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <linux/in.h>
#include <sstream>
#include <string>

class PlayerData : public Serializeable {
public:
	std::string name;
	int x, y, z;
	int pitch, yaw, roll;
	int extra;
	unsigned long long int large_data;

	PlayerData() { }
	PlayerData(
		std::string name,
		int x, int y, int z,
		int pitch, int yaw, int roll
	) : name(name), x(x), y(y), z(z), pitch(pitch), yaw(yaw), roll(roll) { }

	void deserialize(Packet& p) override {
		p.read(name)->read(x)->read(y)->read(z)->rpad(4)->read(pitch)->read(yaw)->read(roll)->read(extra)->read(large_data);
	}

	void serialize(Packet& p) override {
		p.write(name)->write(x)->write(y)->write(z)->wpad(4)->write(pitch)->write(yaw)->write(roll)->write(extra)->write(large_data);
	}

	bool operator==(const PlayerData& other) {
		return (
			name == other.name &&
			x == other.x &&
			y == other.y &&
			z == other.z &&
			pitch == other.pitch &&
			yaw == other.yaw &&
			roll == other.roll
		);
	}
};

std::string to_hex(bytearray data) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');

    std::for_each(data.begin(), data.end(), [&](auto x) { ss << static_cast<int>(x); });

    return ss.str();
}

int main() {
	PlayerData pd("Hello", 6, 9, 4, 2, 0, 6);

	ServerSocket servsock(69420);
	servsock.init();

	std::this_thread::sleep_for(std::chrono::seconds(1));

	ClientSocket clisock(69421);
	clisock.init();
	clisock.on(1, [&](Packet packet) {
		PlayerData pd1;
		pd1.deserialize(packet);

		Logging::get_logger()->info("Recieved bytes:");
		std::stringstream ss;
		for (const auto& data : packet.get_data()) {
			ss << " " << std::to_string(data);
		}
		Logging::get_logger()->info(ss.str());

		assert(pd == pd1);

		clisock.disconnect();
	});

	clisock.connect(servsock.get_socket_info());

	std::this_thread::sleep_for(std::chrono::seconds(5));
	Logging::get_logger()->info("Sending data to ALL clients...");

	Logging::get_logger()->info("Sending packet with type of 1");

	servsock.send_packet_to_all(Packet(1, pd));

	std::this_thread::sleep_for(std::chrono::seconds(10));
	return 0;
}
