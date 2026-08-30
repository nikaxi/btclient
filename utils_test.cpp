#include <gtest/gtest.h>
#include "utils.h"


TEST(Utils, GenerateInfoHashFromTorrentFile) {
    std::string file_path = "/home/nikaxi/debian-12.11.0-amd64-netinst.iso.torrent";
    std::array<uint8_t, 20> info_hash = generate_info_hash(file_path);
    std::string str_hash_info = encode_binary(info_hash);
    std::string expected_info_hash = "6f4370df4304609a8793ce2b59178dcc8febf5e2";
    ASSERT_EQ(str_hash_info, expected_info_hash);
}

TEST(Utils, GeneratePeerId) {
    std::array<std::uint8_t, 20> peer_id = generate_peer_id();
    ASSERT_EQ(peer_id.size(), 20);
    // Check that the generated peer ID is not all zeros
    std::cout << "Generated Peer ID: " << encode_binary(peer_id) << std::endl;
    bool all_zeros = std::all_of(peer_id.begin(), peer_id.end(), [](std::uint8_t byte) { return byte == 0; });
    ASSERT_FALSE(all_zeros);
}


TEST(Utils, EncodeBinary) {
    std::array<std::uint8_t, 20> data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
                                         0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14};
    std::string encoded = encode_binary(data);
    std::string expected = "0102030405060708090a0b0c0d0e0f1011121314";
    ASSERT_EQ(encoded, expected);
}

TEST(Utils, ParseAnnounceUrl) {
    std::string url1 = "http://bttracker.debian.org:6969/announce";
    std::string url2 = "https://example.com:8080/path/to/resource";
    std::string url3 = "ftp://ftp.example.com/resource";
    std::string url4 = "no-protocol.com/path";

    ASSERT_EQ(parse_announce_url(url1), "bttracker.debian.org:6969");
    ASSERT_EQ(parse_announce_url(url2), "example.com:8080");
    ASSERT_EQ(parse_announce_url(url3), "ftp.example.com");
    ASSERT_EQ(parse_announce_url(url4), "no-protocol.com");
}

TEST(Utils, UrlEncode) {
    std::string input = "hello world!@#$%^&*()";
    std::string expected = "%68%65%6C%6C%6F%20%77%6F%72%6C%64%21%40%23%24%25%5E%26%2A%28%29";
    std::vector<std::uint8_t> data(input.begin(), input.end());
    ASSERT_EQ(url_encode(data), expected);
}