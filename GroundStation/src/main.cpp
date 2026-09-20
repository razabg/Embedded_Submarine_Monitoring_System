/*
 * main.cpp - the Ground Station program: a 10-option menu (section 4)
 * over a FleetManager. Reads all input here and passes plain
 * parameters into the fleet classes -- no class ever reads input
 * itself (course rule).
 */
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "combat_submarine.h"
#include "fleet_manager.h"
#include "research_submarine.h"
#include "submarine.h"

/* ===============================================================
 * Input helpers
 * =============================================================== */

static bool read_int(const std::string &prompt, int lo, int hi, int &out)
{
    for (;;) {
        std::cout << prompt;
        if (!(std::cin >> out)) {
            if (std::cin.eof()) return false;
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "not a number, try again\n";
            continue;
        }
        if (out < lo || out > hi) {
            std::cout << "must be between " << lo << " and " << hi << "\n";
            continue;
        }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        return true;
    }
}

static bool read_line(const std::string &prompt, std::string &out)
{
    std::cout << prompt;
    return static_cast<bool>(std::getline(std::cin, out));
}

/* Splits a comma-separated line into trimmed, non-empty names. */
static std::vector<std::string> split_names(const std::string &line)
{
    std::vector<std::string> names;
    std::stringstream ss(line);
    std::string part;
    while (std::getline(ss, part, ',')) {
        size_t begin = part.find_first_not_of(" \t");
        size_t end = part.find_last_not_of(" \t");
        if (begin == std::string::npos) continue;
        names.push_back(part.substr(begin, end - begin + 1));
    }
    return names;
}

/* ===============================================================
 * Menu actions
 * =============================================================== */

static void do_add_submarine(FleetManager &fleet)
{
    int typeChoice;
    std::cout << " 1) Research\n 2) Combat\n";
    if (!read_int("> ", 1, 2, typeChoice)) return;

    std::string serialNumber, name;
    if (!read_line("  serial number: ", serialNumber)) return;
    if (!read_line("  name: ", name)) return;

    bool added;
    if (typeChoice == 1) {
        added = fleet.addResearchSubmarine(serialNumber, name);
    } else {
        std::string centralComputerHost;
        int centralComputerPort;
        if (!read_line("  central computer host (e.g. 127.0.0.1): ", centralComputerHost)) return;
        if (!read_int("  central computer port: ", 1, 65535, centralComputerPort)) return;
        added = fleet.addCombatSubmarine(serialNumber, name, centralComputerHost,
                                          static_cast<uint16_t>(centralComputerPort));
    }
    std::cout << (added ? "added\n" : "rejected (empty field, or serial number already in use)\n");
}

static void do_show_all(const FleetManager &fleet)
{
    if (fleet.submarines().empty()) {
        std::cout << "no submarines in the fleet\n";
        return;
    }
    for (const auto &sub : fleet.submarines()) {
        std::cout << "  " << sub->describe() << "\n";
    }
}

static void do_find_submarine(const FleetManager &fleet)
{
    std::string serialNumber;
    if (!read_line("  serial number: ", serialNumber)) return;

    const Submarine *sub = fleet.findBySerial(serialNumber);
    std::cout << (sub != nullptr ? sub->describe() : "not found") << "\n";
}

static void do_assign_mission(FleetManager &fleet)
{
    std::string serialNumber;
    if (!read_line("  serial number: ", serialNumber)) return;

    Submarine *sub = fleet.findBySerial(serialNumber);
    if (sub == nullptr) {
        std::cout << "not found\n";
        return;
    }

    bool assigned;
    if (sub->type() == SubmarineType::Research) {
        auto *research = dynamic_cast<ResearchSubmarine *>(sub);
        std::string topic, researcherLine;
        if (!read_line("  research topic: ", topic)) return;
        if (!read_line("  researchers (comma separated): ", researcherLine)) return;
        assigned = research->assignMission(topic, split_names(researcherLine));
    } else {
        auto *combat = dynamic_cast<CombatSubmarine *>(sub);
        std::string description, commanderName;
        int personnelCount;
        if (!read_line("  mission description: ", description)) return;
        if (!read_line("  commander name: ", commanderName)) return;
        if (!read_int("  combat personnel: ", 0, 10000, personnelCount)) return;
        assigned = combat->assignMission(description, commanderName, personnelCount);
    }
    std::cout << (assigned ? "assigned\n" : "rejected (already assigned, or invalid details)\n");
}

static void do_update_mission(FleetManager &fleet)
{
    std::string serialNumber;
    if (!read_line("  serial number: ", serialNumber)) return;

    Submarine *sub = fleet.findBySerial(serialNumber);
    if (sub == nullptr) {
        std::cout << "not found\n";
        return;
    }
    if (!sub->isAssigned()) {
        std::cout << "not currently assigned to a mission\n";
        return;
    }

    std::cout << "  (leave a field blank to keep it unchanged)\n";
    if (sub->type() == SubmarineType::Research) {
        auto *research = dynamic_cast<ResearchSubmarine *>(sub);
        std::string topic, researcherLine;
        if (!read_line("  new research topic: ", topic)) return;
        if (!topic.empty()) research->setResearchTopic(topic);
        if (!read_line("  new researchers (comma separated): ", researcherLine)) return;
        if (!researcherLine.empty()) research->setResearchers(split_names(researcherLine));
    } else {
        auto *combat = dynamic_cast<CombatSubmarine *>(sub);
        Mission *mission = combat->currentMission();
        std::string description, commanderName, personnelLine;
        if (!read_line("  new mission description: ", description)) return;
        if (!description.empty()) mission->setDescription(description);
        if (!read_line("  new commander name: ", commanderName)) return;
        if (!commanderName.empty()) mission->setCommanderName(commanderName);
        if (!read_line("  new combat personnel: ", personnelLine)) return;
        if (!personnelLine.empty()) mission->setPersonnelCount(std::stoi(personnelLine));
    }
    std::cout << "updated\n";
}

static void do_end_mission(FleetManager &fleet)
{
    std::string serialNumber;
    if (!read_line("  serial number: ", serialNumber)) return;

    Submarine *sub = fleet.findBySerial(serialNumber);
    if (sub == nullptr) {
        std::cout << "not found\n";
        return;
    }
    std::cout << (sub->endMission() ? "mission ended, submarine available\n" : "was not assigned\n");
}

static void do_link_submarines(FleetManager &fleet)
{
    std::string a, b;
    if (!read_line("  first combat submarine serial number: ", a)) return;
    if (!read_line("  second combat submarine serial number: ", b)) return;

    CombatSubmarine *subA = fleet.findCombatSubmarine(a);
    CombatSubmarine *subB = fleet.findCombatSubmarine(b);
    if (subA == nullptr || subB == nullptr) {
        std::cout << "one or both serial numbers are not a combat submarine in the fleet\n";
        return;
    }
    std::cout << (subA->linkSubmarine(*subB) ? "linked\n" : "rejected (same submarine, or already linked)\n");
}

static void do_send_message(FleetManager &fleet)
{
    std::string senderSerial, recipientSerial, content;
    if (!read_line("  sender serial number: ", senderSerial)) return;
    if (!read_line("  recipient serial number: ", recipientSerial)) return;
    if (!read_line("  message: ", content)) return;

    CombatSubmarine *sender = fleet.findCombatSubmarine(senderSerial);
    CombatSubmarine *recipient = fleet.findCombatSubmarine(recipientSerial);
    if (sender == nullptr || recipient == nullptr) {
        std::cout << "one or both serial numbers are not a combat submarine in the fleet\n";
        return;
    }
    std::cout << (sender->sendMessage(*recipient, content) ? "sent\n"
                                                             : "rejected (not linked to the same mission)\n");
}

static void do_show_messages(const FleetManager &fleet)
{
    std::string serialNumber;
    if (!read_line("  serial number: ", serialNumber)) return;

    const Submarine *sub = fleet.findBySerial(serialNumber);
    if (sub == nullptr) {
        std::cout << "not found\n";
        return;
    }
    if (sub->receivedMessages().empty()) {
        std::cout << "no messages received\n";
        return;
    }
    for (const Message &message : sub->receivedMessages()) {
        std::cout << "  from " << message.sender().serialNumber() << " (" << message.sender().name()
                  << "): " << message.content() << "\n";
    }
}

/* Queries a combat submarine's real Central Computer for stored
 * logs/events over Ethernet (section 4) -- not one of the spec's
 * fixed 10 menu options, added as an explicit 11th item rather than
 * folded into an existing one (see the chat that decided this). Same
 * start/end + 1/2/3 kind-choice UX as Central Computer's own "Query
 * LNC's SD card directly" menu option, for consistency across the
 * whole system. */
static void do_query_central_computer(FleetManager &fleet)
{
    std::string serialNumber;
    if (!read_line("  combat submarine serial number: ", serialNumber)) return;

    CombatSubmarine *sub = fleet.findCombatSubmarine(serialNumber);
    if (sub == nullptr) {
        std::cout << "not found, or not a combat submarine\n";
        return;
    }

    std::string start, end;
    if (!read_line("  start (YYYY-MM-DD HH:MM:SS): ", start)) return;
    if (!read_line("  end   (YYYY-MM-DD HH:MM:SS): ", end)) return;

    int kindChoice;
    std::cout << " 1) Measurements\n 2) Events\n 3) Both\n";
    if (!read_int("> ", 1, 3, kindChoice)) return;

    if (kindChoice == 1 || kindChoice == 3) {
        std::cout << "-- measurements --\n";
        for (const auto &line : sub->centralComputer().queryLogs(start, end)) {
            std::cout << "  " << line << "\n";
        }
    }
    if (kindChoice == 2 || kindChoice == 3) {
        std::cout << "-- events --\n";
        for (const auto &line : sub->centralComputer().queryEvents(start, end)) {
            std::cout << "  " << line << "\n";
        }
    }
}

/* ===============================================================
 * Menu loop
 * =============================================================== */

static void run_menu(FleetManager &fleet)
{
    for (;;) {
        std::cout << "\n===== Ground Station =====\n"
                     " 1) Add a submarine\n"
                     " 2) Show all submarines\n"
                     " 3) Find a submarine by serial number\n"
                     " 4) Assign a mission\n"
                     " 5) Update mission details\n"
                     " 6) End a mission\n"
                     " 7) Link combat submarines to the same mission\n"
                     " 8) Send a message (combat submarines only)\n"
                     " 9) Show messages received by a submarine\n"
                     "10) Exit\n"
                     "11) Query a combat submarine's Central Computer (logs/events)\n";

        int choice;
        if (!read_int("> ", 1, 11, choice)) return;

        switch (choice) {
        case 1: do_add_submarine(fleet); break;
        case 2: do_show_all(fleet); break;
        case 3: do_find_submarine(fleet); break;
        case 4: do_assign_mission(fleet); break;
        case 5: do_update_mission(fleet); break;
        case 6: do_end_mission(fleet); break;
        case 7: do_link_submarines(fleet); break;
        case 8: do_send_message(fleet); break;
        case 9: do_show_messages(fleet); break;
        case 10: return;
        case 11: do_query_central_computer(fleet); break;
        default: break;
        }
    }
}

int main()
{
    FleetManager fleet;
    run_menu(fleet);
    std::cout << "\nclosed\n";
    return 0;
}
