#ifndef MSG_H
#define MSG_H
#include <vector>
#include <cstddef>
#include <string>
#include <cstdint>

enum class MsgType : int {
    MsgChoke = 0,
    MsgUnchoke = 1,
    MsgInterested = 2,
    MsgNotInterested = 3,
    MsgHave = 4,
    MsgBitfield = 5,
    MsgRequest = 6,
    MsgPiece = 7,
    MsgCancel = 8,
    MsgKeepAlive = 9,
};


template <typename Enum> 
constexpr auto to_underlying(Enum e) noexcept {
    return static_cast<std::underlying_type_t<Enum>>(e);
}


class Message {
    private:
    MsgType id;
    std::vector<std::byte> payload;
    public:
    Message(MsgType id_, std::vector<std::byte> payload_):
        id(id_), payload(payload_)
    {
    }
    std::string to_string() const;
    std::string id_str() const;
    MsgType get_type() const {
        return id;
    }
    const std::vector<std::byte>& get_payload() const {
        return payload;
    }
    static Message format_request(u_int32_t idx, size_t begin, size_t length);
    static Message format_have(u_int32_t piece_index);

    static Message parse(u_int32_t idx, std::vector<std::byte>&bytes, Message& msg);
};


#endif