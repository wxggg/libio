#### PROJECT SETTINGS ####
# Compiler used
CXX ?= g++
# Extension of source files used in the project
SRC_EXT = cc
# Path to the source directory, relative to the makefile
HTTP_PATH = ../../http
# Path to the build directory, relative to the makefile
BUILD_PATH = .
# Path to the base directory
BASE_PATH = $(PWD)/..
# Space-separated pkg-config libraries used by this project
LIBS = # -levent++ -L$(BASE_PATH)
# General compiler flags
COMPILE_FLAGS = -std=c++14 -Wall -Wextra -g -Wno-unused-parameter -Wno-restrict -pthread
# Add additional include paths
INCLUDES =-I../..
# General linker settings
LINK_FLAGS = 
#### END PROJECT SETTINGS ####

SOURCES = $(shell find $(HTTP_PATH) -name '*.$(SRC_EXT)')
OBJECTS := $(SOURCES:.$(SRC_EXT)=.o) 

%.o: %.cc
	$(CXX) -c $< $(COMPILE_FLAGS) $(LIBS) $(INCLUDES)

all: http regress_http_client regress_http_server

http:
	cd $(HTTP_PATH) && make

regress_http_client: regress_http_client.o $(OBJECTS)
	$(CXX) -o $@ $< $(OBJECTS) $(COMPILE_FLAGS) $(LIBS) $(INCLUDES)

regress_http_server: regress_http_server.o $(OBJECTS)
	$(CXX) -o $@ $< $(OBJECTS) $(COMPILE_FLAGS) $(LIBS) $(INCLUDES)


clean:
	rm *.o
	rm regress_http_client regress_http_server
	cd $(HTTP_PATH) && make clean
