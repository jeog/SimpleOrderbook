################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../test/performance/performance.cpp \
../test/performance/random.cpp 

OBJS += \
./test/performance/performance.o \
./test/performance/random.o 

CPP_DEPS += \
./test/performance/performance.d \
./test/performance/random.d 


# Each subdirectory must supply rules for building sources it contributes
test/performance/%.o: ../test/performance/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ -std=c++11 -O0 -g3 -Wall -c -fmessage-length=0 -DDEBUG -ftemplate-backtrace-limit=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


