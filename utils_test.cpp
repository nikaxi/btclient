#include <gtest/gtest.h>
#include "utils.h"


TEST(Utils, GenerateInfoHash) {
    std::string file_path = "/home/nikaxi/debian-12.11.0-amd64-netinst.iso.torrent";
    std::array<uint8_t, 20> info_hash = generate_info_hash(file_path);
    std::string str_hash_info = encode_binary(info_hash);
    std::string expected_info_hash = "5cd344ebddcf11e98664c3c57759c0164eb805ca";
    ASSERT_EQ(str_hash_info, expected_info_hash);
    std::cout << str_hash_info << std::endl;
    std::cout << expected_info_hash << std::endl;
}