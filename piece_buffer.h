#include <openssl/sha.h> // 需要链接 OpenSSL 库 (-lssl -lcrypto)

// 假设你有一个 Piece 缓存类
class PieceBuffer {
private:
    uint32_t piece_index;
    uint32_t piece_length;
    std::vector<uint8_t> buffer;
    std::vector<uint8_t> expected_hash; // 从 info 字典中解析出的 20 字节哈希

public:
    PieceBuffer(uint32_t index, uint32_t length, const std::vector<uint8_t>& hash)
        : piece_index(index), piece_length(length), expected_hash(hash) {
        buffer.resize(length, 0); // 预分配整个 Piece 的内存
    }

    // 将收到的 Block 写入内存缓存
    void write_block(uint32_t begin, const uint8_t* data, uint32_t length) {
        if (begin + length <= piece_length) {
            std::memcpy(buffer.data() + begin, data, length);
        }
    }

    // 校验整个 Piece 是否完整且正确
    bool verify_and_save() {
        uint8_t calculated_hash[20];
        SHA1(buffer.data(), buffer.size(), calculated_hash);

        // 比对 20 字节的哈希值
        if (std::memcmp(calculated_hash, expected_hash.data(), 20) == 0) {
            std::cout << "[校验成功] Piece #" << piece_index << " 验证通过，准备写入磁盘！" << std::endl;
            // TODO: 调用 std::ofstream 将 buffer 写入本地文件的对应偏移量
            // TODO: 向其他所有 Peer 发送 Have 消息 (ID=4)
            return true;
        } else {
            std::cerr << "[校验失败] Piece #" << piece_index << " 数据损坏或被投毒！" << std::endl;
            return false;
        }
    }
};