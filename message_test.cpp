#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <arpa/inet.h>
#include <sstream>

#include "message.h"

/**
 * @brief Helper function to convert a 4-byte sequence in network byte order to host uint32_t
 */
static uint32_t bytes_to_host_uint32(const std::byte* data) {
    uint32_t net_val = 0;
    std::memcpy(&net_val, data, 4);
    return ntohl(net_val);
}

class MessageTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// -----------------------------------------------------------------------------
// Tests for Message::format_request
// -----------------------------------------------------------------------------

TEST_F(MessageTest, FormatRequest_ZeroValues) {
    uint32_t piece_index = 0;
    size_t begin = 0;
    size_t length = 0;

    Message msg = Message::format_request(piece_index, begin, length);

    EXPECT_EQ(msg.get_type(), MsgType::MsgRequest);
    
    const auto& payload = msg.get_payload();
    ASSERT_EQ(payload.size(), 12u);

    for (const auto& b : payload) {
        EXPECT_EQ(b, std::byte{0x00});
    }
}

TEST_F(MessageTest, FormatRequest_TypicalValues) {
    uint32_t piece_index = 1;
    size_t begin = 16384;
    size_t length = 16384;

    Message msg = Message::format_request(piece_index, begin, length);

    EXPECT_EQ(msg.get_type(), MsgType::MsgRequest);
    
    const auto& payload = msg.get_payload();
    ASSERT_EQ(payload.size(), 12u);

    EXPECT_EQ(bytes_to_host_uint32(&payload[0]), piece_index);
    EXPECT_EQ(bytes_to_host_uint32(&payload[4]), static_cast<uint32_t>(begin));
    EXPECT_EQ(bytes_to_host_uint32(&payload[8]), static_cast<uint32_t>(length));
}

TEST_F(MessageTest, FormatRequest_NetworkByteOrder) {
    uint32_t piece_index = 0x01020304;
    size_t begin = 0x05060708;
    size_t length = 0x090A0B0C;

    Message msg = Message::format_request(piece_index, begin, length);
    
    const auto& payload = msg.get_payload();
    ASSERT_EQ(payload.size(), 12u);

    // Check piece_index bytes (Big Endian)
    EXPECT_EQ(payload[0], std::byte{0x01});
    EXPECT_EQ(payload[1], std::byte{0x02});
    EXPECT_EQ(payload[2], std::byte{0x03});
    EXPECT_EQ(payload[3], std::byte{0x04});

    // Check begin bytes
    EXPECT_EQ(payload[4], std::byte{0x05});
    EXPECT_EQ(payload[5], std::byte{0x06});
    EXPECT_EQ(payload[6], std::byte{0x07});
    EXPECT_EQ(payload[7], std::byte{0x08});

    // Check length bytes
    EXPECT_EQ(payload[8], std::byte{0x09});
    EXPECT_EQ(payload[9], std::byte{0x0A});
    EXPECT_EQ(payload[10], std::byte{0x0B});
    EXPECT_EQ(payload[11], std::byte{0x0C});
}

TEST_F(MessageTest, FormatRequest_MaxValues) {
    uint32_t piece_index = 0xFFFFFFFF;
    size_t begin = 0xFFFFFFFF;
    size_t length = 0xFFFFFFFF;

    Message msg = Message::format_request(piece_index, begin, length);
    
    const auto& payload = msg.get_payload();
    ASSERT_EQ(payload.size(), 12u);

    for (const auto& b : payload) {
        EXPECT_EQ(b, std::byte{0xFF});
    }
}

// -----------------------------------------------------------------------------
// Tests for Message::format_have
// -----------------------------------------------------------------------------

TEST_F(MessageTest, FormatHave_Basic) {
    uint32_t piece_index = 42;
    
    Message msg = Message::format_have(piece_index);

    EXPECT_EQ(msg.get_type(), MsgType::MsgHave);
    
    const auto& payload = msg.get_payload();
    ASSERT_EQ(payload.size(), 4u);

    EXPECT_EQ(bytes_to_host_uint32(&payload[0]), piece_index);
}

TEST_F(MessageTest, FormatHave_ByteOrder) {
    uint32_t piece_index = 0xAABBCCDD;
    
    Message msg = Message::format_have(piece_index);
    
    const auto& payload = msg.get_payload();
    ASSERT_EQ(payload.size(), 4u);
    
    EXPECT_EQ(payload[0], std::byte{0xAA});
    EXPECT_EQ(payload[1], std::byte{0xBB});
    EXPECT_EQ(payload[2], std::byte{0xCC});
    EXPECT_EQ(payload[3], std::byte{0xDD});
}

// -----------------------------------------------------------------------------
// Tests for Message Class Methods (id_str, to_string)
// -----------------------------------------------------------------------------

TEST_F(MessageTest, IdStrConversion) {
    EXPECT_EQ(Message(MsgType::MsgChoke, {}).id_str(), "choke");
    EXPECT_EQ(Message(MsgType::MsgUnchoke, {}).id_str(), "unchoke");
    EXPECT_EQ(Message(MsgType::MsgInterested, {}).id_str(), "interested");
    EXPECT_EQ(Message(MsgType::MsgNotInterested, {}).id_str(), "not interested");
    EXPECT_EQ(Message(MsgType::MsgHave, {}).id_str(), "have");
    EXPECT_EQ(Message(MsgType::MsgBitfield, {}).id_str(), "bitfield");
    EXPECT_EQ(Message(MsgType::MsgRequest, {}).id_str(), "request");
    EXPECT_EQ(Message(MsgType::MsgPiece, {}).id_str(), "piece");
    EXPECT_EQ(Message(MsgType::MsgCancel, {}).id_str(), "cancel");
    EXPECT_EQ(Message(MsgType::MsgKeepAlive, {}).id_str(), "keepAlive");
}

TEST_F(MessageTest, ToStringOutput) {
    std::vector<std::byte> payload = { std::byte{0xAB}, std::byte{0xCD} };
    Message msg(MsgType::MsgHave, payload);
    
    std::string str = msg.to_string();
    
    // Verify it contains the type string
    EXPECT_NE(str.find("have"), std::string::npos);
    
    // Verify it contains hex values (std::hex usually outputs lowercase)
    EXPECT_NE(str.find("ab"), std::string::npos);
    EXPECT_NE(str.find("cd"), std::string::npos);
}