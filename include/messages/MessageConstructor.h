#include <string>
#include <sstream>
#include "config/SystemConfig.h"
#include "ReactorModel.h"

class MessageConstructor{
    public:
        static void createInitMsg(const SystemConfig& cfg, std::stringstream& buf);
        static void createAckMsg(std::stringstream& buf);
        static void createStateMsg(float& u1, float& u2, float& y1, float& y2, int psc, std::stringstream& buf);
};