#include "bencode.hpp"
#include <iomanip>
#include <ctime>
#include <sstream>
#include <filesystem>
#include <openssl/sha.h>   // 需要链接 -lcrypto
#include <fstream>

// 把任意 bencode::data 打印成人类可读的形式
static std::string data_to_string(const bencode::data& d);

// 计算 info_hash：对 "info" 字典的原始 bencode 字节做 SHA-1
static std::array<uint8_t, 20> compute_info_hash(
    const bencode::data& root
) {
    // 重新 encode info 字典再 sha1（bencode::encode 返回 std::string）
    std::string encoded = bencode::encode(root.at("info"));

    std::array<uint8_t, 20> hash{};
    SHA1(reinterpret_cast<const uint8_t*>(encoded.data()),
         encoded.size(), hash.data());
    return hash;
}

static std::string hex_hash(const std::array<uint8_t, 20>& h) {
    std::ostringstream oss;
    for (auto b : h) oss << std::hex << std::setw(2) << std::setfill('0')
                         << (int)b;
    return oss.str();
}

int main(int argc, char* argv[]) {
    if (argc < 2) { std::cerr << "usage: " << argv[0] << " <file.torrent>\n"; return 1; }

    // 1. 读取原始文件
    std::string raw = [&] {
        std::ifstream ifs(argv[1], std::ios::binary);
        return std::string{std::istreambuf_iterator<char>(ifs), {}};
    }();

    // 2. 解析
    bencode::data root = bencode::decode(raw);

    // ===== 提取顶层字段 =====
    auto get_str = [&](const std::string& key) -> std::string {
        auto it = std::get_if<bencode::dict>(&root.base());
        if (!it) return {};
        auto vit = it->find(key);
        if (vit == it->end()) return {};
        if (auto* s = std::get_if<bencode::string>(&vit->second.base()))
            return *s;
        return {};
    };

    auto get_int = [&](const std::string& key) -> long long {
        auto it = std::get_if<bencode::dict>(&root.base());
        if (!it) return 0;
        auto vit = it->find(key);
        if (vit == it->end()) return 0;
        if (auto* i = std::get_if<bencode::integer>(&vit->second.base()))
            return *i;
        return 0;
    };

    // 基本信息
    std::string announce   = get_str("announce");
    std::string comment    = get_str("comment");
    std::string created_by = get_str("created by");
    long long  creation_date = get_int("creation date");

    // ===== info 字典 =====
    const auto* info_dict = std::get_if<bencode::dict>(&root.at("info").base());
    if (!info_dict) { std::cerr << "missing info dict\n"; return 1; }

    std::string name;
    if (auto* s = std::get_if<bencode::string>(&info_dict->at("name").base()))
        name = *s;

    long long piece_length = 0;
    if (auto* i = std::get_if<bencode::integer>(&info_dict->at("piece length").base()))
        piece_length = *i;

    // pieces 是一个连续的二进制字符串，每 20 字节一个 SHA-1 hash
    const auto* pieces_str = std::get_if<bencode::string>(&info_dict->at("pieces").base());
    long long num_pieces = pieces_str ? pieces_str->size() / 20 : 0;

    // ===== 文件大小 & 模式 =====
    long long total_length = 0;
    std::string mode = "single";

    if (info_dict->find("length") != info_dict->end()) {
        // 单文件模式
        if (auto* i = std::get_if<bencode::integer>(&info_dict->at("length").base()))
            total_length = *i;
    } else if (info_dict->find("files") != info_dict->end()) {
        // 多文件模式
        mode = "multi";
        const auto* files = std::get_if<bencode::list>(&info_dict->at("files").base());
        if (files) {
            for (auto& f : *files) {
                const auto* fd = std::get_if<bencode::dict>(&f.base());
                if (fd && fd->find("length") != fd->end()) {
                    if (auto* i = std::get_if<bencode::integer>(&fd->at("length").base()))
                        total_length += *i;
                }
            }
        }
    }

    // ===== info_hash =====
    auto info_hash = compute_info_hash(root);

    // ===== 打印 =====
    std::cout << "*** BitTorrent File Information ***\n";
    if (!comment.empty())    std::cout << "Comment: " << comment << '\n';
    if (creation_date) {
        std::time_t t = creation_date;
        char buf[64];
        std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", std::gmtime(&t));
        std::cout << "Creation Date: " << buf << '\n';
    }
    if (!created_by.empty()) std::cout << "Created By: " << created_by << '\n';
    std::cout << "Mode: " << mode << '\n';
    std::cout << "Announce:\n  " << announce << '\n';
    std::cout << "Info Hash: " << hex_hash(info_hash) << '\n';

    // 格式化 piece length
    if (piece_length >= 1048576)
        std::cout << "Piece Length: " << (piece_length / 1048576) << "MiB\n";
    else if (piece_length >= 1024)
        std::cout << "Piece Length: " << (piece_length / 1024) << "KiB\n";

    std::cout << "The Number of Pieces: " << num_pieces << '\n';

    // 格式化总大小
    auto fmt_size = [](long long sz) -> std::string {
        std::ostringstream o;
        if (sz >= (1LL << 30))
            o << std::fixed << std::setprecision(1) << (sz / (double)(1LL << 30)) << "GiB";
        else if (sz >= (1 << 20))
            o << (sz >> 20) << "MiB";
        else
            o << (sz >> 10) << "KiB";
        o << " (" << sz << ')';
        return o.str();
    };
    std::cout << "Total Length: " << fmt_size(total_length) << '\n';

    // URL list
    if (auto* ul = std::get_if<bencode::list>(&root.at("url-list").base())) {
        std::cout << "URL List:\n";
        for (auto& u : *ul)
            if (auto* s = std::get_if<bencode::string>(&u.base()))
                std::cout << "  " << *s << '\n';
    }

    std::cout << "Name: " << name << '\n';

    // Magnet URI
    std::string magnet = "magnet:?xt=urn:btih:" + hex_hash(info_hash).substr(0, 40)
                       + "&dn=" + name;
    if (!announce.empty()) {
        // URL encode announce
        std::string enc_announce;
        for (char c : announce) {
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
                enc_announce += c;
            else {
                char buf[4];
                snprintf(buf, sizeof(buf), "%%%02X", (uint8_t)c);
                enc_announce += buf;
            }
        }
        magnet += "&tr=" + enc_announce;
    }
    std::cout << "Magnet URI: " << magnet << '\n';

    // ===== 文件列表 =====
    std::cout << "Files:\nidx|path/length\n";
    std::cout << "===+================================================\n";
    int idx = 1;
    if (mode == "single") {
        std::cout << std::setw(3) << idx << "|./" << name << '\n'
                  << "   |" << fmt_size(total_length) << '\n';
    } else {
        const auto* files = std::get_if<bencode::list>(&info_dict->at("files").base());
        if (files) {
            for (auto& f : *files) {
                const auto* fd = std::get_if<bencode::dict>(&f.base());
                if (!fd) continue;
                // path
                std::string path;
                if (auto* pl = std::get_if<bencode::list>(&fd->at("path").base())) {
                    for (auto& p : *pl) {
                        if (auto* s = std::get_if<bencode::string>(&p.base())) {
                            if (!path.empty()) path += '/';
                            path += *s;
                        }
                    }
                }
                long long flen = 0;
                if (auto* i = std::get_if<bencode::integer>(&fd->at("length").base()))
                    flen = *i;
                std::cout << std::setw(3) << idx++ << "|./" << path << '\n'
                          << "   |" << fmt_size(flen) << '\n';
            }
        }
    }

    return 0;
}