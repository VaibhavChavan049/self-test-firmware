################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Each subdirectory must supply rules for building sources it contributes
%.obj: ../%.c $(GEN_OPTS) | $(GEN_FILES) $(GEN_MISC_FILES)
	@echo 'C2000 Compiler - building file: "$<"'
	"/Applications/ti/ccs2100/ccs/tools/compiler/ti-cgt-c2000_25.11.1.LTS/bin/cl2000" -v28 -ml -mt --cla_support=cla2 --float_support=fpu64 --tmu_support=tmu1 --vcu_support=vcrc -Ooff --include_path="/Users/vaibhavchavan/Documents/self-test firmware/Driverlib Empty CPU1 Example CCS Project" --include_path="/Users/vaibhavchavan/ti/C2000Ware_26_01_00_00" --include_path="/Users/vaibhavchavan/Documents/self-test firmware/Driverlib Empty CPU1 Example CCS Project/device" --include_path="/Users/vaibhavchavan/ti/C2000Ware_26_01_00_00/driverlib/f28p65x/driverlib/" --include_path="/Applications/ti/ccs2100/ccs/tools/compiler/ti-cgt-c2000_25.11.1.LTS/include" --define=_FLASH --define=DEBUG --define=CPU1 --diag_suppress=10063 --diag_warning=225 --diag_wrap=off --display_error_number --gen_func_subsections=on --abi=eabi --preproc_with_compile --preproc_dependency="$(basename $(<F)).d_raw" --include_path="/Users/vaibhavchavan/Documents/self-test firmware/Driverlib Empty CPU1 Example CCS Project/CPU1_FLASH/syscfg" $(GEN_OPTS__FLAG) "$<"
	@echo 'Finished building: "$<"'
	@echo ' '

build-598630378: ../c2000.syscfg
	@echo 'SysConfig - building file: "$<"'
	"/Applications/ti/ccs2100/ccs/utils/sysconfig_1.28.0/sysconfig_cli.sh" -s "/Users/vaibhavchavan/ti/C2000Ware_26_01_00_00/.metadata/sdk.json" -d "F28P65x" -p "256ZEJ" -r "F28P65x_256ZEJ" --script "/Users/vaibhavchavan/Documents/self-test firmware/Driverlib Empty CPU1 Example CCS Project/c2000.syscfg" -o "syscfg" --compiler ccs
	@echo 'Finished building: "$<"'
	@echo ' '

syscfg/board.c: build-598630378 ../c2000.syscfg
syscfg/board.h: build-598630378
syscfg/board.cmd.genlibs: build-598630378
syscfg/board.opt: build-598630378
syscfg/board.json: build-598630378
syscfg/pinmux.csv: build-598630378
syscfg/c2000ware_libraries.cmd.genlibs: build-598630378
syscfg/c2000ware_libraries.opt: build-598630378
syscfg/c2000ware_libraries.c: build-598630378
syscfg/c2000ware_libraries.h: build-598630378
syscfg/clocktree.h: build-598630378
syscfg: build-598630378

syscfg/%.obj: ./syscfg/%.c $(GEN_OPTS) | $(GEN_FILES) $(GEN_MISC_FILES)
	@echo 'C2000 Compiler - building file: "$<"'
	"/Applications/ti/ccs2100/ccs/tools/compiler/ti-cgt-c2000_25.11.1.LTS/bin/cl2000" -v28 -ml -mt --cla_support=cla2 --float_support=fpu64 --tmu_support=tmu1 --vcu_support=vcrc -Ooff --include_path="/Users/vaibhavchavan/Documents/self-test firmware/Driverlib Empty CPU1 Example CCS Project" --include_path="/Users/vaibhavchavan/ti/C2000Ware_26_01_00_00" --include_path="/Users/vaibhavchavan/Documents/self-test firmware/Driverlib Empty CPU1 Example CCS Project/device" --include_path="/Users/vaibhavchavan/ti/C2000Ware_26_01_00_00/driverlib/f28p65x/driverlib/" --include_path="/Applications/ti/ccs2100/ccs/tools/compiler/ti-cgt-c2000_25.11.1.LTS/include" --define=_FLASH --define=DEBUG --define=CPU1 --diag_suppress=10063 --diag_warning=225 --diag_wrap=off --display_error_number --gen_func_subsections=on --abi=eabi --preproc_with_compile --preproc_dependency="syscfg/$(basename $(<F)).d_raw" --include_path="/Users/vaibhavchavan/Documents/self-test firmware/Driverlib Empty CPU1 Example CCS Project/CPU1_FLASH/syscfg" --obj_directory="syscfg" $(GEN_OPTS__FLAG) "$<"
	@echo 'Finished building: "$<"'
	@echo ' '


