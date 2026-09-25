CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2 -g
CC       ?= gcc
CFLAGS   ?= -std=c11 -Wall -Wextra -O2 -g

TLV_DIR    = ../../../Shared/ProtocolTLV
NET_DIR    = ../../../Shared/Net
COMMON_DIR = ../common
COMM_DIR   = ../communication
UART_DIR   = ../uartTransport
LOG_DIR    = ../log

INCLUDES = -I$(TLV_DIR) -I$(NET_DIR) -I$(COMMON_DIR) -I$(COMM_DIR) -I$(UART_DIR) -I$(LOG_DIR)

BUILD = build
BIN   = $(BUILD)/mgmt_hardware_test
OBJ   = $(BUILD)/mgmt_hardware_test.o $(BUILD)/management_command.o \
        $(BUILD)/time_utils.o $(BUILD)/tlv.o $(BUILD)/communication.o \
        $(BUILD)/serial_transport.o $(BUILD)/tcp_transport.o $(BUILD)/serial.o \
        $(BUILD)/log.o

all: $(BIN)

$(BUILD):
	mkdir -p $(BUILD)

$(BIN): $(BUILD) $(OBJ)
	$(CXX) $(CXXFLAGS) -o $(BIN) $(OBJ) -lpthread

$(BUILD)/mgmt_hardware_test.o: mgmt_hardware_test.cpp management_command.h $(LOG_DIR)/log.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/management_command.o: management_command.cpp management_command.h $(COMMON_DIR)/time_utils.h $(LOG_DIR)/log.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/time_utils.o: $(COMMON_DIR)/time_utils.cpp $(COMMON_DIR)/time_utils.h $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/tlv.o: $(TLV_DIR)/tlv.c $(TLV_DIR)/tlv.h $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/communication.o: $(COMM_DIR)/communication.cpp $(COMM_DIR)/communication.h $(NET_DIR)/transport.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/serial_transport.o: $(COMM_DIR)/serial_transport.cpp $(COMM_DIR)/serial_transport.h $(NET_DIR)/transport.h $(UART_DIR)/serial.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/tcp_transport.o: $(NET_DIR)/tcp_transport.cpp $(NET_DIR)/tcp_transport.h $(NET_DIR)/transport.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/serial.o: $(UART_DIR)/serial.cpp $(UART_DIR)/serial.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/log.o: $(LOG_DIR)/log.cpp $(LOG_DIR)/log.h $(COMMON_DIR)/time_utils.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(BUILD) test_output

.PHONY: all clean
