#include "mission.h"

Mission::Mission(std::string description, std::string commanderName, int personnelCount)
    : description_(std::move(description)), commanderName_(std::move(commanderName)),
      personnelCount_(personnelCount)
{
}

bool Mission::setDescription(const std::string &description)
{
    if (description.empty()) return false;
    description_ = description;
    return true;
}

bool Mission::setCommanderName(const std::string &commanderName)
{
    if (commanderName.empty()) return false;
    commanderName_ = commanderName;
    return true;
}

bool Mission::setPersonnelCount(int personnelCount)
{
    if (personnelCount < 0) return false;
    personnelCount_ = personnelCount;
    return true;
}
