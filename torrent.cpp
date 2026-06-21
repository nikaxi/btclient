// torrent.cpp
#include "torrent.h"
#include "bencode.hpp"
#include <algorithm>

using namespace bencode;


static std::pair<size_t, size_t> find_info_range(const std::vector<uint8_t>& data) {
    // 查找 "4:info"
    std::string key = "4:info";
    auto it = std::search(data.begin(), data.end(), key.begin(), key.end());
    if (it == data.end()) {
        throw std::runtime_error("Cannot find 'info' key in torrent");
    }
    
    size_t start = (it - data.begin()) + key.size();
    
    // 确保后面跟着 'd' (dictionary)
    if (data[start] != 'd') {
        throw std::runtime_error("'info' is not a dictionary");
    }
    
    // 解析 bencode 以找到匹配的 'e'
    int depth = 1;
    size_t i = start + 1;
    while (i < data.size() && depth > 0) {
        char c = data[i];
        if (c == 'd' || c == 'l') {
            depth++;
        } else if (c == 'e') {
            depth--;
        } else if (std::isdigit(c)) {
            // 跳过字符串
            size_t len = 0;
            while (i < data.size() && std::isdigit(data[i])) {
                len = len * 10 + (data[i] - '0');
                i++;
            }
            if (i < data.size() && data[i] == ':') {
                i++; // skip ':'
                i += len; // skip content
            }
            continue; // don't increment i again at end of loop
        }
        i++;
    }
    
    if (depth != 0) {
        throw std::runtime_error("Malformed info dictionary");
    }
    
    return {start, i}; // end is exclusive
}

Torrent::Torrent(std::ifstream &f_stream) {
    // 1. 读取整个文件到内存
    f_stream.seekg(0, std::ios::end);
    std::streamsize size = f_stream.tellg();
    f_stream.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> buffer(size);
    if (!f_stream.read(reinterpret_cast<char*>(buffer.data()), size)) {
        throw std::runtime_error("Failed to read torrent file");
    }
    
    // 2. 计算 Info Hash (从原始字节)
    try {
        auto [start, end] = find_info_range(buffer);
        SHA1(buffer.data() + start, end - start, info_hash.data());
    } catch (const std::exception& e) {
        std::cerr << "Warning: Failed to calculate info_hash: " << e.what() << std::endl;
        // 你可以选择抛出异常或保留全0哈希
        std::fill(info_hash.begin(), info_hash.end(), 0);
    }

    // 3. 从内存流中解码 Bencode
    std::istringstream iss(std::string(buffer.begin(), buffer.end()));
    auto data = bencode::decode(iss);
    auto dict = std::get<bencode::dict>(data);

    announce = std::get<bencode::string>(dict["announce"]);

    auto info_dic = std::get<bencode::dict>(dict["info"]);

    info.piece_length = std::get<bencode::integer>(info_dic["piece length"]);
    info.length = std::get<bencode::integer>(info_dic["length"]);
    info.name = std::get<bencode::string>(info_dic["name"]);

    auto pieces_data = std::get<bencode::string>(info_dic["pieces"]);

    std::vector<std::byte> v(pieces_data.size());
    std::transform(pieces_data.begin(), pieces_data.end(), v.begin(), [](char c) {
            return static_cast<std::byte>(c);
    });
    
    info.pieces = std::move(v);
}