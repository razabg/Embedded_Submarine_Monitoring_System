#include "combat_submarine.h"

#include <algorithm>
#include <sstream>

CombatSubmarine::CombatSubmarine(std::string serialNumber, std::string name,
                                  std::string centralComputerHost, uint16_t centralComputerPort)
    : Submarine(std::move(serialNumber), std::move(name)),
      centralComputer_(std::make_unique<CentralComputer>(std::move(centralComputerHost), centralComputerPort))
{
}

std::string CombatSubmarine::describe() const
{
    std::ostringstream out;
    out << "[Combat] " << serialNumber() << " \"" << name() << "\" - "
        << (isAssigned() ? "assigned" : "available");
    if (currentMission_) {
        out << " | mission: " << currentMission_->description() << " | commander: "
            << currentMission_->commanderName() << " | personnel: " << currentMission_->personnelCount();
        out << " | linked: ";
        for (size_t i = 0; i < linkedSubmarines_.size(); ++i) {
            if (i > 0) out << ", ";
            out << linkedSubmarines_[i]->serialNumber();
        }
    }
    out << " | past missions: " << missionHistory_.size();
    return out.str();
}

bool CombatSubmarine::assignMission(std::string description, std::string commanderName, int personnelCount)
{
    if (isAssigned() || description.empty() || commanderName.empty() || personnelCount < 0) return false;
    currentMission_.emplace(std::move(description), std::move(commanderName), personnelCount);
    setAssigned(true);
    return true;
}

bool CombatSubmarine::endMission()
{
    if (!Submarine::endMission()) return false;

    if (currentMission_) {
        missionHistory_.push_back(*currentMission_);
        currentMission_.reset();
    }

    for (CombatSubmarine *linked : linkedSubmarines_) {
        auto &peers = linked->linkedSubmarines_;
        peers.erase(std::remove(peers.begin(), peers.end(), this), peers.end());
    }
    linkedSubmarines_.clear();

    return true;
}

bool CombatSubmarine::linkSubmarine(CombatSubmarine &other)
{
    if (&other == this || isLinkedTo(other)) return false;
    linkedSubmarines_.push_back(&other);
    other.linkedSubmarines_.push_back(this);
    return true;
}

bool CombatSubmarine::isLinkedTo(const CombatSubmarine &other) const
{
    return std::find(linkedSubmarines_.begin(), linkedSubmarines_.end(), &other) != linkedSubmarines_.end();
}

bool CombatSubmarine::sendMessage(CombatSubmarine &recipient, const std::string &content) const
{
    if (!isLinkedTo(recipient)) return false;
    recipient.receiveMessage(Message(content, *this));
    return true;
}
