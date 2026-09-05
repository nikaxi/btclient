#include "message.h"
#include <string>
#include <array>
#include <arpa/inet.h>
#include <cstring>
#include <sstream>
#include "error.h"

std::string Message::id_str() const
{ 
    switch (this->id)
    {
    case MsgType::MsgChoke:
        return "choke";
    case MsgType::MsgUnchoke:
        return "unchoke";
    case MsgType::MsgInterested:
        return "interested";
    case MsgType::MsgNotInterested:
        return "not interested";
    case MsgType::MsgHave:
        return "have";
    case MsgType::MsgBitfield:
        return "bitfield";
    case MsgType::MsgRequest:
        return "request";
    case MsgType::MsgPiece:
        return "piece";
    case MsgType::MsgCancel:
        return "cancel";
    case MsgType::MsgKeepAlive:
        return "keepAlive";
    default:
        return "unknown";
    }
}

// Added 'static' to match header
Message Message::format_request(u_int32_t piece_index, size_t begin, size_t length) { 
    // Create a local vector for the payload
    std::vector<std::byte> payload(12);

    u_int32_t idx = htonl(piece_index);
    // Use payload.data() to get the pointer to the internal array
    std::memcpy(payload.data(), &idx, 4);
    
    u_int32_t begin_ = htonl(begin);
    std::memcpy(payload.data() + 4, &begin_, 4);
    
    u_int32_t length_ = htonl(length);
    std::memcpy(payload.data() + 8, &length_, 4);
    
    // Return a new Message object constructed with the populated payload
    return Message(MsgType::MsgRequest, payload);
}

// Added 'static' to match header
Message Message::format_have(u_int32_t piece_index) { 
    // Create a local vector for the payload
    std::vector<std::byte> payload(4);
    
    u_int32_t idx = htonl(piece_index);
    std::memcpy(payload.data(), &idx, 4);
    
    return Message(MsgType::MsgHave, payload);
}

std::string Message::to_string() const
{
    std::stringstream ss;
    ss << "id: " << this->id_str() << " payload: ";
    for (auto b : this->payload)
    {
        ss << std::hex << static_cast<int>(b) << " ";
    }
    return ss.str();
}


Message Message::parse(u_int32_t idx, std::vector<std::byte>&bytes, Message& msg)
{ 
    if (msg.get_type() != MsgType::MsgPiece) {
        throw  Error("Invalid message type");
    }
    if (msg.get_payload().size() < 8) {
        throw  Error("Invalid message size");
    }
    // Get the first 4 bytes
    u_int32_t piece_index;
    std::memcpy(&piece_index, msg.get_payload().data(), 4);
    piece_index = ntohl(piece_index);
    
    if (piece_index != idx) {
        throw  Error("Invalid piece index");
    }

    u_int32_t begin;
    std::memcpy(&begin, msg.get_payload().data() + 4, 4);
    begin = ntohl(begin);

    if (begin > bytes.size() - 8) {
        throw  Error("Invalid begin");
    }


}
