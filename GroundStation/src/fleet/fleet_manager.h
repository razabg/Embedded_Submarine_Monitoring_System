/*
 * fleet_manager.h - the manager class: owns every Submarine in the
 * fleet (course rule: a manager class holds all the data). main.cpp
 * reads input and calls into this; no I/O happens in here or in any
 * Submarine.
 */
#ifndef FLEET_MANAGER_H
#define FLEET_MANAGER_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "combat_submarine.h"
#include "research_submarine.h"
#include "submarine.h"

class FleetManager
{
public:
    FleetManager() = default;

    FleetManager(const FleetManager &) = delete;
    FleetManager &operator=(const FleetManager &) = delete;

    /* False (nothing added) if serialNumber/name is empty or
     * serialNumber is already used by another submarine. */
    bool addResearchSubmarine(std::string serialNumber, std::string name);

    /* centralComputerHost/Port: see combat_submarine.h. */
    bool addCombatSubmarine(std::string serialNumber, std::string name, std::string centralComputerHost,
                             uint16_t centralComputerPort);

    const std::vector<std::unique_ptr<Submarine>> &submarines() const { return submarines_; }

    Submarine *findBySerial(const std::string &serialNumber);
    const Submarine *findBySerial(const std::string &serialNumber) const;

    /* findBySerial() + a dynamic_cast, for the combat-only menu
     * options (link submarines / send a message) -- nullptr if not
     * found, or found but not a CombatSubmarine. */
    CombatSubmarine *findCombatSubmarine(const std::string &serialNumber);

private:
    bool hasSerial(const std::string &serialNumber) const;

    std::vector<std::unique_ptr<Submarine>> submarines_;
};

#endif /* FLEET_MANAGER_H */
