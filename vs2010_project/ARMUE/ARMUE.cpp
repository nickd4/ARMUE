// ARMUE.cpp : 定义控制台应用程序的入口点。
//
#include <stdio.h>
#include <tchar.h>
#include <stdlib.h>
#include <string.h>
#include "module_helper.h"

#include "memory_map.h"
#include "soc.h"

int _tmain(int argc, _TCHAR* argv[])
{
//	printf("%d\n",sizeof(short int));
	
	for(int i = 0; i < 100000; i++){
		// register all exsisted modules
		register_all_modules();

		// memory map
		rom_t* rom = alloc_rom();
		set_rom_size(rom, 2000);
		set_rom_base_addr(rom, 0x00);
		open_rom(_T("E:\\ARMUE\\test.rom"), rom);
		fill_rom_with_bin(rom, _T("E:\\ARMUE\\cortex_m3_test\\test.bin"));
		memory_map_t *memory_map = create_memory_map();		
		set_memory_map_rom(memory_map, rom, 0);
	

		// find the module that we need
		module_t* cpu_module = find_module(_T("arm_cm3"));
		module_t* xxx_module = find_module(_T("xxx"));
		
		// soc
		if(cpu_module != NULL){
			soc_t* soc = create_soc(cpu_module->create_cpu(), memory_map);
			startup_soc(soc);
			for(int j = 0;j < 4; j++){
				run_soc(soc);
			}
			destory_soc(&soc);
		}
		
		unregister_all_modules();
	}
}