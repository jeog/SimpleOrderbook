################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../test/functional/functional.cpp 

OBJS += \
./test/functional/functional.o 

CPP_DEPS += \
./test/functional/functional.d 


# Each subdirectory must supply rules for building sources it contributes
test/functional/%.o: ../test/functional/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ -O3 -Wall -c -fmessage-length=0 -std=c++0x -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


