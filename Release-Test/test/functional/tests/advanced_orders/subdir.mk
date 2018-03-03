################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../test/functional/tests/advanced_orders/bracket.cpp \
../test/functional/tests/advanced_orders/fill_or_kill.cpp \
../test/functional/tests/advanced_orders/one_cancels_other.cpp \
../test/functional/tests/advanced_orders/one_triggers_other.cpp \
../test/functional/tests/advanced_orders/trailing_bracket.cpp \
../test/functional/tests/advanced_orders/trailing_stop.cpp 

OBJS += \
./test/functional/tests/advanced_orders/bracket.o \
./test/functional/tests/advanced_orders/fill_or_kill.o \
./test/functional/tests/advanced_orders/one_cancels_other.o \
./test/functional/tests/advanced_orders/one_triggers_other.o \
./test/functional/tests/advanced_orders/trailing_bracket.o \
./test/functional/tests/advanced_orders/trailing_stop.o 

CPP_DEPS += \
./test/functional/tests/advanced_orders/bracket.d \
./test/functional/tests/advanced_orders/fill_or_kill.d \
./test/functional/tests/advanced_orders/one_cancels_other.d \
./test/functional/tests/advanced_orders/one_triggers_other.d \
./test/functional/tests/advanced_orders/trailing_bracket.d \
./test/functional/tests/advanced_orders/trailing_stop.d 


# Each subdirectory must supply rules for building sources it contributes
test/functional/tests/advanced_orders/%.o: ../test/functional/tests/advanced_orders/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ -O3 -Wall -c -fmessage-length=0 -std=c++0x -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


