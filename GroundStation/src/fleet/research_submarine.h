/*
 * research_submarine.h - a submarine used for research missions
 * (section 4). Its "mission" is simply its topic and crew -- no
 * separate Mission object, no CentralComputer (see submarine.h and
 * the PDF's own scoping of the central computer to combat subs only).
 */
#ifndef RESEARCH_SUBMARINE_H
#define RESEARCH_SUBMARINE_H

#include <string>
#include <vector>

#include "submarine.h"

class ResearchSubmarine : public Submarine
{
public:
    ResearchSubmarine(std::string serialNumber, std::string name);

    SubmarineType type() const override { return SubmarineType::Research; }
    std::string describe() const override;

    const std::vector<std::string> &researchers() const { return researchers_; }
    const std::string &researchTopic() const { return researchTopic_; }

    /* Assigns the mission: sets the topic + crew and marks the
     * submarine busy. False if already assigned or topic is empty. */
    bool assignMission(std::string researchTopic, std::vector<std::string> researchers);

    bool setResearchTopic(const std::string &researchTopic);
    bool setResearchers(std::vector<std::string> researchers);

private:
    std::vector<std::string> researchers_;
    std::string researchTopic_;
};

#endif /* RESEARCH_SUBMARINE_H */
