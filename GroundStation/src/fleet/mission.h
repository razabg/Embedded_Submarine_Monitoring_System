/*
 * mission.h - a combat mission: description, commander, and how many
 * combat personnel are assigned to it (section 4's CombatSubmarine
 * fields). A CombatSubmarine holds one as its current mission and
 * keeps past ones in a history list once each ends.
 */
#ifndef MISSION_H
#define MISSION_H

#include <string>

class Mission
{
public:
    Mission(std::string description, std::string commanderName, int personnelCount);

    const std::string &description() const { return description_; }
    const std::string &commanderName() const { return commanderName_; }
    int personnelCount() const { return personnelCount_; }

    bool setDescription(const std::string &description);
    bool setCommanderName(const std::string &commanderName);
    bool setPersonnelCount(int personnelCount);

private:
    std::string description_;
    std::string commanderName_;
    int personnelCount_;
};

#endif /* MISSION_H */
