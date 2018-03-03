################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../test/performance/tests/insert.cpp \
../test/performance/tests/pull.cpp 

OBJS += \
./test/performance/tests/insert.o \
./test/performance/tests/pull.o 

CPP_DEPS += \
./test/performance/tests/insert.d \
./test/performance/tests/pull.d 


# Each subdirectory must supply rules for building sources it contributes
test/performance/tests/%.o: ../test/performance/tests/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ -O3 -Wall -c -fmessage-length=0 -std=c++0x -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


