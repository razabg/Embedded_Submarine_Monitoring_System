CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2 -g
CC       ?= gcc
CFLAGS   ?= -std=c11 -Wall -Wextra -O2 -g

TLV_DIR    = ../../../Shared/ProtocolTLV
COMMON_DIR = ../common
DCA_DIR    = ../dataCollection

INCLUDES = -I$(TLV_DIR) -I$(COMMON_DIR) -I$(DCA_DIR) -I/usr/local/include
LIBS     = -L/usr/local/lib -lSQLiteCpp -lsqlite3 -lpthread -ldl

BUILD = build
BIN   = $(BUILD)/gs_link_test
OBJ   = $(BUILD)/gs_link_test.o $(BUILD)/gs_link_server.o $(BUILD)/data_collection_analysis.o \
        $(BUILD)/time_utils.o $(BUILD)/tlv.o

all: $(BIN)

$(BUILD):
	mkdir -p $(BUILD)

$(BIN): $(BUILD) $(OBJ)
	$(CXX) $(CXXFLAGS) -o $(BIN) $(OBJ) $(LIBS)

$(BUILD)/gs_link_test.o: gs_link_test.cpp gs_link_server.h $(DCA_DIR)/data_collection_analysis.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/gs_link_server.o: gs_link_server.cpp gs_link_server.h $(DCA_DIR)/data_collection_analysis.h $(COMMON_DIR)/time_utils.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/data_collection_analysis.o: $(DCA_DIR)/data_collection_analysis.cpp $(DCA_DIR)/data_collection_analysis.h $(COMMON_DIR)/time_utils.h $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/time_utils.o: $(COMMON_DIR)/time_utils.cpp $(COMMON_DIR)/time_utils.h $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/tlv.o: $(TLV_DIR)/tlv.c $(TLV_DIR)/tlv.h $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(BUILD) test_output

.PHONY: all clean
