#pragma once

#include "IServer.h"
#include <string>

namespace Thing {
	namespace CoAP {
		class Observer
		{
		public:
			Observer(IPAddress ip, int port, uint16_t messageID, std::vector<uint8_t> tokens);
			Observer(IPAddress ip, int port, uint16_t messageID);
			Observer(const Observer&) = default;
			virtual ~Observer();

			IPAddress GetIPAddress() const;
			int GetPort() const;
			std::vector<uint8_t> GetTokens() const;
			uint16_t NextCount();
			uint16_t NextMessageID();

			Observer& operator=(const Observer& other)
			{
				Observer o(other);
				return o;
			}

			bool operator==(const Observer& other) const
			{
				return ip == other.ip && port == other.port;
			}
		private:
			const IPAddress ip;
			const int port;
			const std::vector<uint8_t> tokens;
			uint16_t count;
			uint16_t messageID;
		};
	}
}