#include <vector>
#include <string>

enum MessageType{
    INIT,
    ACK,
    START,
    STATUS
};

const std::vector<std::string> MESSAGE_TYPES({"INIT", "ACK", "START", "STATUS"});