#include "submarine.h"

Submarine::Submarine(std::string serialNumber, std::string name)
    : serialNumber_(std::move(serialNumber)), name_(std::move(name))
{
}

bool Submarine::setName(const std::string &name)
{
    if (name.empty()) return false;
    name_ = name;
    return true;
}

bool Submarine::endMission()
{
    if (!assigned_) return false;
    assigned_ = false;
    return true;
}

void Submarine::receiveMessage(Message message)
{
    receivedMessages_.push_back(std::move(message));
}
