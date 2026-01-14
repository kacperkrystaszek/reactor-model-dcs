#include "messages/MessageConstructor.h"
#include "messages/MessageType.h"


void MessageConstructor::createInitMsg(const SystemConfig& cfg, std::stringstream& buf){
    const std::string type = MESSAGE_TYPES[MessageType::INIT];
    buf << "{\"type\":\"" << type << "\", \"id\": \"" << cfg.model_id << "\"}";
}

void MessageConstructor::createAckMsg(std::stringstream& buf){
    const std::string type = MESSAGE_TYPES[MessageType::ACK];
    buf << "{\"type\":\"" << type << "\"}";
}
void MessageConstructor::createStateMsg(float& u1, float& u2, float& y1, float& y2, std::stringstream& buf){
    const std::string type = MESSAGE_TYPES[MessageType::STATUS];
    buf << "{\"type\":\"" << type << "\", \"payload\": {" << 
    "\"u1\": " << u1 << ", " << 
    "\"u2\": " << u2 << ", " << 
    "\"y1\": " << y1 << ", " << 
    "\"y2\": " << y2 << "}}";
}