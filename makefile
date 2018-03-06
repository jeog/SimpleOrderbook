PROJECT_ROOT = $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

BUILD_DIR := bin
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

# compiler options
# note: debug build for functional-test, release for performance-test
CXXFLAGS := -std=c++11 -Wall -fmessage-length=0 -ftemplate-backtrace-limit=0
debug : CXXFLAGS += -DDEBUG -g -O0
release : CXXFLAGS += -O3
functional-test : CXXFLAGS += -DDEBUG -g -O0
performance-test : CXXFLAGS += -O3

LIBS := -lpthread -ldl -lutil
SOB_LIB_NAME := libSimpleOrderbook.a


all: performance-test functional-test 

performance-test: $(RELEASE_BUILD_DIR)/$(SOB_LIB_NAME) $(RELEASE_BUILD_DIR)/PerformanceTest

functional-test: $(DEBUG_BUILD_DIR)/$(SOB_LIB_NAME) $(DEBUG_BUILD_DIR)/FunctionalTest

debug: $(DEBUG_BUILD_DIR)/$(SOB_LIB_NAME)

release: $(RELEASE_BUILD_DIR)/$(SOB_LIB_NAME)


$(DEBUG_BUILD_DIR)/$(SOB_LIB_NAME): $(DEBUG_SOB_LIB_OBJS) 
	@echo 'Building target: $@'
	@echo 'Invoking: GCC Archiver'
	ar -r  "$(DEBUG_BUILD_DIR)/$(SOB_LIB_NAME)" $(DEBUG_SOB_LIB_OBJS) 
	@echo 'Finished building target: $@'
	@echo ' '

$(DEBUG_BUILD_DIR)/src/%.o : src/%.cpp | $(DEBUG_SOB_LIB_SUBDIRS)
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ $(CXXFLAGS) -c -fPIC -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '			
	
$(DEBUG_SOB_LIB_SUBDIRS):	
	mkdir -p $@	
	

$(RELEASE_BUILD_DIR)/$(SOB_LIB_NAME): $(RELEASE_SOB_LIB_OBJS) 
	@echo 'Building target: $@'
	@echo 'Invoking: GCC Archiver'
	ar -r  "$(RELEASE_BUILD_DIR)/$(SOB_LIB_NAME)" $(RELEASE_SOB_LIB_OBJS) 
	@echo 'Finished building target: $@'
	@echo ' '
		
$(RELEASE_BUILD_DIR)/src/%.o : src/%.cpp | $(RELEASE_SOB_LIB_SUBDIRS)
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ $(CXXFLAGS) -c -fPIC -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

$(RELEASE_SOB_LIB_SUBDIRS):	
	mkdir -p $@	
			
	
$(DEBUG_BUILD_DIR)/FunctionalTest : $(DEBUG_SOB_TEST_OBJS) $(DEBUG_BUILD_DIR)/$(SOB_LIB_NAME)
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C++ Linker'
	g++ $(CXXFLAGS) -o "$(DEBUG_BUILD_DIR)/FunctionalTest" \
	$(LIBS) $(DEBUG_SOB_TEST_OBJS) $(DEBUG_BUILD_DIR)/$(SOB_LIB_NAME)
	@echo 'Finished building target: $@'
	@echo ' '

$(DEBUG_BUILD_DIR)/test/%.o : test/%.cpp | $(DEBUG_TEST_SUBDIRS)
	@echo 'Building file: $<'	
	@echo 'Invoking: GCC C++ Compiler'
	g++ $(CXXFLAGS) -c -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

$(DEBUG_TEST_SUBDIRS):	
	mkdir -p $@	
	
	
$(RELEASE_BUILD_DIR)/PerformanceTest : $(RELEASE_SOB_TEST_OBJS) $(RELEASE_BUILD_DIR)/$(SOB_LIB_NAME)
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C++ Linker'
	g++ $(CXXFLAGS) -o "$(RELEASE_BUILD_DIR)/PerformanceTest" \
	$(LIBS) $(RELEASE_SOB_TEST_OBJS) $(RELEASE_BUILD_DIR)/$(SOB_LIB_NAME)
	@echo 'Finished building target: $@'
	@echo ' '	
	
$(RELEASE_BUILD_DIR)/test/%.o : test/%.cpp | $(RELEASE_TEST_SUBDIRS) 
	@echo 'Building file: $<'	
	@echo 'Invoking: GCC C++ Compiler'
	g++ $(CXXFLAGS) -c -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
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
			
.PHONY : all performance-test functional-test release debug \
         clean clean-debug clean-release clean-functional-test clean-performance-test


	
