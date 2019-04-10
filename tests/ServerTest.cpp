#include "ServerTest.h"
#include <algorithm>
#include <boost/algorithm/string.hpp>

using ::testing::Matches;
using ::testing::Return;
using ::testing::_;
using ::testing::SetArgPointee;
using ::testing::Contains;

namespace Thing {
	namespace CoAP {
		namespace Tests {
			ACTION_TEMPLATE(CopyBuffer, HAS_2_TEMPLATE_PARAMS(int, a, int, b), AND_2_VALUE_PARAMS(pointer, length))
			{
				const void* bufferData = (void*)::std::tr1::get<a>(args);
				const void* lengthData = (void*)::std::tr1::get<b>(args);

				uint8_t* buffer = new uint8_t[(int)lengthData];
				std::memcpy(buffer, bufferData, (int)lengthData);

				*pointer = buffer;
				*length = (int)lengthData;
			}

			MATCHER_P(ContentFormat, contentformat, "")
			{
				Thing::CoAP::Option value = (Thing::CoAP::Option)arg;

				if (value.GetNumber() != Thing::CoAP::OptionValue::ContentFormat)
					return false;

				uint32_t aux = 0;
				for (int i = 0; i < value.GetLenght(); ++i)
				{
					aux = aux << 8;
					aux |= value.GetBuffer()[i];
				}
				return aux == static_cast<uint32_t>(contentformat);
			}

			MATCHER_P3(Option, optionNumber, payload, payloadSize, "")
			{
				Thing::CoAP::Option value = (Thing::CoAP::Option)arg;

				if (value.GetNumber() != optionNumber)
					return false;

				if (!payload && payloadSize == value.GetLenght() && value.GetLenght() == 0)
					return true;

				if (!payload)
					return false;

				if (0 == std::memcmp(payload, value.GetBuffer(), value.GetLenght()))
					return true;

				return false;
			}

			void ServerTest::SetUp()
			{
				Server.SetPacketProvider(udpProviderMock);
			}

			void ServerTest::TearDown()
			{
				EXPECT_CALL(udpProviderMock, Stop()).Times(1);
				Server.Stop();
			}

			TEST_F(ServerTest, PortTest)
			{
				const int ports[] = { 1, 2, 3 };
				for (int i = 0; i < sizeof(ports) / sizeof(int); ++i)
				{
					Server.SetPort(ports[i]);
					EXPECT_EQ(ports[i], Server.GetPort());

					EXPECT_CALL(udpProviderMock, Start(ports[i])).Times(1);
					Server.Start();
				}
			}

			TEST_F(ServerTest, NoUdpProvider)
			{
				Server.SetPacketProvider(NULL);
				EXPECT_CALL(udpProviderMock, ReadPacket(_, _, _, _)).Times(0);
				Server.Process();
				Server.SetPacketProvider(udpProviderMock);
			}

#pragma region Simple Endpoint Tests
			void ServerTest::SimpleEndpointTest(Thing::CoAP::Method method)
			{
				const std::string endpointPath = "Endpoint";
				const uint8_t tokens[] = { 0x01, 0x02, 0x03, 0x04 };
				const uint16_t messageID = 0x1234;
				const Thing::CoAP::IPAddress requestIPAddress = 0x01010101;
				const int requestPort = 1234;
				const uint8_t version = 1;
				const Thing::CoAP::ContentFormat contentFormat = Thing::CoAP::ContentFormat::ApplicationJSon;
				const Thing::CoAP::MessageType messageType = Thing::CoAP::MessageType::NonConfirmable;

				CoAPEndpointMock endpoint;
				EXPECT_CALL(endpoint, GetEndpoint()).WillRepeatedly(Return(endpointPath));
				EXPECT_CALL(endpoint, GetContentFormat()).WillRepeatedly(Return(contentFormat));
				EXPECT_CALL(endpoint, Get(_)).Times(method == Thing::CoAP::Method::Get ? 1 : 0).WillOnce(Return(Thing::CoAP::Status::Ok()));
				EXPECT_CALL(endpoint, Post(_)).Times(method == Thing::CoAP::Method::Post ? 1 : 0).WillOnce(Return(Thing::CoAP::Status::Ok()));
				EXPECT_CALL(endpoint, Put(_)).Times(method == Thing::CoAP::Method::Put ? 1 : 0).WillOnce(Return(Thing::CoAP::Status::Ok()));
				EXPECT_CALL(endpoint, Delete(_)).Times(method == Thing::CoAP::Method::Delete ? 1 : 0).WillOnce(Return(Thing::CoAP::Status::Ok()));

				Thing::CoAP::Packet request;
				request.SetVersion(version);
				request.SetTokens(std::vector<uint8_t>(tokens, tokens + sizeof(tokens)));
				request.SetMessageID(messageID);
				request.SetType(messageType);
				std::vector<Thing::CoAP::Option> options;
				Thing::CoAP::Option urlPath;
				urlPath.SetNumber(Thing::CoAP::OptionValue::URIPath);
				urlPath.SetOption((uint8_t*)endpointPath.c_str(), endpointPath.size());
				options.push_back(urlPath);
				request.SetOptions(options);
				request.SetCode(method);

				Server.AddEndpoint(endpoint);
				Server.Start();

				uint8_t* requestBuffer;
				int requestLength;

				request.SerializePacket(&requestBuffer, &requestLength);
				EXPECT_CALL(udpProviderMock, ReadPacket(_, _, _, _))
					.Times(1)
					.WillOnce(DoAll(SetArgPointee<0>(requestBuffer), SetArgPointee<1>(requestLength), SetArgPointee<2>(requestIPAddress), SetArgPointee<3>(requestPort), Return(true)));

				EXPECT_CALL(udpProviderMock, DestroyPacket(requestBuffer)).Times(1);

				uint8_t* responseBuffer;
				int responseLength;
				EXPECT_CALL(udpProviderMock, SendPacket(_, _, requestIPAddress, requestPort)).WillOnce(CopyBuffer<0, 1>(&responseBuffer, &responseLength));

				Server.Process();

				Thing::CoAP::Packet response;
				response.DesserializePacket(responseBuffer, responseLength);

				EXPECT_EQ(version, response.GetVersion());
				EXPECT_EQ(messageID, response.GetMessageID());
				EXPECT_EQ(messageType, response.GetType());
				EXPECT_EQ(static_cast<int>(Thing::CoAP::ResponseCode::Ok), static_cast<int>(response.GetCode()));
				EXPECT_EQ(request.GetTokens().size(), response.GetTokens().size());
				EXPECT_TRUE(0 == std::memcmp(&request.GetTokens()[0], &response.GetTokens()[0], response.GetTokens().size()));

				std::vector<Thing::CoAP::Option>& responseOptions = response.GetOptions();
				EXPECT_THAT(responseOptions, ::testing::Contains(ContentFormat(contentFormat)));
				EXPECT_EQ(0, response.GetPayloadLength());

				delete[] responseBuffer;
				delete[] requestBuffer;
			}

			TEST_F(ServerTest, SimpleEndpointGetTesting)
			{
				SimpleEndpointTest(Thing::CoAP::Method::Get);
			}

			TEST_F(ServerTest, SimpleEndpointPostTesting)
			{
				SimpleEndpointTest(Thing::CoAP::Method::Post);
			}

			TEST_F(ServerTest, SimpleEndpointPutTesting)
			{
				SimpleEndpointTest(Thing::CoAP::Method::Put);
			}

			TEST_F(ServerTest, SimpleEndpointDeleteTesting)
			{
				SimpleEndpointTest(Thing::CoAP::Method::Delete);
			}
#pragma endregion

#pragma region Endpoint Not Found Tests
			void ServerTest::EndpointNotFoundTest(Thing::CoAP::Method method)
			{
				const std::string endpointPath = "EndpointNotExist";
				const uint8_t tokens[] = { 0x01, 0x02, 0x03, 0x04 };
				const uint16_t messageID = 0x1234;
				const Thing::CoAP::IPAddress requestIPAddress = 0x01010101;
				const int requestPort = 1234;
				const uint8_t version = 1;
				const Thing::CoAP::ContentFormat contentFormat = Thing::CoAP::ContentFormat::ApplicationJSon;
				const Thing::CoAP::MessageType messageType = Thing::CoAP::MessageType::NonConfirmable;

				Thing::CoAP::Packet request;
				request.SetVersion(version);
				request.SetTokens(std::vector<uint8_t>(tokens, tokens + sizeof(tokens)));
				request.SetMessageID(messageID);
				request.SetType(messageType);
				std::vector<Thing::CoAP::Option> options;
				Thing::CoAP::Option urlPath;
				urlPath.SetNumber(Thing::CoAP::OptionValue::URIPath);
				urlPath.SetOption((uint8_t*)endpointPath.c_str(), endpointPath.size());
				options.push_back(urlPath);
				request.SetOptions(options);
				request.SetCode(method);

				Server.Start();

				uint8_t* requestBuffer;
				int requestLength;

				request.SerializePacket(&requestBuffer, &requestLength);
				EXPECT_CALL(udpProviderMock, ReadPacket(_, _, _, _))
					.Times(1)
					.WillOnce(DoAll(SetArgPointee<0>(requestBuffer), SetArgPointee<1>(requestLength), SetArgPointee<2>(requestIPAddress), SetArgPointee<3>(requestPort), Return(true)));

				EXPECT_CALL(udpProviderMock, DestroyPacket(requestBuffer)).Times(1);

				uint8_t* responseBuffer;
				int responseLength;
				EXPECT_CALL(udpProviderMock, SendPacket(_, _, requestIPAddress, requestPort)).WillOnce(CopyBuffer<0, 1>(&responseBuffer, &responseLength));

				Server.Process();

				Thing::CoAP::Packet response;
				response.DesserializePacket(responseBuffer, responseLength);

				EXPECT_EQ(version, response.GetVersion());
				EXPECT_EQ(messageID, response.GetMessageID());
				EXPECT_EQ(messageType, response.GetType());
				EXPECT_EQ(static_cast<int>(Thing::CoAP::ResponseCode::NotFound), static_cast<int>(response.GetCode()));
				EXPECT_EQ(request.GetTokens().size(), response.GetTokens().size());
				EXPECT_TRUE(0 == std::memcmp(&request.GetTokens()[0], &response.GetTokens()[0], response.GetTokens().size()));

				std::vector<Thing::CoAP::Option>& responseOptions = response.GetOptions();
				EXPECT_EQ(0, responseOptions.size());
				EXPECT_EQ(0, response.GetPayloadLength());

				delete[] responseBuffer;
				delete[] requestBuffer;
			}

			TEST_F(ServerTest, NoEndpointFoundGetTesting)
			{
				EndpointNotFoundTest(Thing::CoAP::Method::Get);
			}

			TEST_F(ServerTest, NoEndpointFoundPostTesting)
			{
				EndpointNotFoundTest(Thing::CoAP::Method::Post);
			}

			TEST_F(ServerTest, NoEndpointFoundPutTesting)
			{
				EndpointNotFoundTest(Thing::CoAP::Method::Put);
			}
			TEST_F(ServerTest, NoEndpointFoundDeleteTesting)
			{
				EndpointNotFoundTest(Thing::CoAP::Method::Delete);
			}
#pragma endregion

			void ServerTest::MultipleEndpointTest(CoAPEndpointMock* endpoint, int size, int callIndex, Thing::CoAP::Method method)
			{
				const std::string endpointPath = endpoint[callIndex].GetEndpoint();
				const uint8_t tokens[] = { 0x01, 0x02, 0x03, 0x04 };
				const uint16_t messageID = 0x1234;
				const Thing::CoAP::IPAddress requestIPAddress = 0x01010101;
				const int requestPort = 1234;
				const uint8_t version = 1;
				const Thing::CoAP::ContentFormat contentFormat = Thing::CoAP::ContentFormat::ApplicationJSon;
				const Thing::CoAP::MessageType messageType = Thing::CoAP::MessageType::NonConfirmable;

				for (int i = 0; i < size; ++i)
				{
					EXPECT_CALL(endpoint[i], Get(_)).Times(method == Thing::CoAP::Method::Get ? (callIndex == i ? 1 : 0) : 0).WillOnce(Return(Thing::CoAP::Status::Ok()));
					EXPECT_CALL(endpoint[i], Post(_)).Times(method == Thing::CoAP::Method::Post ? (callIndex == i ? 1 : 0) : 0).WillOnce(Return(Thing::CoAP::Status::Ok()));
					EXPECT_CALL(endpoint[i], Put(_)).Times(method == Thing::CoAP::Method::Put ? (callIndex == i ? 1 : 0) : 0).WillOnce(Return(Thing::CoAP::Status::Ok()));
					EXPECT_CALL(endpoint[i], Delete(_)).Times(method == Thing::CoAP::Method::Delete ? (callIndex == i ? 1 : 0) : 0).WillOnce(Return(Thing::CoAP::Status::Ok()));
				}

				Thing::CoAP::Packet request;
				request.SetVersion(version);
				request.SetTokens(std::vector<uint8_t>(tokens, tokens + sizeof(tokens)));
				request.SetMessageID(messageID);
				request.SetType(messageType);
				std::vector<Thing::CoAP::Option> options;
				Thing::CoAP::Option urlPath;
				urlPath.SetNumber(Thing::CoAP::OptionValue::URIPath);
				urlPath.SetOption((uint8_t*)endpointPath.c_str(), endpointPath.size());
				options.push_back(urlPath);
				request.SetOptions(options);
				request.SetCode(method);

				uint8_t* requestBuffer;
				int requestLength;

				request.SerializePacket(&requestBuffer, &requestLength);
				EXPECT_CALL(udpProviderMock, ReadPacket(_, _, _, _))
					.Times(1)
					.WillOnce(DoAll(SetArgPointee<0>(requestBuffer), SetArgPointee<1>(requestLength), SetArgPointee<2>(requestIPAddress), SetArgPointee<3>(requestPort), Return(true)));

				EXPECT_CALL(udpProviderMock, DestroyPacket(requestBuffer)).Times(1);

				uint8_t* responseBuffer;
				int responseLength;
				EXPECT_CALL(udpProviderMock, SendPacket(_, _, requestIPAddress, requestPort)).WillOnce(CopyBuffer<0, 1>(&responseBuffer, &responseLength));

				Server.Process();

				Thing::CoAP::Packet response;
				response.DesserializePacket(responseBuffer, responseLength);

				EXPECT_EQ(version, response.GetVersion());
				EXPECT_EQ(messageID, response.GetMessageID());
				EXPECT_EQ(messageType, response.GetType());
				EXPECT_EQ(static_cast<int>(Thing::CoAP::ResponseCode::Ok), static_cast<int>(response.GetCode()));
				EXPECT_EQ(request.GetTokens().size(), response.GetTokens().size());
				EXPECT_TRUE(0 == std::memcmp(&request.GetTokens()[0], &response.GetTokens()[0], response.GetTokens().size()));

				std::vector<Thing::CoAP::Option>& responseOptions = response.GetOptions();
				EXPECT_THAT(responseOptions, ::testing::Contains(ContentFormat(contentFormat)));
				EXPECT_EQ(0, response.GetPayloadLength());

				delete[] responseBuffer;
				delete[] requestBuffer;
			}

			TEST_F(ServerTest, MultipleEndpointTest)
			{
				const int totalEndpoints = 5;
				CoAPEndpointMock endpoint[totalEndpoints];

				const std::string endpointPath = "Endpoint";
				const Thing::CoAP::ContentFormat contentFormat = Thing::CoAP::ContentFormat::ApplicationJSon;

				for (int i = 0; i < totalEndpoints; ++i)
				{
					EXPECT_CALL(endpoint[i], GetEndpoint()).WillRepeatedly(Return(endpointPath + std::to_string(i)));
					EXPECT_CALL(endpoint[i], GetContentFormat()).WillRepeatedly(Return(contentFormat));
					Server.AddEndpoint(endpoint[i]);
				}
				Server.Start();

				for (int i = 0; i < totalEndpoints; ++i)
					MultipleEndpointTest(endpoint, totalEndpoints, i, Thing::CoAP::Method::Get);
			}

#pragma region Resource Discovery Test
			void ServerTest::ResourceDiscoveryTest(uint8_t** responseBuffer, int* responseLength)
			{
				const std::string endpointPath = ".well-known/core";
				const uint8_t tokens[] = { 0x01, 0x02, 0x03, 0x04 };
				const uint16_t messageID = 0x1234;
				const Thing::CoAP::IPAddress requestIPAddress = 0x01010101;
				const int requestPort = 1234;
				const uint8_t version = 1;
				const Thing::CoAP::ContentFormat contentFormat = Thing::CoAP::ContentFormat::ApplicationLinkFormat;
				const Thing::CoAP::MessageType messageType = Thing::CoAP::MessageType::NonConfirmable;

				Thing::CoAP::Packet request;
				request.SetVersion(version);
				request.SetTokens(std::vector<uint8_t>(tokens, tokens + sizeof(tokens)));
				request.SetMessageID(messageID);
				request.SetType(messageType);
				std::vector<Thing::CoAP::Option> options;
				Thing::CoAP::Option urlPath;
				urlPath.SetNumber(Thing::CoAP::OptionValue::URIPath);
				urlPath.SetOption((uint8_t*)endpointPath.c_str(), endpointPath.size());
				options.push_back(urlPath);
				request.SetOptions(options);
				request.SetCode(Thing::CoAP::Method::Get);

				Server.Start();

				uint8_t* requestBuffer;
				int requestLength;

				request.SerializePacket(&requestBuffer, &requestLength);
				EXPECT_CALL(udpProviderMock, ReadPacket(_, _, _, _))
					.Times(1)
					.WillOnce(DoAll(SetArgPointee<0>(requestBuffer), SetArgPointee<1>(requestLength), SetArgPointee<2>(requestIPAddress), SetArgPointee<3>(requestPort), Return(true)));

				EXPECT_CALL(udpProviderMock, DestroyPacket(requestBuffer)).Times(1);
				EXPECT_CALL(udpProviderMock, SendPacket(_, _, requestIPAddress, requestPort)).WillOnce(CopyBuffer<0, 1>(responseBuffer, responseLength));

				Server.Process();

				Thing::CoAP::Packet response;
				response.DesserializePacket(*responseBuffer, *responseLength);

				EXPECT_EQ(version, response.GetVersion());
				EXPECT_EQ(messageID, response.GetMessageID());
				EXPECT_EQ(messageType, response.GetType());
				EXPECT_EQ(static_cast<int>(Thing::CoAP::ResponseCode::Content), static_cast<int>(response.GetCode()));
				EXPECT_EQ(request.GetTokens().size(), response.GetTokens().size());
				EXPECT_TRUE(0 == std::memcmp(&request.GetTokens()[0], &response.GetTokens()[0], response.GetTokens().size()));

				std::vector<Thing::CoAP::Option>& responseOptions = response.GetOptions();
				EXPECT_THAT(responseOptions, ::testing::Contains(ContentFormat(contentFormat)));

				delete[] requestBuffer;
			}

			TEST_F(ServerTest, ResourceDiscoveryEmptyTest)
			{
				uint8_t* responseBuffer;
				int responseLength;
				ResourceDiscoveryTest(&responseBuffer, &responseLength);

				Thing::CoAP::Packet response;
				response.DesserializePacket(responseBuffer, responseLength);

				EXPECT_EQ(0, response.GetPayloadLength());

				delete[] responseBuffer;
			}

			TEST_F(ServerTest, ResourceDiscoverySimpleTest)
			{
				const std::string endpointName = "Endpoint";
				const Thing::CoAP::ContentFormat contentFormat = Thing::CoAP::ContentFormat::ApplicationJSon;
				const bool isObservable = true;
				CoAPEndpointMock endpoint;

				EXPECT_CALL(endpoint, GetEndpoint()).WillRepeatedly(Return(endpointName));
				EXPECT_CALL(endpoint, GetContentFormat()).WillRepeatedly(Return(contentFormat));
				EXPECT_CALL(endpoint, IsObservable()).WillRepeatedly(Return(isObservable));

				Server.AddEndpoint(endpoint);

				uint8_t* responseBuffer;
				int responseLength;
				ResourceDiscoveryTest(&responseBuffer, &responseLength);

				Thing::CoAP::Packet response;
				response.DesserializePacket(responseBuffer, responseLength);

				EXPECT_NE(0, response.GetPayloadLength());

				std::string payload = std::string(reinterpret_cast<char*>(response.GetPayload()), response.GetPayloadLength());

				std::vector<std::string> resources;
				boost::split(resources, payload, boost::is_any_of(","));
				EXPECT_EQ(1, resources.size());

				std::vector<std::string> informations;
				boost::split(informations, resources[0], boost::is_any_of(";"));
				EXPECT_EQ("</" + endpointName + ">", informations[0]);

				EXPECT_THAT(informations, Contains("ct=" + std::to_string(static_cast<int>(contentFormat))));
				delete[] responseBuffer;
			}

			TEST_F(ServerTest, ResourceDiscoveryMultipleTest)
			{
				const int totalEndpoints = 10;
				const std::string endpointBaseName = "Endpoint";
				const Thing::CoAP::ContentFormat contentFormat = Thing::CoAP::ContentFormat::ApplicationJSon;
				CoAPEndpointMock endpoint[totalEndpoints];

				for (int i = 0; i < totalEndpoints; ++i)
				{
					EXPECT_CALL(endpoint[i], GetEndpoint()).WillRepeatedly(Return(endpointBaseName + std::to_string(i)));
					EXPECT_CALL(endpoint[i], GetContentFormat()).WillRepeatedly(Return(contentFormat));
					EXPECT_CALL(endpoint[i], IsObservable()).WillRepeatedly(Return(i % 2));

					Server.AddEndpoint(endpoint[i]);
				}

				uint8_t* responseBuffer;
				int responseLength;
				ResourceDiscoveryTest(&responseBuffer, &responseLength);

				Thing::CoAP::Packet response;
				response.DesserializePacket(responseBuffer, responseLength);

				EXPECT_NE(0, response.GetPayloadLength());

				std::string payload = std::string(reinterpret_cast<char*>(response.GetPayload()), response.GetPayloadLength());

				std::vector<std::string> resources;
				boost::split(resources, payload, boost::is_any_of(","));
				EXPECT_EQ(totalEndpoints, resources.size());

				for (int i = 0; i < totalEndpoints; ++i)
				{
					std::vector<std::string> informations;
					boost::split(informations, resources[i], boost::is_any_of(";"));
					EXPECT_EQ("</" + endpoint[i].GetEndpoint() + ">", informations[0]);

					EXPECT_THAT(informations, Contains("ct=" + std::to_string(static_cast<int>(contentFormat))));
				}
				delete[] responseBuffer;
			}

#pragma endregion

#pragma region Observers Test
			void ServerTest::ObserveTest(bool observe, Thing::CoAP::IPAddress requestIPAddress, int requestPort, std::vector<uint8_t> tokens, std::string endpoint, uint8_t** responseBuffer, int* responseLength)
			{
				const std::string endpointPath = endpoint;
				const uint16_t messageID = 0x1234;
				const uint8_t version = 1;
				const Thing::CoAP::ContentFormat contentFormat = Thing::CoAP::ContentFormat::ApplicationJSon;
				const Thing::CoAP::MessageType messageType = Thing::CoAP::MessageType::NonConfirmable;


				Thing::CoAP::Packet request;
				request.SetVersion(version);
				request.SetTokens(tokens);
				request.SetMessageID(messageID);
				request.SetType(messageType);
				std::vector<Thing::CoAP::Option> options;

				Thing::CoAP::Option observeOption;
				observeOption.SetNumber(Thing::CoAP::OptionValue::Observe);
				if (!observe)
				{
					uint8_t payload[1] = { 1 };
					observeOption.SetOption(payload, 1);
				}
				options.push_back(observeOption);

				Thing::CoAP::Option urlPath;
				urlPath.SetNumber(Thing::CoAP::OptionValue::URIPath);
				urlPath.SetOption((uint8_t*)endpointPath.c_str(), endpointPath.size());
				options.push_back(urlPath);

				request.SetOptions(options);
				request.SetCode(Thing::CoAP::Method::Get);
				uint8_t* requestBuffer;
				int requestLength;

				request.SerializePacket(&requestBuffer, &requestLength);

				Server.SetPacketProvider(udpProviderMock);
				EXPECT_CALL(udpProviderMock, ReadPacket(_, _, _, _))
					.Times(1)
					.WillOnce(DoAll(SetArgPointee<0>(requestBuffer), SetArgPointee<1>(requestLength), SetArgPointee<2>(requestIPAddress), SetArgPointee<3>(requestPort), Return(true)));

				EXPECT_CALL(udpProviderMock, DestroyPacket(requestBuffer)).Times(1);

				Server.Process();
			}

			TEST_F(ServerTest, ObserverRequestSimpleTest)
			{
				const std::string endpointPath = "Endpoint";
				const Thing::CoAP::ContentFormat contentFormat = Thing::CoAP::ContentFormat::ApplicationJSon;
				const Thing::CoAP::IPAddress requestIPAddress = 0x01010101;
				const int requestPort = 1234;
				const uint8_t tokens[] = { 0x01, 0x02, 0x03, 0x04 };
				const std::string observePacketPayload = "Observe Test";
				const int totalNotifications = 5;

				CoAPEndpointMock endpoint;
				EXPECT_CALL(endpoint, GetEndpoint()).WillRepeatedly(Return(endpointPath));
				EXPECT_CALL(endpoint, GetContentFormat()).WillRepeatedly(Return(contentFormat));
				EXPECT_CALL(endpoint, Get(_)).WillRepeatedly(Return(Thing::CoAP::Status::Ok(observePacketPayload)));
				Server.AddEndpoint(endpoint);

				Server.Start();

				uint8_t* responseBuffer = NULL;
				int responseLength = 0;
				ObserveTest(true, requestIPAddress, requestPort, std::vector<uint8_t>(tokens, tokens + sizeof(tokens)), endpointPath, &responseBuffer, &responseLength);

				EXPECT_EQ(NULL, responseBuffer);
				EXPECT_EQ(0, responseLength);
				delete[] responseBuffer;

				for (int i = 0; i < totalNotifications; ++i)
				{
					uint8_t* observeBuffer;
					int observeBufferLength;
					EXPECT_CALL(udpProviderMock, SendPacket(_, _, requestIPAddress, requestPort)).Times(1).WillOnce(CopyBuffer<0, 1>(&observeBuffer, &observeBufferLength));
					Server.NotifyObservers(endpoint, Thing::CoAP::Status::Ok(observePacketPayload));

					Thing::CoAP::Packet observePacket;
					observePacket.DesserializePacket(observeBuffer, observeBufferLength);

					EXPECT_EQ(sizeof(tokens), observePacket.GetTokens().size());
					EXPECT_TRUE(0 == std::memcmp(tokens, &observePacket.GetTokens()[0], sizeof(tokens)));

					EXPECT_EQ(Thing::CoAP::MessageType::Confirmable, observePacket.GetType());
					EXPECT_EQ(static_cast<int>(Thing::CoAP::ResponseCode::Content), static_cast<int>(observePacket.GetCode()));
					std::vector<Thing::CoAP::Option>& responseOptions = observePacket.GetOptions();
					EXPECT_EQ(2, responseOptions.size());

					EXPECT_THAT(responseOptions, Contains(Option(Thing::CoAP::OptionValue::URIPath, (uint8_t*)endpointPath.c_str(), endpointPath.size())));
					EXPECT_THAT(responseOptions, Contains(ContentFormat(Thing::CoAP::ContentFormat::ApplicationJSon)));

					ASSERT_EQ(observePacket.GetPayloadLength(), observePacketPayload.size());
					EXPECT_TRUE(0 == std::memcmp(observePacket.GetPayload(), observePacketPayload.c_str(), observePacketPayload.size()));
					delete[] observeBuffer;
				}
			}

			TEST_F(ServerTest, MultipleObserversTest)
			{
				const std::string endpointPath = "Endpoint";
				const Thing::CoAP::ContentFormat contentFormat = Thing::CoAP::ContentFormat::ApplicationJSon;
				const int totalObservers = 10;
				const Thing::CoAP::IPAddress requestIPAddress = 0x01010100;
				const int requestPort = 1200;
				uint8_t tokens[] = { 0x00, 0x02, 0x03, 0x04 };
				const std::string observePacketPayload = "Observe Test";
				const int totalNotifications = 5;

				CoAPEndpointMock endpoint;
				EXPECT_CALL(endpoint, GetEndpoint()).WillRepeatedly(Return(endpointPath));
				EXPECT_CALL(endpoint, GetContentFormat()).WillRepeatedly(Return(contentFormat));
				EXPECT_CALL(endpoint, Get(_)).WillRepeatedly(Return(Thing::CoAP::Status::Ok(observePacketPayload)));
				Server.AddEndpoint(endpoint);

				Server.Start();

				for (int i = 0; i < totalObservers; ++i)
				{
					uint8_t* responseBuffer = NULL;
					int responseLength = 0;
					std::vector<uint8_t> packetTokens;
					for (int j = 0; j < sizeof(tokens); ++j)
						packetTokens.push_back(tokens[j]);
					packetTokens[0] = i;
					ObserveTest(true, requestIPAddress | (uint8_t)i, requestPort | (uint8_t)i, packetTokens, endpointPath, &responseBuffer, &responseLength);

					EXPECT_EQ(NULL, responseBuffer);
					EXPECT_EQ(0, responseLength);
					delete[] responseBuffer;
				}

				std::vector<std::vector<uint8_t> > receivedTokens;
				uint8_t* observeBuffer[totalObservers];
				int observeBufferLength[totalObservers];
				for (int i = 0; i < totalNotifications; ++i)
				{

					for (int j = 0; j < totalObservers; ++j)
						EXPECT_CALL(udpProviderMock, SendPacket(_, _, requestIPAddress | (uint8_t)j, requestPort | (uint8_t)j)).Times(1).WillOnce(CopyBuffer<0, 1>(&observeBuffer[j], &observeBufferLength[j]));
					Server.NotifyObservers(endpoint, Thing::CoAP::Status::Ok(observePacketPayload));

					for (int j = 0; j < totalObservers; ++j)
					{
						Thing::CoAP::Packet observePacket;
						observePacket.DesserializePacket(observeBuffer[j], observeBufferLength[j]);

						receivedTokens.push_back(observePacket.GetTokens());

						EXPECT_EQ(Thing::CoAP::MessageType::Confirmable, observePacket.GetType());
						EXPECT_EQ(static_cast<int>(Thing::CoAP::ResponseCode::Content), static_cast<int>(observePacket.GetCode()));
						std::vector<Thing::CoAP::Option>& responseOptions = observePacket.GetOptions();
						EXPECT_EQ(2, responseOptions.size());

						EXPECT_THAT(responseOptions, Contains(Option(Thing::CoAP::OptionValue::URIPath, (uint8_t*)endpointPath.c_str(), endpointPath.size())));
						EXPECT_THAT(responseOptions, Contains(ContentFormat(Thing::CoAP::ContentFormat::ApplicationJSon)));

						ASSERT_EQ(observePacket.GetPayloadLength(), observePacketPayload.size());
						EXPECT_TRUE(0 == std::memcmp(observePacket.GetPayload(), observePacketPayload.c_str(), observePacketPayload.size()));

						delete[] observeBuffer[j];
					}
				}

				for (int i = 0; i < totalObservers; ++i)
				{
					tokens[0] = i;
					std::vector<uint8_t> test(tokens, tokens + sizeof(tokens));
					EXPECT_THAT(receivedTokens, Contains(test));
				}
			}

			TEST_F(ServerTest, MultipleEndpointsObserverTest)
			{
				const std::string endpointPath = "Endpoint";
				const Thing::CoAP::ContentFormat contentFormat = Thing::CoAP::ContentFormat::ApplicationJSon;
				const Thing::CoAP::IPAddress requestIPAddress = 0x01010101;
				const int requestPort = 1234;
				const uint8_t tokens[] = { 0x01, 0x02, 0x03, 0x04 };
				const std::string observePacketPayload = "Observe Test";
				const int totalNotifications = 5;
				const int totalEndpoints = 10;

				CoAPEndpointMock endpoint[totalEndpoints];

				for (int i = 0; i < totalEndpoints; ++i)
				{
					EXPECT_CALL(endpoint[i], GetEndpoint()).WillRepeatedly(Return(endpointPath + std::to_string(i)));
					EXPECT_CALL(endpoint[i], GetContentFormat()).WillRepeatedly(Return(contentFormat));
					EXPECT_CALL(endpoint[i], Get(_)).WillRepeatedly(Return(Thing::CoAP::Status::Ok(observePacketPayload)));
					Server.AddEndpoint(endpoint[i]);
				}
				Server.Start();

				for (int i = 0; i < totalEndpoints; ++i)
				{
					if (i % 2 == 0)
						continue;

					uint8_t* responseBuffer = NULL;
					int responseLength = 0;
					ObserveTest(true, requestIPAddress, requestPort, std::vector<uint8_t>(tokens, tokens + sizeof(tokens)), endpoint[i].GetEndpoint(), &responseBuffer, &responseLength);

					EXPECT_EQ(NULL, responseBuffer);
					EXPECT_EQ(0, responseLength);
					delete[] responseBuffer;
				}

				for (int j = 0; j < totalEndpoints; ++j)
					for (int i = 0; i < totalNotifications; ++i)
					{
						uint8_t* observeBuffer;
						int observeBufferLength;
						EXPECT_CALL(udpProviderMock, SendPacket(_, _, requestIPAddress, requestPort)).Times(j % 2 == 0 ? 0 : 1).WillOnce(CopyBuffer<0, 1>(&observeBuffer, &observeBufferLength));
						Server.NotifyObservers(endpoint[j], Thing::CoAP::Status::Ok(observePacketPayload));
						if (j % 2 == 0)
							continue;

						Thing::CoAP::Packet observePacket;
						observePacket.DesserializePacket(observeBuffer, observeBufferLength);

						EXPECT_EQ(sizeof(tokens), observePacket.GetTokens().size());
						EXPECT_TRUE(0 == std::memcmp(tokens, &observePacket.GetTokens()[0], sizeof(tokens)));

						EXPECT_EQ(Thing::CoAP::MessageType::Confirmable, observePacket.GetType());
						EXPECT_EQ(static_cast<int>(Thing::CoAP::ResponseCode::Content), static_cast<int>(observePacket.GetCode()));
						std::vector<Thing::CoAP::Option>& responseOptions = observePacket.GetOptions();
						EXPECT_EQ(2, responseOptions.size());

						EXPECT_THAT(responseOptions, Contains(Option(Thing::CoAP::OptionValue::URIPath, (uint8_t*)endpoint[j].GetEndpoint().c_str(), endpoint[j].GetEndpoint().size())));
						EXPECT_THAT(responseOptions, Contains(ContentFormat(Thing::CoAP::ContentFormat::ApplicationJSon)));

						ASSERT_EQ(observePacket.GetPayloadLength(), observePacketPayload.size());
						EXPECT_TRUE(0 == std::memcmp(observePacket.GetPayload(), observePacketPayload.c_str(), observePacketPayload.size()));
						delete[] observeBuffer;
					}
			}

			TEST_F(ServerTest, ObserveRemoveSimpleTest)
			{
				const std::string endpointPath = "Endpoint";
				const Thing::CoAP::ContentFormat contentFormat = Thing::CoAP::ContentFormat::ApplicationJSon;
				const Thing::CoAP::IPAddress requestIPAddress = 0x01010101;
				const int requestPort = 1234;
				const uint8_t tokens[] = { 0x01, 0x02, 0x03, 0x04 };
				const std::string observePacketPayload = "Observe Test";

				CoAPEndpointMock endpoint;
				EXPECT_CALL(endpoint, GetEndpoint()).WillRepeatedly(Return(endpointPath));
				EXPECT_CALL(endpoint, GetContentFormat()).WillRepeatedly(Return(contentFormat));
				EXPECT_CALL(endpoint, Get(_)).WillRepeatedly(Return(Thing::CoAP::Status::Ok(observePacketPayload)));
				Server.AddEndpoint(endpoint);

				Server.Start();

				uint8_t* responseBuffer = NULL;
				int responseLength = 0;
				ObserveTest(true, requestIPAddress, requestPort, std::vector<uint8_t>(tokens, tokens + sizeof(tokens)), endpointPath, &responseBuffer, &responseLength);

				EXPECT_EQ(NULL, responseBuffer);
				EXPECT_EQ(0, responseLength);
				delete[] responseBuffer;

				uint8_t* observeBuffer;
				int observeBufferLength;
				EXPECT_CALL(udpProviderMock, SendPacket(_, _, requestIPAddress, requestPort)).Times(1).WillOnce(CopyBuffer<0, 1>(&observeBuffer, &observeBufferLength));
				Server.NotifyObservers(endpoint, Thing::CoAP::Status::Ok(observePacketPayload));

				Thing::CoAP::Packet observePacket;
				observePacket.DesserializePacket(observeBuffer, observeBufferLength);

				EXPECT_EQ(sizeof(tokens), observePacket.GetTokens().size());
				EXPECT_TRUE(0 == std::memcmp(tokens, &observePacket.GetTokens()[0], sizeof(tokens)));

				EXPECT_EQ(Thing::CoAP::MessageType::Confirmable, observePacket.GetType());
				EXPECT_EQ(static_cast<int>(Thing::CoAP::ResponseCode::Content), static_cast<int>(observePacket.GetCode()));
				std::vector<Thing::CoAP::Option>& responseOptions = observePacket.GetOptions();
				EXPECT_EQ(2, responseOptions.size());

				EXPECT_THAT(responseOptions, Contains(Option(Thing::CoAP::OptionValue::URIPath, (uint8_t*)endpointPath.c_str(), endpointPath.size())));
				EXPECT_THAT(responseOptions, Contains(ContentFormat(Thing::CoAP::ContentFormat::ApplicationJSon)));

				ASSERT_EQ(observePacket.GetPayloadLength(), observePacketPayload.size());
				EXPECT_TRUE(0 == std::memcmp(observePacket.GetPayload(), observePacketPayload.c_str(), observePacketPayload.size()));
				delete[] observeBuffer;


				//Remove From Observer
				EXPECT_CALL(endpoint, Get(_)).Times(1).WillOnce(Return(Thing::CoAP::Status::Ok()));
				EXPECT_CALL(udpProviderMock, SendPacket(_, _, requestIPAddress, requestPort)).Times(1);
				ObserveTest(false, requestIPAddress, requestPort, std::vector<uint8_t>(tokens, tokens + sizeof(tokens)), endpointPath, &responseBuffer, &responseLength);
				EXPECT_CALL(udpProviderMock, SendPacket(_, _, requestIPAddress, requestPort)).Times(0);
				Server.NotifyObservers(endpoint, Thing::CoAP::Status::Ok(observePacketPayload));
			}

			TEST_F(ServerTest, MultipleObserveRemoveTest)
			{
				const std::string endpointPath = "Endpoint";
				const Thing::CoAP::ContentFormat contentFormat = Thing::CoAP::ContentFormat::ApplicationJSon;
				const int totalObservers = 10;
				const Thing::CoAP::IPAddress requestIPAddress = 0x01010100;
				const int requestPort = 1200;
				uint8_t tokens[] = { 0x00, 0x02, 0x03, 0x04 };
				const std::string observePacketPayload = "Observe Test";
				const int totalNotifications = 5;

				CoAPEndpointMock endpoint;
				EXPECT_CALL(endpoint, GetEndpoint()).WillRepeatedly(Return(endpointPath));
				EXPECT_CALL(endpoint, GetContentFormat()).WillRepeatedly(Return(contentFormat));
				EXPECT_CALL(endpoint, Get(_)).WillRepeatedly(Return(Thing::CoAP::Status::Ok(observePacketPayload)));
				Server.AddEndpoint(endpoint);

				Server.Start();

				for (int i = 0; i < totalObservers; ++i)
				{
					uint8_t* responseBuffer = NULL;
					int responseLength = 0;
					std::vector<uint8_t> packetTokens;
					for (int j = 0; j < sizeof(tokens); ++j)
						packetTokens.push_back(tokens[j]);
					packetTokens[0] = i;
					ObserveTest(true, requestIPAddress | (uint8_t)i, requestPort | (uint8_t)i, packetTokens, endpointPath, &responseBuffer, &responseLength);

					EXPECT_EQ(NULL, responseBuffer);
					EXPECT_EQ(0, responseLength);
					delete[] responseBuffer;
				}

				uint8_t* observeBuffer[totalObservers];
				int observeBufferLength[totalObservers];
				for (int i = 0; i < totalNotifications; ++i)
				{

					for (int j = 0; j < totalObservers; ++j)
						EXPECT_CALL(udpProviderMock, SendPacket(_, _, requestIPAddress | (uint8_t)j, requestPort | (uint8_t)j)).Times(1).WillOnce(CopyBuffer<0, 1>(&observeBuffer[j], &observeBufferLength[j]));
					Server.NotifyObservers(endpoint, Thing::CoAP::Status::Ok(observePacketPayload));

					for (int j = 0; j < totalObservers; ++j)
					{
						Thing::CoAP::Packet observePacket;
						observePacket.DesserializePacket(observeBuffer[j], observeBufferLength[j]);

						EXPECT_EQ(Thing::CoAP::MessageType::Confirmable, observePacket.GetType());
						EXPECT_EQ(static_cast<int>(Thing::CoAP::ResponseCode::Content), static_cast<int>(observePacket.GetCode()));
						std::vector<Thing::CoAP::Option>& responseOptions = observePacket.GetOptions();
						EXPECT_EQ(2, responseOptions.size());

						EXPECT_THAT(responseOptions, Contains(Option(Thing::CoAP::OptionValue::URIPath, (uint8_t*)endpointPath.c_str(), endpointPath.size())));
						EXPECT_THAT(responseOptions, Contains(ContentFormat(Thing::CoAP::ContentFormat::ApplicationJSon)));

						ASSERT_EQ(observePacket.GetPayloadLength(), observePacketPayload.size());
						EXPECT_TRUE(0 == std::memcmp(observePacket.GetPayload(), observePacketPayload.c_str(), observePacketPayload.size()));

						delete[] observeBuffer[j];
					}
				}

				//Remove From Observer
				for (int i = 0; i < totalObservers; ++i)
				{
					uint8_t* responseBuffer = NULL;
					int responseLength = 0;

					EXPECT_CALL(endpoint, Get(_)).Times(1).WillOnce(Return(Thing::CoAP::Status::Ok()));
					EXPECT_CALL(udpProviderMock, SendPacket(_, _, requestIPAddress | (uint8_t)i, requestPort | (uint8_t)i)).Times(1);
					ObserveTest(false, requestIPAddress | (uint8_t)i, requestPort | (uint8_t)i, std::vector<uint8_t>(tokens, tokens + sizeof(tokens)), endpointPath, &responseBuffer, &responseLength);
					EXPECT_CALL(udpProviderMock, SendPacket(_, _, requestIPAddress | (uint8_t)i, requestPort | (uint8_t)i)).Times(0);
					for (int j = i + 1; j < totalObservers; ++j)
						EXPECT_CALL(udpProviderMock, SendPacket(_, _, requestIPAddress | (uint8_t)j, requestPort | (uint8_t)j)).Times(1);

					Server.NotifyObservers(endpoint, Thing::CoAP::Status::Ok(observePacketPayload));
				}
			}

			TEST_F(ServerTest, MultipleEndpointsObserveRemoveTest)
			{
				const std::string endpointPath = "Endpoint";
				const Thing::CoAP::ContentFormat contentFormat = Thing::CoAP::ContentFormat::ApplicationJSon;
				const Thing::CoAP::IPAddress requestIPAddress = 0x01010101;
				const int requestPort = 1234;
				const uint8_t tokens[] = { 0x01, 0x02, 0x03, 0x04 };
				const std::string observePacketPayload = "Observe Test";
				const int totalNotifications = 5;
				const int totalEndpoints = 10;

				CoAPEndpointMock endpoint[totalEndpoints];

				for (int i = 0; i < totalEndpoints; ++i)
				{
					EXPECT_CALL(endpoint[i], GetEndpoint()).WillRepeatedly(Return(endpointPath + std::to_string(i)));
					EXPECT_CALL(endpoint[i], GetContentFormat()).WillRepeatedly(Return(contentFormat));
					EXPECT_CALL(endpoint[i], Get(_)).WillRepeatedly(Return(Thing::CoAP::Status::Ok(observePacketPayload)));
					Server.AddEndpoint(endpoint[i]);
				}
				Server.Start();

				for (int i = 0; i < totalEndpoints; ++i)
				{
					if (i % 2 == 0)
						continue;

					uint8_t* responseBuffer = NULL;
					int responseLength = 0;
					ObserveTest(true, requestIPAddress, requestPort, std::vector<uint8_t>(tokens, tokens + sizeof(tokens)), endpoint[i].GetEndpoint(), &responseBuffer, &responseLength);

					EXPECT_EQ(NULL, responseBuffer);
					EXPECT_EQ(0, responseLength);
					delete[] responseBuffer;
				}

				for (int j = 0; j < totalEndpoints; ++j)
					for (int i = 0; i < totalNotifications; ++i)
					{
						uint8_t* observeBuffer;
						int observeBufferLength;
						EXPECT_CALL(udpProviderMock, SendPacket(_, _, requestIPAddress, requestPort)).Times(j % 2 == 0 ? 0 : 1).WillOnce(CopyBuffer<0, 1>(&observeBuffer, &observeBufferLength));
						Server.NotifyObservers(endpoint[j], Thing::CoAP::Status::Ok(observePacketPayload));
						if (j % 2 == 0)
							continue;

						Thing::CoAP::Packet observePacket;
						observePacket.DesserializePacket(observeBuffer, observeBufferLength);

						EXPECT_EQ(sizeof(tokens), observePacket.GetTokens().size());
						EXPECT_TRUE(0 == std::memcmp(tokens, &observePacket.GetTokens()[0], sizeof(tokens)));

						EXPECT_EQ(Thing::CoAP::MessageType::Confirmable, observePacket.GetType());
						EXPECT_EQ(static_cast<int>(Thing::CoAP::ResponseCode::Content), static_cast<int>(observePacket.GetCode()));
						std::vector<Thing::CoAP::Option>& responseOptions = observePacket.GetOptions();
						EXPECT_EQ(2, responseOptions.size());

						EXPECT_THAT(responseOptions, Contains(Option(Thing::CoAP::OptionValue::URIPath, (uint8_t*)endpoint[j].GetEndpoint().c_str(), endpoint[j].GetEndpoint().size())));
						EXPECT_THAT(responseOptions, Contains(ContentFormat(Thing::CoAP::ContentFormat::ApplicationJSon)));

						ASSERT_EQ(observePacket.GetPayloadLength(), observePacketPayload.size());
						EXPECT_TRUE(0 == std::memcmp(observePacket.GetPayload(), observePacketPayload.c_str(), observePacketPayload.size()));
						delete[] observeBuffer;
					}

				for (int i = 1; i < totalEndpoints; i += 2)
				{
					uint8_t* responseBuffer = NULL;
					int responseLength = 0;

					EXPECT_CALL(endpoint[i], Get(_)).Times(1).WillOnce(Return(Thing::CoAP::Status::Ok()));
					EXPECT_CALL(udpProviderMock, SendPacket(_, _, requestIPAddress, requestPort)).Times(1);
					ObserveTest(false, requestIPAddress, requestPort, std::vector<uint8_t>(tokens, tokens + sizeof(tokens)), endpoint[i].GetEndpoint(), &responseBuffer, &responseLength);
					EXPECT_CALL(udpProviderMock, SendPacket(_, _, requestIPAddress, requestPort)).Times(0);

					Server.NotifyObservers(endpoint[i], Thing::CoAP::Status::Ok(observePacketPayload));
					for (int j = i + 2; j < totalEndpoints; j += 2)
					{
						EXPECT_CALL(udpProviderMock, SendPacket(_, _, requestIPAddress, requestPort)).Times(1);
						Server.NotifyObservers(endpoint[j], Thing::CoAP::Status::Ok(observePacketPayload));
					}
				}
			}

			TEST_F(ServerTest, OneObserverMultipleRequest)
			{
				const std::string endpointPath = "Endpoint";
				const Thing::CoAP::ContentFormat contentFormat = Thing::CoAP::ContentFormat::ApplicationJSon;
				const Thing::CoAP::IPAddress requestIPAddress = 0x01010101;
				const int requestPort = 1234;
				const uint8_t tokens[] = { 0x01, 0x02, 0x03, 0x04 };
				const std::string observePacketPayload = "Observe Test";
				const int totalNotifications = 5;
				const int totalObservationRequestSent = 10;

				CoAPEndpointMock endpoint;
				EXPECT_CALL(endpoint, GetEndpoint()).WillRepeatedly(Return(endpointPath));
				EXPECT_CALL(endpoint, GetContentFormat()).WillRepeatedly(Return(contentFormat));
				EXPECT_CALL(endpoint, Get(_)).WillRepeatedly(Return(Thing::CoAP::Status::Ok(observePacketPayload)));
				Server.AddEndpoint(endpoint);

				Server.Start();

				for (int i = 0; i < totalObservationRequestSent; ++i)
				{
					uint8_t* responseBuffer = NULL;
					int responseLength = 0;
					ObserveTest(true, requestIPAddress, requestPort, std::vector<uint8_t>(tokens, tokens + sizeof(tokens)), endpointPath, &responseBuffer, &responseLength);

					EXPECT_EQ(NULL, responseBuffer);
					EXPECT_EQ(0, responseLength);
					delete[] responseBuffer;
				}

				for (int i = 0; i < totalNotifications; ++i)
				{
					uint8_t* observeBuffer;
					int observeBufferLength;
					EXPECT_CALL(udpProviderMock, SendPacket(_, _, requestIPAddress, requestPort)).Times(1).WillOnce(CopyBuffer<0, 1>(&observeBuffer, &observeBufferLength));
					Server.NotifyObservers(endpoint, Thing::CoAP::Status::Ok(observePacketPayload));

					Thing::CoAP::Packet observePacket;
					observePacket.DesserializePacket(observeBuffer, observeBufferLength);

					EXPECT_EQ(sizeof(tokens), observePacket.GetTokens().size());
					EXPECT_TRUE(0 == std::memcmp(tokens, &observePacket.GetTokens()[0], sizeof(tokens)));

					EXPECT_EQ(Thing::CoAP::MessageType::Confirmable, observePacket.GetType());
					EXPECT_EQ(static_cast<int>(Thing::CoAP::ResponseCode::Content), static_cast<int>(observePacket.GetCode()));
					std::vector<Thing::CoAP::Option>& responseOptions = observePacket.GetOptions();
					EXPECT_EQ(2, responseOptions.size());

					EXPECT_THAT(responseOptions, Contains(Option(Thing::CoAP::OptionValue::URIPath, (uint8_t*)endpointPath.c_str(), endpointPath.size())));
					EXPECT_THAT(responseOptions, Contains(ContentFormat(Thing::CoAP::ContentFormat::ApplicationJSon)));

					ASSERT_EQ(observePacket.GetPayloadLength(), observePacketPayload.size());
					EXPECT_TRUE(0 == std::memcmp(observePacket.GetPayload(), observePacketPayload.c_str(), observePacketPayload.size()));
					delete[] observeBuffer;
				}
			}
#pragma endregion
		}
	}
}