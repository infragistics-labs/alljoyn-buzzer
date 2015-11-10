DEPS_BASE := /home/saguiar
RPI_BASE := $(DEPS_BASE)/rpi-root
RPI_TOOLS := $(DEPS_BASE)/rpi-tools
ALLJOYN_BASE := $(DEPS_BASE)/alljoyn/core/alljoyn
ALLJOYN_VARIANT := release
ALLJOYN_BUILD := $(ALLJOYN_BASE)/build/linux/arm/$(ALLJOYN_VARIANT)/dist
ALLJOYN_AUDIO_BUILD := $(ALLJOYN_BASE)/services/audio/build/linux/arm/$(ALLJOYN_VARIANT)/dist
OBJ_DIR := obj

BIN_DIR := bin

# Statically link bundled router
ALLJOYN_LIB := \
	$(ALLJOYN_BUILD)/cpp/lib/libajrouter.a \
        $(ALLJOYN_AUDIO_BUILD)/audio/lib/liballjoyn_audio.a \
	$(ALLJOYN_BUILD)/cpp/lib/liballjoyn_about.a \
	$(ALLJOYN_BUILD)/cpp/lib/liballjoyn.a

CROSS_COMPILE?=$(RPI_TOOLS)/arm-bcm2708/gcc-linaro-arm-linux-gnueabihf-raspbian/bin/arm-linux-gnueabihf-
CXX = $(CROSS_COMPILE)g++
# Pass ROUTER flag to enable bundled router
CXXFLAGS = -Wall -pipe -std=c++98 -fno-rtti -fno-exceptions -Wno-long-long -Wno-deprecated -g -DQCC_OS_LINUX -DQCC_OS_GROUP_POSIX -DROUTER
CPPFLAGS = -I$(ALLJOYN_BUILD)/cpp/inc -I$(RPI_BASE)/usr/include/ -I$(ALLJOYN_AUDIO_BUILD)/audio/inc/
LDLIBS = $(ALLJOYN_LIB) -L$(RPI_BASE)/usr/lib/arm-linux-gnueabihf -lstdc++ -lpthread -lrt -lasound

BINARIES := \
	$(BIN_DIR)/buzzer_service \
	$(BIN_DIR)/sink_client \
	$(BIN_DIR)/sink_service
.PHONY: default clean all

default: all

$(BIN_DIR)/buzzer_service: $(OBJ_DIR)/buzzer_service.o $(OBJ_DIR)/GPIOClass.o
	mkdir -p $(BIN_DIR)
	$(CXX) -o $@ $^ $(LDLIBS)

$(BIN_DIR)/sink_client: $(OBJ_DIR)/sink_client.o $(OBJ_DIR)/WavDataSource.o $(OBJ_DIR)/AlsaDataSource.o
	mkdir -p $(BIN_DIR)
	$(CXX) -o $@ $^ $(LDLIBS)

$(BIN_DIR)/sink_service: $(OBJ_DIR)/sink_service.o 
	mkdir -p $(BIN_DIR)
	$(CXX) -o $@ $^ $(LDLIBS)

$(OBJ_DIR)/%.o : %.cc
	mkdir -p $(OBJ_DIR)
	$(CXX) -c $(CXXFLAGS) $(CPPFLAGS) -o $@ $<

all: $(BINARIES)

clean:
	rm -rf $(BIN_DIR)
	rm -rf $(OBJ_DIR)
