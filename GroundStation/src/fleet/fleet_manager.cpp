#include "fleet_manager.h"

bool FleetManager::hasSerial(const std::string &serialNumber) const
{
    return findBySerial(serialNumber) != nullptr;
}

bool FleetManager::addResearchSubmarine(std::string serialNumber, std::string name)
{
    if (serialNumber.empty() || name.empty() || hasSerial(serialNumber)) return false;
    submarines_.push_back(std::make_unique<ResearchSubmarine>(std::move(serialNumber), std::move(name)));
    return true;
}

bool FleetManager::addCombatSubmarine(std::string serialNumber, std::string name,
                                       std::string centralComputerHost, uint16_t centralComputerPort)
{
    if (serialNumber.empty() || name.empty() || hasSerial(serialNumber)) return false;
    submarines_.push_back(std::make_unique<CombatSubmarine>(std::move(serialNumber), std::move(name),
                                                              std::move(centralComputerHost),
                                                              centralComputerPort));
    return true;
}

Submarine *FleetManager::findBySerial(const std::string &serialNumber)
{
    for (auto &sub : submarines_) {
        if (sub->serialNumber() == serialNumber) return sub.get();
    }
    return nullptr;
}

const Submarine *FleetManager::findBySerial(const std::string &serialNumber) const
{
    for (const auto &sub : submarines_) {
        if (sub->serialNumber() == serialNumber) return sub.get();
    }
    return nullptr;
}

CombatSubmarine *FleetManager::findCombatSubmarine(const std::string &serialNumber)
{
    return dynamic_cast<CombatSubmarine *>(findBySerial(serialNumber));
}
