#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <vector>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <mutex>
#include <format>
#include <openssl/sha.h>
#include <iomanip>
#include "bencode.hpp"

using namespace bencode;

inline std::string encode_binary(std::array<std::uint8_t, 20> &data) {
   std::string result;
   
   for (auto &byte : data) {
      result += std::format("{:02x}", byte);
   }
   return result;
}

inline std::array<uint8_t, 20> generate_info_hash(std::string &file_path) { 
    std::ifstream file(file_path, std::ios::binary);

    auto data = bencode::decode(file);
    auto dict = std::get<bencode::dict>(data); 
    // auto info_data = std::get<bencode::dict>(dict["info"]);
    // info_data is a bencode::data object, we need to encode it back to binary form
    const std::string info_data_bytes = bencode::encode(dict["info"]);
    // info_data_bytes is a std::string, we can use its data() and size() to get the raw bytes for SHA1
    std::array<uint8_t, 20> info_hash;
    SHA1(reinterpret_cast<const unsigned char*>(info_data_bytes.data()), info_data_bytes.size(), info_hash.data());
    // 

    return info_hash;
}
inline std::array<std::uint8_t, 20> generate_peer_id() {
    std::array<std::uint8_t, 20> peer_id;
    for (int i = 0; i < 20; i++) {
        peer_id[i] = static_cast<std::uint8_t>(rand() % 256);
    }
    return peer_id;
}




inline std::string parse_announce_url(const std::string &url) {
    std::string host; 
    // 1. 查找协议分隔符 "://"
    size_t proto_pos = url.find("://");
    if (proto_pos == std::string::npos) {
        // 如果没有协议，假设从开头开始
        proto_pos = 0;
    } else {
        // 包含协议部分 (http://)
        proto_pos += 3; 
    }

    // 2. 查找路径分隔符 '/' (从协议之后开始找)
    size_t path_pos = url.find('/', proto_pos);

    if (path_pos != std::string::npos) {
        // 提取 Host: 从开头到第一个 '/' 之前
        host = url.substr(0, path_pos);
    } else {
        // 没有路径，整个剩余部分都是 Host
        host = url;
    }

    return host;
}

inline std::string url_encode(const std::vector<std::uint8_t>& data) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (auto c : data) {
        // 标准 URL 编码：所有非字母数字字符都编码，或者为了安全起见，编码所有字节
        // 对于二进制数据（如 hash），通常编码所有字节
        escaped << '%' << std::setw(2) << static_cast<int>(c);
    }

    return escaped.str();
}


#endif