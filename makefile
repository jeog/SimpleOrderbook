PROJECT_ROOT = $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

BUILD_DIR := bin
DEBUG_BUILD_DIR := $(BUILD_DIR)/debug
RELEASE_BUILD_DIR := $(BUILD_DIR)/release


SOB_LIB_OBJS += \
advanced_order.o \
simpleorderbook.o 

DEBUG_SOB_LIB_OBJS = $(addprefix $(DEBUG_BUILD_DIR)/, $(SOB_LIB_OBJS))
RELEASE_SOB_LIB_OBJS = $(addprefix $(RELEASE_BUILD_DIR)/, $(SOB_LIB_OBJS))


SOB_TEST_OBJS += \
test/test.o \
test/performance/performance.o \
test/performance/random.o \
test/performance/tests/insert.o \
test/performance/tests/pull.o \
test/functional/functional.o \
test/functional/tests/basic_orders.o \
test/functional/tests/orderbook.o \
test/functional/tests/pull_replace.o \
test/functional/tests/advanced_orders/bracket.o \
test/functional/tests/advanced_orders/fill_or_kill.o \
test/functional/tests/advanced_orders/one_cancels_other.o \
test/functional/tests/advanced_orders/one_triggers_other.o \
test/functional/tests/advanced_orders/trailing_bracket.o \
test/functional/tests/advanced_orders/trailing_stop.o  

DEBUG_SOB_TEST_OBJS = $(addprefix $(DEBUG_BUILD_DIR)/, $(SOB_TEST_OBJS))
RELEASE_SOB_TEST_OBJS = $(addprefix $(RELEASE_BUILD_DIR)/, $(SOB_TEST_OBJS))


TEST_SUBDIRS = \
test \
test/performance \
test/performance/tests \
test/functional \
test/functional/tests \
test/functional/tests/advanced_orders

DEBUG_TEST_SUBDIRS = $(addprefix $(DEBUG_BUILD_DIR)/, $(TEST_SUBDIRS))
RELEASE_TEST_SUBDIRS = $(addprefix $(RELEASE_BUILD_DIR)/, $(TEST_SUBDIRS))


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

$(DEBUG_BUILD_DIR)/%.o : src/%.cpp | $(DEBUG_BUILD_DIR)
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ $(CXXFLAGS) -c -fPIC -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '			
	
$(DEBUG_BUILD_DIR):	
	mkdir -p $@	
	
	
$(RELEASE_BUILD_DIR)/$(SOB_LIB_NAME): $(RELEASE_SOB_LIB_OBJS) 
	@echo 'Building target: $@'
	@echo 'Invoking: GCC Archiver'
	ar -r  "$(RELEASE_BUILD_DIR)/$(SOB_LIB_NAME)" $(RELEASE_SOB_LIB_OBJS) 
	@echo 'Finished building target: $@'
	@echo ' '
		
$(RELEASE_BUILD_DIR)/%.o : src/%.cpp | $(RELEASE_BUILD_DIR)
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ $(CXXFLAGS) -c -fPIC -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

$(RELEASE_BUILD_DIR):	
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
	rm -fr $(DEBUG_BUILD_DIR)/*
	
clean-release:
	rm -fr $(RELEASE_BUILD_DIR)/*
	
.PHONY : all performance-test functional-test release debug clean clean-debug clean-release


	
