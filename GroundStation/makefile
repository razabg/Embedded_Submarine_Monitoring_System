# GroundStation/makefile - builds the ground_station program
# (src/main.cpp): the fleet-management menu (section 4) over the
# Submarine/FleetManager classes in src/fleet, plus the (currently
# mocked) CentralComputer link in src/communication.

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2 -g
CC       ?= gcc
CFLAGS   ?= -std=c11 -Wall -Wextra -O2 -g

TLV_DIR   = ../Shared/ProtocolTLV
NET_DIR   = ../Shared/Net
FLEET_DIR = src/fleet
COMM_DIR  = src/communication

INCLUDES = -I$(TLV_DIR) -I$(NET_DIR) -I$(FLEET_DIR) -I$(COMM_DIR)

BUILD = build
BIN   = $(BUILD)/ground_station
OBJ   = $(BUILD)/main.o $(BUILD)/fleet_manager.o $(BUILD)/submarine.o $(BUILD)/research_submarine.o \
        $(BUILD)/combat_submarine.o $(BUILD)/mission.o $(BUILD)/message.o \
        $(BUILD)/central_computer.o $(BUILD)/tcp_transport.o $(BUILD)/tlv.o

all: $(BIN)

$(BUILD):
	mkdir -p $(BUILD)

$(BIN): $(BUILD) $(OBJ)
	$(CXX) $(CXXFLAGS) -o $(BIN) $(OBJ)

$(BUILD)/main.o: src/main.cpp $(FLEET_DIR)/fleet_manager.h $(FLEET_DIR)/combat_submarine.h $(FLEET_DIR)/research_submarine.h $(FLEET_DIR)/submarine.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/fleet_manager.o: $(FLEET_DIR)/fleet_manager.cpp $(FLEET_DIR)/fleet_manager.h $(FLEET_DIR)/combat_submarine.h $(FLEET_DIR)/research_submarine.h $(FLEET_DIR)/submarine.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/submarine.o: $(FLEET_DIR)/submarine.cpp $(FLEET_DIR)/submarine.h $(FLEET_DIR)/message.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/research_submarine.o: $(FLEET_DIR)/research_submarine.cpp $(FLEET_DIR)/research_submarine.h $(FLEET_DIR)/submarine.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/combat_submarine.o: $(FLEET_DIR)/combat_submarine.cpp $(FLEET_DIR)/combat_submarine.h $(FLEET_DIR)/submarine.h $(FLEET_DIR)/mission.h $(COMM_DIR)/central_computer.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/mission.o: $(FLEET_DIR)/mission.cpp $(FLEET_DIR)/mission.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/message.o: $(FLEET_DIR)/message.cpp $(FLEET_DIR)/message.h $(FLEET_DIR)/submarine.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/central_computer.o: $(COMM_DIR)/central_computer.cpp $(COMM_DIR)/central_computer.h $(NET_DIR)/tcp_transport.h $(TLV_DIR)/tlv.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/tcp_transport.o: $(NET_DIR)/tcp_transport.cpp $(NET_DIR)/tcp_transport.h $(NET_DIR)/transport.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/tlv.o: $(TLV_DIR)/tlv.c $(TLV_DIR)/tlv.h $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
