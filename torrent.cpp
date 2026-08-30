// torrent.cpp
#include "torrent.h"
#include "bencode.hpp"
#include <algorithm>
#include <openssl/sha.h>
#include "utils.h"

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

    auto data = bencode::decode(f_stream);
    auto dict = std::get<bencode::dict>(data);

    announce = std::get<bencode::string>(dict["announce"]);

    auto info_dic = std::get<bencode::dict>(dict["info"]);


    // setup Info
    info.piece_length = std::get<bencode::integer>(info_dic["piece length"]);
    // 多文件没有length字段，只有files字段
    info.length = std::get<bencode::integer>(info_dic["length"]);
    info.name = std::get<bencode::string>(info_dic["name"]);
    auto pieces_data = std::get<bencode::string>(info_dic["pieces"]);
    std::vector<std::uint8_t> v(pieces_data.size());
    std::transform(pieces_data.begin(), pieces_data.end(), v.begin(), [](char c) {
            return static_cast<std::uint8_t>(c);
    });   

    info.pieces = std::move(split_pieces(v));

    std::cout << "name:" << info.name << std::endl;
    std::cout << "length:" << info.length << std::endl;
    std::cout << "piece_length:" << info.piece_length << std::endl;

    set_hash(info_dic);
}

void Torrent::set_hash(bencode::dict &info_dict) {
    auto encoded_info = bencode::encode(info_dict);
    SHA1(reinterpret_cast<const unsigned char*>(encoded_info.data()), encoded_info.size(), 
                         reinterpret_cast<unsigned char*> (this->info_hash.data()));
}