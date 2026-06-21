#ifndef ERROR_H
#define ERROR_H

#include <string>
#include <exception>
class Error :public std::exception {
public:
    Error(std::string msg_): msg(msg_) {}
    std::string msg;
};

class InvalidMessage : public Error {
public:
    InvalidMessage(std::string msg_): Error(msg_) {}
};

#endif