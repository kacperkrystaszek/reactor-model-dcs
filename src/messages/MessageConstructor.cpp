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
void MessageConstructor::createStateMsg(float u1, float u2, float y1, float y2, float sp_y1, float sp_y2, int psc1, int psc2, bool is_event_y1, bool is_event_y2, std::stringstream& buf){
    const std::string type = MESSAGE_TYPES[MessageType::STATUS];
    buf << "{\"type\":\"" << type << "\", \"payload\": {" << 
    "\"u1\": " << u1 << ", " << 
    "\"u2\": " << u2 << ", " << 
    "\"y1\": " << y1 << ", " << 
    "\"y2\": " << y2 << ", " <<
    "\"sp_y1\": " << sp_y1 << ", " <<
    "\"sp_y2\": " << sp_y2 << ", " <<
    "\"psc1\": " << psc1 << ", " <<
    "\"psc2\": " << psc2 << ", " <<
    "\"is_event_y1\": " << (is_event_y1 ? "true" : "false") << ", " <<
    "\"is_event_y2\": " << (is_event_y2 ? "true" : "false") << "}}";
}