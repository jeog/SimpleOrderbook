################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../test/functional/tests/basic_orders.cpp \
../test/functional/tests/orderbook.cpp \
../test/functional/tests/pull_replace.cpp 

OBJS += \
./test/functional/tests/basic_orders.o \
./test/functional/tests/orderbook.o \
./test/functional/tests/pull_replace.o 

CPP_DEPS += \
./test/functional/tests/basic_orders.d \
./test/functional/tests/orderbook.d \
./test/functional/tests/pull_replace.d 


# Each subdirectory must supply rules for building sources it contributes
test/functional/tests/%.o: ../test/functional/tests/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ -std=c++11 -O0 -g3 -Wall -c -fmessage-length=0 -DDEBUG -ftemplate-backtrace-limit=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


