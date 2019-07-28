#### PROJECT SETTINGS ####
# Compiler used
CXX ?= g++
# Extension of source files used in the project
SRC_EXT = cc
# Path to the source directory, relative to the makefile
SRC_PATH = .
# Path to the build directory, relative to the makefile
BUILD_PATH = .
# Path to the base directory
BASE_PATH = 
# Space-separated pkg-config libraries used by this project
LIBS = # -levent++ -L$(BASE_PATH)
# General compiler flags
COMPILE_FLAGS = -std=c++14 -Wall -O2 -Wextra -g -Wno-unused-parameter -Wno-restrict -pthread
# Add additional include paths
INCLUDES =-I..
# General linker settings
LINK_FLAGS = 
#### END PROJECT SETTINGS ####

SOURCES := $(shell find $(SRC_PATH) -name '*.$(SRC_EXT)')
OBJECTS := $(SOURCES:.$(SRC_EXT)=.o) 

%.o: %.cc
	$(CXX) -c $< $(COMPILE_FLAGS) $(LIBS) $(INCLUDES)


all: $(OBJECTS)


clean:
	rm *.o
