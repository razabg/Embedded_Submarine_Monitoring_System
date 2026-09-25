CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2 -g
CC       ?= gcc
CFLAGS   ?= -std=c11 -Wall -Wextra -O2 -g

TLV_DIR  = ../../../Shared/ProtocolTLV
NET_DIR  = ../../../Shared/Net
UART_DIR = ../uartTransport

INCLUDES = -I$(TLV_DIR) -I$(NET_DIR) -I$(UART_DIR)

BUILD = build
BIN   = $(BUILD)/comm_test
OBJ   = $(BUILD)/comm_test.o $(BUILD)/communication.o $(BUILD)/serial_transport.o \
        $(BUILD)/tcp_transport.o $(BUILD)/serial.o $(BUILD)/tlv.o

all: $(BIN)

$(BUILD):
	mkdir -p $(BUILD)

$(BIN): $(BUILD) $(OBJ)
	$(CXX) $(CXXFLAGS) -o $(BIN) $(OBJ) -lpthread

$(BUILD)/comm_test.o: comm_test.cpp communication.h serial_transport.h $(NET_DIR)/tcp_transport.h $(NET_DIR)/transport.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/communication.o: communication.cpp communication.h $(NET_DIR)/transport.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/serial_transport.o: serial_transport.cpp serial_transport.h $(NET_DIR)/transport.h $(UART_DIR)/serial.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/tcp_transport.o: $(NET_DIR)/tcp_transport.cpp $(NET_DIR)/tcp_transport.h $(NET_DIR)/transport.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/serial.o: $(UART_DIR)/serial.cpp $(UART_DIR)/serial.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/tlv.o: $(TLV_DIR)/tlv.c $(TLV_DIR)/tlv.h $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
