#include "research_submarine.h"

#include <sstream>

ResearchSubmarine::ResearchSubmarine(std::string serialNumber, std::string name)
    : Submarine(std::move(serialNumber), std::move(name))
{
}

std::string ResearchSubmarine::describe() const
{
    std::ostringstream out;//std::format in cpp20
    out << "[Research] " << serialNumber() << " \"" << name() << "\" - "
        << (isAssigned() ? "assigned" : "available");
    if (isAssigned()) {
        out << " | topic: " << researchTopic_ << " | researchers: ";
        for (size_t i = 0; i < researchers_.size(); ++i) {
            if (i > 0) out << ", ";
            out << researchers_[i];
        }
    }
    return out.str();
}

bool ResearchSubmarine::assignMission(std::string researchTopic, std::vector<std::string> researchers)
{
    if (isAssigned() || researchTopic.empty()) return false;
    researchTopic_ = std::move(researchTopic);
    researchers_ = std::move(researchers);
    setAssigned(true);
    return true;
}

bool ResearchSubmarine::setResearchTopic(const std::string &researchTopic)
{
    if (researchTopic.empty()) return false;
    researchTopic_ = researchTopic;
    return true;
}

bool ResearchSubmarine::setResearchers(std::vector<std::string> researchers)
{
    researchers_ = std::move(researchers);
    return true;
}
