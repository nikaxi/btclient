#include "client.h"
#include "peer.h"
#include <random>
#include "httplib.h"
#include "utils.h"
#include <fstream>
#include "torrent.h"
#include "bencode.hpp"
#include "utils.h"
using namespace bencode;

void Client::set_bit(int idx) {
    auto byte_idx = idx / 8;
    auto bit_idx = idx % 8;
    bit_field[byte_idx] |= static_cast<std::byte>(1 <<(7 -  bit_idx));
}

bool Client::is_bit_set(int idx) {
    auto byte_idx = idx / 8;
    auto bit_idx = idx % 8;
    if (byte_idx >= bit_field.size()) {
        return false;
    }
    return static_cast<int>(bit_field[byte_idx] & static_cast<std::byte>(1 <<(7 -  bit_idx))) != 0;
}


std::vector<std::string> Client::get_peers(Torrent &torrent) {
    std::vector<std::string> peers_vec;
    // send request to announce_url
    std::string host = parse_announce_url(torrent.announce);
    httplib::Client cli(host);

    for(auto &byte: info_hash){
        std::cout << static_cast<int>(byte) << " ";
    }


    std::string url = "/announce?compact=1&info_hash=" + url_encode(std::vector(info_hash.begin(),info_hash.end()))
    + "&peer_id=" + url_encode(std::vector(peer_id.begin(), peer_id.end())) 
    + "&port=" + std::to_string(PORT) 
    + "&uploaded=0&event=started&downloaded=0&left=" 
    + std::to_string(torrent.info.length);

    std::cout << "URL: " << url << std::endl;

    cli.set_follow_location(true); // follow redirects
    httplib::Result res = cli.Get(url);

    std::cout << "Status code: " << res->status << std::endl;
    std::cout << "Body size: " << res->body.size() << std::endl;

    if (res && res->status == 200) {
        auto decoded = bencode::decode(res->body);
        auto peers = std::get<bencode::string>(decoded["peers"]);
        for (auto i = 0; i < peers.size(); i += 6) {
            std::array<std::byte, 4> ip;
            std::array<std::byte, 2> port;
            for (auto j = 0; j < 4; j++) {
                ip[j] = static_cast<std::byte>(peers[i + j]);
            }
            for (auto j = 0; j < 2; j++) {
                port[j] = static_cast<std::byte>(peers[i + j + 4]);
            }
            // convert to string 
            peers_vec.push_back(Peer(ip, port).to_string());
        }
    } else {
        std::cout << "Error: " << res.error() << std::endl;
    }

    return peers_vec;

}

bool Client::request_piece(int index, std::vector<std::uint8_t> &piece_hash) {
    // 这里可以实现请求 piece 的逻辑
    // 例如，建立与 peer 的连接，发送请求消息，接收数据等
    // 目前只是一个占位符，返回 true 表示请求成功
    std::cout << "Requesting piece index: " << index << std::endl;
    // 构造请求
    
    return true;
}

vec_u8 Client::recv_bitfield() {
    return bit_field;
}






