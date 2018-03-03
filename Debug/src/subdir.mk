################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../src/advanced_order.cpp \
../src/simpleorderbook.cpp 

OBJS += \
./src/advanced_order.o \
./src/simpleorderbook.o 

CPP_DEPS += \
./src/advanced_order.d \
./src/simpleorderbook.d 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ -std=c++11 -O0 -g3 -Wall -c -fmessage-length=0 -DDEBUG -ftemplate-backtrace-limit=0 -fPIC -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


