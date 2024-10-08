#include "cm_system_control_space.h"
#include "error_code.h"
#include "arm_v7m_ins_implement.h"
#include <stdlib.h>
#include <string.h>

#define ud_read(offset, buffer, size, scs) \
    (scs)->user_defined_read ? (scs)->user_defined_read(offset, buffer, size, scs) : -1

#define ud_write(offset, buffer, size, scs) \
    (scs)->user_defined_write ? (scs)->user_defined_write(offset, buffer, size, scs) : -1

#define GET_NVIC_INFO(scs) ((cm_NVIC_t*)(scs)->NVIC->controller_info)

/* Interrupt Controller Type Register */
int ICTR(uint8_t* data, int rw_flag, cm_scs_t *scs)
{
    if(rw_flag == MEM_READ){
        *(uint32_t*)data = GET_NVIC_INFO(scs)->interrupt_lines;
        return 0;
    }else{
        return -1;
    }
}

/* Application Interrupt and Reset Control Register */
int AIRCR(uint8_t *data, int rw_flag, cm_scs_t *scs)
{
    if(rw_flag == MEM_READ){
        int endian = scs->config.endianess;
        int prigroup = scs->config.prigroup;
        *(uint32_t *)data = (0xFA05 << 16) | (endian << 15) | (prigroup << 8);
        return 0;
    }else{
        uint32_t val = *(uint32_t *)data;
        uint32_t vectorkey = LOW_BIT32(val >> 16, 16);
        if(vectorkey == 0x05FA){
            scs->config.endianess = LOW_BIT32(val >> 15, 1);
            scs->config.prigroup  = LOW_BIT32(val >> 8,  3);
            return 0;
        }else{
            return -1;
        }
    }
}

/* Auxiliary Control Register */

/* Software Triggered Interrupt Register */
int STIR(uint8_t *data, int rw_flag, cm_scs_t *scs)
{
    if(rw_flag == MEM_READ){
        return -1;
    }else{
        uint32_t excep_num = *(uint32_t*)data;
        scs->NVIC->throw_exception(excep_num & 0x1FF, scs->NVIC);
        return 0;
    }
}

cm_scs_t* create_cm_scs()
{
    cm_scs_t *scs = (cm_scs_t*)malloc(sizeof(cm_scs_t));
    memset(scs, 0, sizeof(cm_scs_t));
    return scs;
}

#define CM_SCS_REGS_ACCESS(rw_flag, user_defined_access) \
do{\
    cm_scs_t *scs = (cm_scs_t*)region->region_data;\
    switch(offset){\
    /* system control not in SCB */\
    case 0x004:\
        return ICTR(buffer, rw_flag, scs);\
    case 0x008:\
        return user_defined_access(offset, buffer, size, scs);\
    /* System Tick */\
    case 0x010:\
        return SYST_CSR(buffer, rw_flag, scs);\
    case 0x014:\
        return SYST_RVR(buffer, rw_flag, scs);\
    case 0x018:\
        return SYST_CVR(buffer, rw_flag, scs);\
    case 0x1C:\
        return SYST_CALIB(buffer, rw_flag, scs);\
    /* NVIC */\
    case 0x100:\
        /* NVIC_ISR0*/\
        /* ...*/\
    case 0x13C:\
        /* NVIC_ISR15*/\
    case 0x180:\
        /* NVIC_ICER0*/\
        /* ...*/\
    case 0x1BC:\
        /* NVIC_ICER15*/\
    case 0x200:\
        /* NVIC_ISPR0*/\
        /* ...*/\
    case 0x23C:\
        /* NVIC_ISPR15*/\
    case 0x280:\
        /* NVIC_ICPR0*/\
        /* ...*/\
    case 0x2BC:\
        /* NVIC_ICPR15*/\
    case 0x300:\
        /* NVIC_IABR0*/\
        /* ...*/\
    case 0x37C:\
        /* NVIC_IABR15*/\
    case 0x400:\
        /* NVIC_IPR0*/\
        /* ...*/\
    case 0x7EC:\
        /* NVIC_IPR123*/\
    /* SCB */\
    case 0xD00:\
        /* CPUID*/\
    case 0xD04:\
        /* ICSR*/\
    case 0xD08:\
        /* VTOR*/\
    case 0xD0C:\
        return AIRCR(buffer, rw_flag, scs);\
    case 0xD10:\
        /* SCR*/\
    case 0xD14:\
        /* CCR*/\
    case 0xD18:\
        /* SHPR1*/\
    case 0xD1C:\
        /* SHPR2*/\
    case 0xD20:\
        /* SHPR3*/\
    case 0xD24:\
        /* SHCSR*/\
    case 0xD28:\
        /* CFSR*/\
    case 0xD2C:\
        /* HFSR*/\
    case 0xD30:\
        /* DFSR*/\
    case 0xD34:\
        /* MMFAR*/\
    case 0xD38:\
        /* BFAR*/\
    case 0xD3C:\
        /* AFSR*/\
    /***************
    Implementation defined CPUID reigsters
    case 0xD00:
    ...
    case 0xD7C:
    ***************/\
    case 0xD88:\
        /* CPACR*/\
    /* MPU */\
    case 0xD90:\
        /* MPU_TYPE*/\
    case 0xD94:\
        /* MPU_CTRL*/\
    case 0xD98:\
        /* MPU_RNR*/\
    case 0xD9C:\
        /* MPU_RBAR*/\
    case 0xDA0:\
        /* MPU_RASR*/\
    case 0xDA4:\
        /* MPU_RBAR_A1*/\
    case 0xDA8:\
        /* MPU_RASR_A1*/\
    case 0xDAC:\
        /* MPU_RBAR_A2*/\
    case 0xDB0:\
        /* MPU_RASR_A2*/\
    case 0xDB4:\
        /* MPU_RBAR_A3*/\
    case 0xDB8:\
        /* MPU_RASR_A3*/\
    /* Debug register */\
    case 0xDF0:\
        /* DHCSR*/\
    case 0xDF4:\
        /* DCRSR*/\
    case 0xDF8:\
        /* DCRDR*/\
    case 0xDFC:\
        /* DEMCR*/\
    /* software trigger interrupt not in SCB */\
    case 0xF00:\
        return STIR(buffer, rw_flag, scs);\
    /* FP extension */\
    case 0xF34:\
        /* FPCCR*/\
    case 0xF38:\
        /* FPCAR*/\
    case 0xF3C:\
        /* FPDSCR*/\
    case 0xF40:\
        /* MVFR0*/\
    case 0xF44:\
        /* MVFR1*/\
    /****************
    Implementation defined
    case 0xF90:
    case 0xFCF:
    ****************/\
    /* MC-spec ID registers not in SCB */\
    case 0xFD0:\
        /* PID4
         ...*/\
    case 0xFDC:\
        /* PID7*/\
    case 0xFE0:\
        /* PID0
         ...*/\
    case 0xFEC:\
        /* PID3*/\
    case 0xFF0:\
        /* CID0
         ...*/\
    case 0xFFC:\
        /* CID3*/\
    default:\
        return user_defined_access(offset, buffer, size, scs);\
    }\
}while(0)

int cm_scs_read(uint32_t offset, uint8_t *buffer, int size, memory_region_t *region)
{
    CM_SCS_REGS_ACCESS(MEM_READ, ud_read);
}

int cm_scs_write(uint32_t offset, uint8_t *buffer, int size, memory_region_t *region)
{
    CM_SCS_REGS_ACCESS(MEM_WRITE, ud_write);
}

int cm_scs_init(cpu_t *cpu) //,soc_conf_t* config)
{
    int retval;

    /* memory map must be initialed before calling this function */
    memory_map_t *memory = cpu->memory_map;
    if(memory == NULL){
        retval = -ERROR_MEMORY_MAP;
        goto no_memory;
    }

    memory_region_t *region = request_memory_region(memory, CM_SCS_BASE, CM_SCS_SIZE);
    if(region == NULL){
        retval = -ERROR_MEMORY_MAP;
        goto get_region_fail;
    }

    /* Now we can setup the scs fields */
    cm_scs_t *scs = create_cm_scs();
    //scs->user_defined_data =
    //scs->user_defined_read
    //scs->user_defined_write
    scs->NVIC = cm_NVIC_init(cpu);
    if(scs->NVIC == NULL){
        retval = -ERROR_CREATE;
        goto NVIC_init_fail;
    }

    /* set system_info in cpu so that other part of the system can visit scs */
    cpu->system_info = scs;
    /* set memory region data as well, we don't need the full cpu information when read and write scs */
    region->region_data = scs;
    region->read = cm_scs_read;
    region->write = cm_scs_write;
    region->type = MEMORY_REGION_SYS;

    /* some configs */
    scs->config.endianess = _LITTLE_ENDIAN;
    scs->config.prigroup  = 1;
    /* enable Debug Halting and disable Debug Exception default, refering to page 830 */
    scs->regs.DHCSR = 0x00000001;

    /* systick regiters */
    scs->systick_regs.SYST_CSR = 0x00000002; // TICKINT
    scs->cpu = cpu;

    return SUCCESS;

NVIC_init_fail:
get_region_fail:
no_memory:
    return retval;
}
