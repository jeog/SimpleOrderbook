#
# src/ root of sources for library
# test/ root of sources for tests
#
# bin/debug build files and binaries for debug builds
# bin/release build files and binaries for release builds
#
# tests are combined in a single binary w/ the types to run selected at 
# compile-time, depending on DEBUG #define or overrides. Passing -DDEBUG will 
# compile 'functional' tests; not doing so(or passing -DNDEBUG) will compile 
# 'performance' tests. To override this behavior use the generic test 
# targets(debug-test, release-test) and make w/ one of the following:
#     CXXFLAGS=-DRUN_FUNCTIONAL_TESTS
#     CXXFLAGS=-DRUN_PERFORMANCE_TESTS
#     CXXFLAGS=-DRUN_ALL_TESTS
#
# targets:
#     debug: debug build of library -> bin/debug
#     release: release build of library -> bin/release
#
#     functional-test: specialized debug build of tests -> bin/debug
#     performance-test: specialized release build of tests -> bin/release
#
#     debug-test: generic debug build of tests -> bin/debug
#     release-test: generic release build of tests -> bin/release
#
#         

PROJECT_ROOT = $(patsubst %/, %, $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
BUILD_DIR := $(PROJECT_ROOT)/bin
DEBUG_BUILD_DIR := $(BUILD_DIR)/debug
RELEASE_BUILD_DIR := $(BUILD_DIR)/release

# source file dirs for the main lib
SOB_LIB_SUBDIRS = src

DEBUG_SOB_LIB_SUBDIRS = $(addprefix $(DEBUG_BUILD_DIR)/, $(SOB_LIB_SUBDIRS))
RELEASE_SOB_LIB_SUBDIRS = $(addprefix $(RELEASE_BUILD_DIR)/, $(SOB_LIB_SUBDIRS))

# obj files for the main lib
SOB_LIB_OBJS = \
$(foreach var, $(SOB_LIB_SUBDIRS), \
    $(patsubst %.cpp, %.o, $(wildcard $(var)/*.cpp)) ) 

DEBUG_SOB_LIB_OBJS = $(addprefix $(DEBUG_BUILD_DIR)/, $(SOB_LIB_OBJS))
RELEASE_SOB_LIB_OBJS = $(addprefix $(RELEASE_BUILD_DIR)/, $(SOB_LIB_OBJS))

# source file dirsfor the performance and functional tests
TEST_SUBDIRS = \
test \
test/performance \
test/performance/tests \
test/functional \
test/functional/tests \
test/functional/tests/advanced_orders

DEBUG_TEST_SUBDIRS = $(addprefix $(DEBUG_BUILD_DIR)/, $(TEST_SUBDIRS))
RELEASE_TEST_SUBDIRS = $(addprefix $(RELEASE_BUILD_DIR)/, $(TEST_SUBDIRS))

# obj files for the performance and functional tests
SOB_TEST_OBJS = \
$(foreach var, $(TEST_SUBDIRS), \
    $(patsubst %.cpp, %.o, $(wildcard $(var)/*.cpp)) )

DEBUG_SOB_TEST_OBJS = $(addprefix $(DEBUG_BUILD_DIR)/, $(SOB_TEST_OBJS))
RELEASE_SOB_TEST_OBJS = $(addprefix $(RELEASE_BUILD_DIR)/, $(SOB_TEST_OBJS))

# include .d files for all builds/targets
DEPS = \
$(patsubst %.o, %.d, $(DEBUG_SOB_LIB_OBJS)) \
$(patsubst %.o, %.d, $(RELEASE_SOB_LIB_OBJS)) \
$(patsubst %.o, %.d, $(DEBUG_SOB_TEST_OBJS)) \
$(patsubst %.o, %.d, $(RELEASE_SOB_TEST_OBJS)) 
-include $(DEP)

# (internal) compiler options; CXXFLAGS should be set externally
OURFLAGS += -std=c++11 -Wall -fmessage-length=0 -ftemplate-backtrace-limit=0
DEBUG_FLAGS := -DDEBUG -g -O0

debug : OURFLAGS += $(DEBUG_FLAGS)
release : OURFLAGS += -O3
functional-test : OURFLAGS += $(DEBUG_FLAGS)
performance-test : OURFLAGS += -O3
debug-test : OURFLAGS += $(DEBUG_FLAGS)
release-test : OURFLAGS += -O3

LIBS := -lpthread -ldl -lutil
SOB_LIB_NAME := libSimpleOrderbook.a


all: performance-test functional-test 

functional-test: $(DEBUG_BUILD_DIR)/$(SOB_LIB_NAME) $(DEBUG_BUILD_DIR)/FunctionalTest

performance-test: $(RELEASE_BUILD_DIR)/$(SOB_LIB_NAME) $(RELEASE_BUILD_DIR)/PerformanceTest

debug-test: $(DEBUG_BUILD_DIR)/$(SOB_LIB_NAME) $(DEBUG_BUILD_DIR)/SimpleOrderbookTest

release-test: $(RELEASE_BUILD_DIR)/$(SOB_LIB_NAME) $(RELEASE_BUILD_DIR)/SimpleOrderbookTest

debug: $(DEBUG_BUILD_DIR)/$(SOB_LIB_NAME)

release: $(RELEASE_BUILD_DIR)/$(SOB_LIB_NAME)


$(DEBUG_BUILD_DIR)/$(SOB_LIB_NAME): $(DEBUG_SOB_LIB_OBJS) 
	@echo 'Building target: $@'
	@echo 'Invoking: GCC Archiver'
	ar -r  "$@" $(DEBUG_SOB_LIB_OBJS) 
	@echo 'Finished building target: $@'
	@echo ' '

$(DEBUG_BUILD_DIR)/src/%.o : $(PROJECT_ROOT)/src/%.cpp | $(DEBUG_SOB_LIB_SUBDIRS)
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ $(CXXFLAGS) $(OURFLAGS) -c -fPIC -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '			
	
$(DEBUG_SOB_LIB_SUBDIRS):	
	mkdir -p $@	
	

$(RELEASE_BUILD_DIR)/$(SOB_LIB_NAME): $(RELEASE_SOB_LIB_OBJS) 
	@echo 'Building target: $@'
	@echo 'Invoking: GCC Archiver'
	ar -r  "$@" $(RELEASE_SOB_LIB_OBJS) 
	@echo 'Finished building target: $@'
	@echo ' '
		
$(RELEASE_BUILD_DIR)/src/%.o : $(PROJECT_ROOT)/src/%.cpp | $(RELEASE_SOB_LIB_SUBDIRS)
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ $(CXXFLAGS) $(OURFLAGS) -c -fPIC -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

$(RELEASE_SOB_LIB_SUBDIRS):	
	mkdir -p $@	
			
	
$(DEBUG_BUILD_DIR)/FunctionalTest $(DEBUG_BUILD_DIR)/SimpleOrderbookTest : \
$(DEBUG_SOB_TEST_OBJS) $(DEBUG_BUILD_DIR)/$(SOB_LIB_NAME)
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C++ Linker'
	g++ $(CXXFLAGS) $(OURFLAGS) -o "$@" $(LIBS) $(DEBUG_SOB_TEST_OBJS) $(DEBUG_BUILD_DIR)/$(SOB_LIB_NAME)
	@echo 'Finished building target: $@'
	@echo ' '

$(DEBUG_BUILD_DIR)/test/%.o : $(PROJECT_ROOT)/test/%.cpp | $(DEBUG_TEST_SUBDIRS)
	@echo 'Building file: $<'	
	@echo 'Invoking: GCC C++ Compiler'
	g++ $(CXXFLAGS) $(OURFLAGS) -c -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

$(DEBUG_TEST_SUBDIRS):	
	mkdir -p $@	
	
	
$(RELEASE_BUILD_DIR)/PerformanceTest $(RELEASE_BUILD_DIR)/SimpleOrderbookTest : \
$(RELEASE_SOB_TEST_OBJS) $(RELEASE_BUILD_DIR)/$(SOB_LIB_NAME)
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C++ Linker'
	g++ $(CXXFLAGS) $(OURFLAGS) -o "$@"	$(LIBS) $(RELEASE_SOB_TEST_OBJS) $(RELEASE_BUILD_DIR)/$(SOB_LIB_NAME)
	@echo 'Finished building target: $@'
	@echo ' '	
	
$(RELEASE_BUILD_DIR)/test/%.o : $(PROJECT_ROOT)/test/%.cpp | $(RELEASE_TEST_SUBDIRS) 
	@echo 'Building file: $<'	
	@echo 'Invoking: GCC C++ Compiler'
	g++ $(CXXFLAGS) $(OURFLAGS) -c -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

$(RELEASE_TEST_SUBDIRS):	
	mkdir -p $@	


clean:
	rm -fr $(DEBUG_BUILD_DIR)/* $(RELEASE_BUILD_DIR)/*

clean-debug:
	rm -fr $(DEBUG_BUILD_DIR)/$(SOB_LIB_NAME) $(DEBUG_BUILD_DIR)/src
	
clean-release:
	rm -fr $(RELEASE_BUILD_DIR)/$(SOB_LIB_NAME) $(RELEASE_BUILD_DIR)/src

clean-functional-test:
	rm -fr $(DEBUG_BUILD_DIR)/FunctionalTest $(DEBUG_BUILD_DIR)/test
		
clean-performance-test:
	rm -fr $(RELEASE_BUILD_DIR)/PerformanceTest $(RELEASE_BUILD_DIR)/test

clean-debug-test:
	rm -fr $(DEBUG_BUILD_DIR)/SimpleOrderbookTest $(DEBUG_BUILD_DIR)/test
		
clean-release-test:
	rm -fr $(RELEASE_BUILD_DIR)/SimpleOrderbookTest $(RELEASE_BUILD_DIR)/test
				
.PHONY : all performance-test functional-test release debug \
         clean clean-debug clean-release clean-functional-test \
         clean-performance-test clean-debug-test clean-release-test


	
