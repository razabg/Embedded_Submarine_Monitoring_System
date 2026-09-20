/*
 * combat_submarine.h - a submarine used for operational missions
 * (section 4). Composes its own CentralComputer (the PDF's "central
 * computer... treated as an object that belongs to each combat
 * submarine") and tracks the other combat submarines sharing its
 * current mission plus a history of past missions.
 *
 * linkedSubmarines_ holds raw, non-owning pointers -- same reasoning
 * as Message::sender_ (submarine.h/message.h): the fleet manager owns
 * every submarine; this is purely "who else is on this mission,"
 * never a claim on their lifetime.
 */
#ifndef COMBAT_SUBMARINE_H
#define COMBAT_SUBMARINE_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "central_computer.h"
#include "mission.h"
#include "submarine.h"

class CombatSubmarine : public Submarine
{
public:
    /* centralComputerHost/Port: where this submarine's real Central
     * Computer listens for the GroundStation link (GsLinkServer,
     * CentralComputer/src/gsLink/) -- not necessarily reachable at
     * add-mission time, just where to try later. */
    CombatSubmarine(std::string serialNumber, std::string name,
                     std::string centralComputerHost, uint16_t centralComputerPort);

    SubmarineType type() const override { return SubmarineType::Combat; }
    std::string describe() const override;

    /* Sets the current mission and marks the submarine busy. False
     * if already assigned, or description/commanderName is empty, or
     * personnelCount is negative -- nothing changed either way. */
    bool assignMission(std::string description, std::string commanderName, int personnelCount);

    /* Archives the current mission into history, clears it, unlinks
     * from every submarine that was sharing it, and marks available.
     * False if not currently assigned. */
    bool endMission() override;

    Mission *currentMission() { return currentMission_ ? &*currentMission_ : nullptr; }
    const Mission *currentMission() const { return currentMission_ ? &*currentMission_ : nullptr; }
    const std::vector<Mission> &missionHistory() const { return missionHistory_; }

    /* Links two combat submarines as sharing the current mission
     * (bidirectional). False if they're already linked or it's the
     * same submarine. */
    bool linkSubmarine(CombatSubmarine &other);
    bool isLinkedTo(const CombatSubmarine &other) const;
    const std::vector<CombatSubmarine *> &linkedSubmarines() const { return linkedSubmarines_; }

    /* Sends a message to another combat submarine. False (nothing
     * sent) unless recipient is currently linked to this one. */
    bool sendMessage(CombatSubmarine &recipient, const std::string &content) const;

    CentralComputer &centralComputer() { return *centralComputer_; }
    const CentralComputer &centralComputer() const { return *centralComputer_; }

private:
    std::optional<Mission> currentMission_;
    std::vector<Mission> missionHistory_;
    std::vector<CombatSubmarine *> linkedSubmarines_;
    std::unique_ptr<CentralComputer> centralComputer_;
};

#endif /* COMBAT_SUBMARINE_H */
