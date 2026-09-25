/*
 * submarine.h - Submarine base class (section 4). Holds what every
 * submarine has regardless of type: identity, mission-assignment
 * status, and messages it has received. Type-specific data (research
 * topic, combat mission details, the CentralComputer composition)
 * lives in the derived classes -- per the spec PDF, every submarine
 * has a central computer in reality, but only CombatSubmarine models
 * it as an owned object.
 *
 * Polymorphic base: copy/move are deleted to rule out slicing through
 * a Submarine& or Submarine* -- every submarine is created once by
 * the fleet manager and referred to by pointer/reference from there
 * on, never duplicated.
 */
#ifndef SUBMARINE_H
#define SUBMARINE_H

#include <string>
#include <vector>

#include "message.h"

enum class SubmarineType { Research, Combat };

class Submarine
{
public:
    Submarine(std::string serialNumber, std::string name);
    virtual ~Submarine() = default;

    Submarine(const Submarine &) = delete;
    Submarine &operator=(const Submarine &) = delete;

    const std::string &serialNumber() const { return serialNumber_; }
    const std::string &name() const { return name_; }
    bool isAssigned() const { return assigned_; }

    bool setName(const std::string &name);

    /* Marks the submarine available again. Returns false if it
     * wasn't assigned to begin with. Virtual so CombatSubmarine can
     * extend it (archive the current mission into its history)
     * while still going through this base behavior first. */
    virtual bool endMission();

    void receiveMessage(Message message);
    const std::vector<Message> &receivedMessages() const { return receivedMessages_; }

    virtual SubmarineType type() const = 0;
    virtual std::string describe() const = 0;

protected:
    void setAssigned(bool assigned) { assigned_ = assigned; }

private:
    std::string serialNumber_;
    std::string name_;
    bool assigned_ = false;
    std::vector<Message> receivedMessages_;
};

#endif /* SUBMARINE_H */
