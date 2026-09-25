CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2 -g
CC       ?= gcc
CFLAGS   ?= -std=c11 -Wall -Wextra -O2 -g

TLV_DIR    = ../../../Shared/ProtocolTLV
NET_DIR    = ../../../Shared/Net
COMMON_DIR = ../common
COMM_DIR   = ../communication
UART_DIR   = ../uartTransport

INCLUDES = -I$(TLV_DIR) -I$(NET_DIR) -I$(COMMON_DIR) -I$(COMM_DIR) -I$(UART_DIR) -I/usr/local/include
LIBS     = -L/usr/local/lib -lSQLiteCpp -lsqlite3 -lpthread -ldl

BUILD = build

# dca_test: plain unit-style smoke test, no Communication/Transport involved.
DCA_TEST_BIN = $(BUILD)/dca_test
DCA_TEST_OBJ = $(BUILD)/dca_test.o $(BUILD)/data_collection_analysis.o $(BUILD)/time_utils.o $(BUILD)/tlv.o

# dca_hardware_test: the real thing, wired to a live Transport/Communication.
HW_TEST_BIN = $(BUILD)/dca_hardware_test
HW_TEST_OBJ = $(BUILD)/dca_hardware_test.o $(BUILD)/data_collection_analysis.o \
              $(BUILD)/time_utils.o $(BUILD)/tlv.o $(BUILD)/communication.o \
              $(BUILD)/serial_transport.o $(BUILD)/tcp_transport.o $(BUILD)/serial.o

all: $(DCA_TEST_BIN) $(HW_TEST_BIN)

$(BUILD):
	mkdir -p $(BUILD)

$(DCA_TEST_BIN): $(BUILD) $(DCA_TEST_OBJ)
	$(CXX) $(CXXFLAGS) -o $(DCA_TEST_BIN) $(DCA_TEST_OBJ) $(LIBS)

$(HW_TEST_BIN): $(BUILD) $(HW_TEST_OBJ)
	$(CXX) $(CXXFLAGS) -o $(HW_TEST_BIN) $(HW_TEST_OBJ) $(LIBS)

$(BUILD)/dca_test.o: dca_test.cpp data_collection_analysis.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/dca_hardware_test.o: dca_hardware_test.cpp data_collection_analysis.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/data_collection_analysis.o: data_collection_analysis.cpp data_collection_analysis.h $(COMMON_DIR)/time_utils.h $(BUILD)
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

clean:
	rm -rf $(BUILD) test_output

.PHONY: all clean
