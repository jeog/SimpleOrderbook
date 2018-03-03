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
	g++ -std=c++11 -O0 -g3 -Wall -c -fmessage-length=0 -DDEBUG -ftemplate-backtrace-limit=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


