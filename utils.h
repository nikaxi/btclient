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
    // const std::string info_data_bytes = bencode::encode(dict["info"]);
    // info_data_bytes is a std::string, we can use its data() and size() to get the raw bytes for SHA1
    // std::array<uint8_t, 20> info_hash;
    // SHA1(reinterpret_cast<const unsigned char*>(info_data_bytes.data()), info_data_bytes.size(), info_hash.data());
    // 

    const std::string info_str = bencode::encode(dict["info"]);
    std::array<uint8_t, 20> info_hash;
    SHA1(reinterpret_cast<const unsigned char*>(info_str.data()), info_str.size(), info_hash.data());
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
        // 包含协议部分 (.*://)
        proto_pos += 3; 
    }

    // 2. 查找路径分隔符 '/' (从协议之后开始找)
    size_t path_pos = url.find('/', proto_pos);

    if (path_pos != std::string::npos) {
        // 提取 Host: 从开头到第一个 '/' 之前
        host = url.substr(proto_pos, path_pos - proto_pos);
    } else {
        // 没有路径，整个剩余部分都是 Host
        host = url.substr(proto_pos);
    }

    return host;
}

// inline std::string url_encode(const std::vector<std::uint8_t>& data) {
//     // 预分配内存，避免 std::string 动态扩容
//     std::string result;
//     result.reserve(data.size() * 3); 
    
//     // 十六进制字符查找表（大写，符合 BT 规范）
//     static const char hex_chars[] = "0123456789ABCDEF";

//     for (auto byte : data) {
//         result.push_back('%');
//         result.push_back(hex_chars[(byte >> 4) & 0x0F]); // 取高 4 位
//         result.push_back(hex_chars[byte & 0x0F]);        // 取低 4 位
//     }
//     return result;
// }

inline std::string url_encode(const std::vector<std::uint8_t>& data) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (auto c : data) {
        escaped << '%' << std::setw(2) << static_cast<int>(c);
    }
    return escaped.str();
}

// 将 pieces 字节序列拆分为每个 piece 的哈希值，并存储在 map 中
inline std::map<std::uint32_t, std::vector<std::uint8_t>> split_pieces(const std::vector<std::uint8_t> &pieces) {
    std::map<std::uint32_t, std::vector<std::uint8_t>> pieces_map;
    size_t num_pieces = pieces.size() / 20; // 每个 piece 的哈希值为 20 字节
    for (size_t i = 0; i < num_pieces; ++i) {
        std::vector<std::uint8_t> piece_hash(pieces.begin() + i * 20, pieces.begin() + (i + 1) * 20);
        pieces_map[static_cast<std::uint32_t>(i)] = piece_hash;
    }
    return pieces_map;
}

#endif