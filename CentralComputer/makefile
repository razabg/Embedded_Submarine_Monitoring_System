# CentralComputer/makefile - builds the real central_computer program
# (src/main.cpp), pulling in every Part-B module. Per-module makefiles
# under src/<module>/ still exist and still work independently -- each
# builds that module's own manual test harness (comm_test, dca_test,
# dca_hardware_test, mgmt_hardware_test, log_test, gateway, sermon).
# This is the one that links them all together into the one real
# program described in CLAUDE.md's Build & Test section.

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2 -g
CC       ?= gcc
CFLAGS   ?= -std=c11 -Wall -Wextra -O2 -g

TLV_DIR    = ../Shared/ProtocolTLV
NET_DIR    = ../Shared/Net
COMMON_DIR = src/common
COMM_DIR   = src/communication
UART_DIR   = src/uartTransport
LOG_DIR    = src/log
DCA_DIR    = src/dataCollection
MGMT_DIR   = src/managementCommand
GSLINK_DIR = src/gsLink

INCLUDES = -I$(TLV_DIR) -I$(NET_DIR) -I$(COMMON_DIR) -I$(COMM_DIR) -I$(UART_DIR) \
           -I$(LOG_DIR) -I$(DCA_DIR) -I$(MGMT_DIR) -I$(GSLINK_DIR) -I/usr/local/include
LIBS     = -L/usr/local/lib -lSQLiteCpp -lsqlite3 -lpthread -ldl

BUILD = build
BIN   = $(BUILD)/central_computer
OBJ   = $(BUILD)/main.o $(BUILD)/communication.o $(BUILD)/serial_transport.o \
        $(BUILD)/tcp_transport.o $(BUILD)/serial.o $(BUILD)/tlv.o \
        $(BUILD)/log.o $(BUILD)/data_collection_analysis.o \
        $(BUILD)/management_command.o $(BUILD)/time_utils.o \
        $(BUILD)/gs_link_server.o

all: $(BIN)

$(BUILD):
	mkdir -p $(BUILD)

$(BIN): $(BUILD) $(OBJ)
	$(CXX) $(CXXFLAGS) -o $(BIN) $(OBJ) $(LIBS)

$(BUILD)/main.o: src/main.cpp $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/communication.o: $(COMM_DIR)/communication.cpp $(COMM_DIR)/communication.h $(NET_DIR)/transport.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/serial_transport.o: $(COMM_DIR)/serial_transport.cpp $(COMM_DIR)/serial_transport.h $(NET_DIR)/transport.h $(UART_DIR)/serial.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/tcp_transport.o: $(NET_DIR)/tcp_transport.cpp $(NET_DIR)/tcp_transport.h $(NET_DIR)/transport.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/serial.o: $(UART_DIR)/serial.cpp $(UART_DIR)/serial.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/tlv.o: $(TLV_DIR)/tlv.c $(TLV_DIR)/tlv.h $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/log.o: $(LOG_DIR)/log.cpp $(LOG_DIR)/log.h $(COMMON_DIR)/time_utils.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/data_collection_analysis.o: $(DCA_DIR)/data_collection_analysis.cpp $(DCA_DIR)/data_collection_analysis.h $(COMMON_DIR)/time_utils.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/management_command.o: $(MGMT_DIR)/management_command.cpp $(MGMT_DIR)/management_command.h $(COMMON_DIR)/time_utils.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/time_utils.o: $(COMMON_DIR)/time_utils.cpp $(COMMON_DIR)/time_utils.h $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/gs_link_server.o: $(GSLINK_DIR)/gs_link_server.cpp $(GSLINK_DIR)/gs_link_server.h $(DCA_DIR)/data_collection_analysis.h $(COMMON_DIR)/time_utils.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(BUILD) logs data

.PHONY: all clean
