// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "auth_protocol/auth_protocol.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"
#include "binary_io/container_source.h"

using namespace mmo;


// This test ensures that packet header encryption is working as expected.
TEST_CASE("AuthPacketCheck", "[auth_protocol]")
{
	// Setup a buffer
	std::vector<char> buffer;

	// Create a sink object to write to that buffer
	io::VectorSink sink{ buffer };

	// These are some values that are serialized and deserialized, so we save them as constants
	// for easier checks and more readability
	const uint8 opCode = auth::client_login_packet::LogonChallenge;
	const std::string testString = "test";
	const uint32 uint32Test = 0xfafafafa;
	const float floatTest = 1.0f;

	// This is the expected packet size in bytes (only the packet content, without the header)
	const uint32 expectedPacketSize = sizeof(uint32) + sizeof(float) + sizeof(uint8) + testString.length();

	// Setup a packet and write data
	auth::OutgoingPacket p{ sink };
	p.Start(opCode);
	p << io::write<uint32>(uint32Test);
	p << io::write<float>(floatTest);
	p << io::write_dynamic_range<uint8>(testString);
	p.Finish();

	// Flush sink to ensure everything is serialized properly
	sink.Flush();

	// Now verify that the buffer contents are as expected
	CHECK(buffer[0] == opCode);
	CHECK(*((uint32*)(&buffer[1])) == expectedPacketSize);
	CHECK(*((uint32*)(&buffer[5])) == uint32Test);
	CHECK(*((float*)(&buffer[9])) == floatTest);
	CHECK(buffer[13] == testString.length());
	CHECK(std::string(buffer.begin() + 14, buffer.end()) == testString);

	// Now create an incoming packet using the same buffer
	io::MemorySource src{ buffer };
	auth::IncomingPacket incomingPacket;

	// Start parsing the packet
	CHECK(auth::IncomingPacket::Start(incomingPacket, src) == ReceiveState::Complete);
	CHECK(incomingPacket.GetId() == opCode);
	CHECK(incomingPacket.GetSize() == expectedPacketSize);
	
	// Read data from the packet and ensure that there is no error while reading
	uint32 tmpUint32 = 0;
	float tmpFloat = 0.0f;
	std::string tmpString;
	CHECK(incomingPacket >> io::read<uint32>(tmpUint32));
	CHECK(incomingPacket >> io::read<float>(tmpFloat));
	CHECK(incomingPacket >> io::read_container<uint8>(tmpString));

	// Verify the read values
	CHECK(tmpUint32 == uint32Test);
	CHECK(tmpFloat == floatTest);
	CHECK(tmpString == testString);
}
