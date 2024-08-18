#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "_types.h"
#include "arm_gdb_stub.h"
#include "arm_v7m_ins_decode.h"
#include "arm_v7m_ins_implement.h"
#include "cm_system_control_space.h"
#include <string.h>


#define MAX_ARM_REG_NUM 32

#define CHECKSUM_OK 1
#define CHECKSUM_FAIL -1
#define CHECKSUM_NONE 0

gdb_stub_t *create_stub()
{
    gdb_stub_t *stub = (gdb_stub_t *)malloc(sizeof(gdb_stub_t));
    if(stub == NULL){
        goto err_out;
    }
    memset(stub, 0, sizeof(gdb_stub_t));

    hash_t *sw_bp = create_hash(MAX_BP_TABLE_SIZE);
    if(sw_bp == NULL){
        goto sw_bp_error;
    }

    stub->sw_breakpoint = sw_bp;
    stub->port = 4331;
    return stub;

sw_bp_error:
    free(stub);
err_out:
    return NULL;
}

int wait_for_rsp_client(gdb_stub_t *stub)
{
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    int addrlen = sizeof(client_addr);
    // accept
    if((stub->client = accept(stub->server, (struct sockaddr*)&client_addr, &addrlen)) == /*INVALID_SOCKET*/-1){
        LOG(LOG_ERROR, "fail to accept socket\n");
        return -1;
    }
    LOG(LOG_DEBUG, "Accept connection from %s\n", inet_ntoa(client_addr.sin_addr));
    return 0;
}

int init_stub(gdb_stub_t *stub)
{
#if 0
    WSADATA wsa;

    // initialize windows socket dll
    if(WSAStartup(MAKEWORD(2,2),&wsa) != 0){
        LOG(LOG_ERROR, "fail to start windows socket\n");
        return -1;
    }
#endif
    // create socket
    if((stub->server = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP)) == /*INVALID_SOCKET*/-1){
        LOG(LOG_ERROR, "fail to create socket\n");
        return -1;
    }
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(struct sockaddr_in));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr/*.S_un*/.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(stub->port);

    // bind
    if(bind(stub->server,(struct sockaddr*)&server_addr, sizeof(server_addr)) == /*SOCKET_ERROR*/-1){
        LOG(LOG_ERROR, "fail to bind port %d\n", stub->port);
        goto bind_fail;
    }

    // listen
    if(listen(stub->server, SOMAXCONN) == /*SOCKET_ERROR*/-1){
        LOG(LOG_ERROR, "fail to listen port %d\n", stub->port);
        goto listen_fail;
    }
    LOG(LOG_DEBUG, "Server %d is listening......\n", stub->port);

    if(wait_for_rsp_client(stub) < 0){
        goto wait_fail;
    }
    return 0;

wait_fail:
listen_fail:
bind_fail:
#if 0
    WSACleanup();
#endif
    return -1;
}

static int char_to_hex(char c)
{
    if(c >= '0' && c <= '9'){
        return c - '0';
    }else if(c >= 'A' && c <= 'F'){
        return c - 'A' + 0xA;
    }else if(c >= 'a' && c <= 'f'){
        return c - 'a' + 0xA;
    }else{
        return -1;
    }
}

static char hex_to_char(int hex)
{
    if(hex >= 0 && hex <= 9){
        return hex + '0';
    }else if(hex >= 0xA && hex <= 0xF){
        return hex - 0xA + 'a';
    }
    return -1;
}

static uint32_t strtohex_u32(char *str)
{
    uint32_t result = 0;
    while(*str != '\0'){
        result *= 0x10;
        result += char_to_hex(*str);
        str++;
    }
    return result;
}

/* the input pointer will pointer to '#' when the function return */
static uint8_t calc_checksum(IOput char **packet)
{
    while(**packet == '+' || **packet == '$'){
        (*packet)++;
    }

    int sum = 0;
    while(**packet != '#'){
        sum += **packet;
        (*packet)++;
    }
    return sum;
}

static int validate_checksum(char *packet)
{
    int checksum = calc_checksum(&packet);

    int xchecksum = 0;
    while(*packet == '#'){
        packet++;
    }
    while(*packet != '\0'){
        xchecksum *= 16;
        xchecksum += char_to_hex(*packet++);
    }

    if(checksum == xchecksum){
        return CHECKSUM_OK;
    }else{
        return CHECKSUM_FAIL;
    }
}

/* get packet via socket */
static int get_packet(gdb_stub_t *stub)
{
    stub->recv_len = recv(stub->client, stub->recv_buf, sizeof(stub->recv_buf), 0);
    stub->recv_buf[stub->recv_len]='\0';
    return stub->recv_len;
}

/* send packet via socket */
static int put_packet(gdb_stub_t *stub)
{
    int ret = send(stub->client, stub->send_buf, stub->send_len, 0);
    return ret;
}

/* generate ack and the head of packet */
static void make_packet_head(gdb_stub_t *stub, int checksum_ok)
{
    stub->send_len = 0;
    if(checksum_ok > 0){
        stub->send_buf[0] = '+';
        stub->send_len++;
    }else if(checksum_ok < 0){
        stub->send_buf[0] = '-';
        stub->send_len++;
    }

    stub->send_buf[stub->send_len++] = '$';
}

static void make_check_packet(gdb_stub_t *stub, int checksum_ok)
{
    stub->send_len = 0;
    if(checksum_ok > 0){
        stub->send_buf[0] = '+';
        stub->send_len++;
    }else if(checksum_ok < 0){
        stub->send_buf[0] = '-';
        stub->send_len++;
    }
}

/* calculate checksum and fill the tail of the packet */
static void make_packet_tail(gdb_stub_t *stub)
{
    stub->send_buf[stub->send_len++] = '#';
    char *packet = stub->send_buf;
    int checksum = calc_checksum(&packet);
    char char_checksum[2];
    char_checksum[0] = hex_to_char(checksum / 0x10);
    char_checksum[1] = hex_to_char(checksum % 0x10);

    int i;
    for(i = 0; i < 2; i++){
        stub->send_buf[stub->send_len++] = char_checksum[i];
    }
    stub->send_buf[stub->send_len] = '\0';
}

/* append param to the send buffer */
static int make_packet(gdb_stub_t* stub, const char *format, ...)
{
    va_list arg;
    int done;
    va_start(arg, format);
    done = vsnprintf(stub->send_buf + stub->send_len, MAX_PACKET_SIZE - stub->send_len, format, arg);
    va_end(arg);
    stub->send_len = strlen(stub->send_buf);
    return done;
}

/* get value of registers and set send buffer */
static void get_registers(gdb_stub_t *stub, int start, int end, cpu_t *cpu)
{
    arm_reg_t *regs = ARMv7m_GET_REGS(cpu);
    char reg_val[10];
    uint32_t reg;
    int i;

    if(start < 0 || end > 31){
        return;
    }

    for(i = start; i <= end; i++){
        if(((cm_scs_t *)cpu->system_info)->config.endianess == _LITTLE_ENDIAN){
            reg = htonl(regs->R[i]);
        }else{
            reg = regs->R[i];
        }
        sprintf(reg_val, "%08x", reg);
        make_packet(stub, reg_val);
    }
}

static int set_registers(char *buf, int start, int end, cpu_t *cpu)
{
    int i, k;
    // check the content length
    for(i = 0; ;i++){
        if(buf[i] == '#'){
            break;
        }
    }
    if(i % 8 != 0){
        return -1;
    }

    arm_reg_t *regs = ARMv7m_GET_REGS(cpu);
    char hex[9] = {0};
    for(i = start; i <= end; i++){
        for(k = 0; k < 4; k++){
            hex[7 - k * 2 - 1] = *buf++;
            hex[7 - k * 2] = *buf++;
        }
        regs->R[i] = strtohex_u32(hex);
        if(*buf == '#'){
            break;
        }
    }

    return 0;
}

/* division describe how to treat the paramter and how to stop division
   division has the format like <kind><symbol1><symbol2>...

   in the kind partition:
   s - string
   x - hex

   in the symbol partition:
   null - process to the end

   return the amount of parameters parsed.

   for example: "x," means the paramter is a hex value, and should divided by ','
                "s,;" means the paramter is a string, and should divided by frist ',' then ';' */
static int next_division(char **buf, char *division, void *param)
{
    if(**buf == '#'){
        return 0;
    }

    int len = strlen(division);
    if(len < 1){
        return -1;
    }

    switch(division[0]){
    case 's':
    case 'x':
        break;
    default:
        return -1;
    }

    char symbol = division[1];
    char *start;
    int i;
    for(i = 0; symbol != '\0'; i++){
        start = *buf;
        while(**buf != symbol && **buf != '#'){
            if(division[0] == 's'){
                *(char *)param++ = **buf;
            }
            (*buf)++;
        }
        if(division[0] == 'x'){
            **buf = '\0';
            ((uint32_t *)param)[i] = strtohex_u32(start);
            **buf = symbol;
            // I want to leave '#' in the buf when goes to the end of the parameters
            if(symbol == '#'){
                i++;
                break;
            }
            (*buf)++;
        }
        symbol = division[i + 2];
    }

    if(division[0] == 's'){
        *(char *)param++ = '\0';
        (*buf)++;
    }

    return i;
}

/* read memory from cpu and set the send buffer */
static int read_mem(gdb_stub_t *stub, uint32_t addr, uint32_t size, cpu_t *cpu)
{
    if(size * 2 > MAX_PACKET_SIZE - stub->send_len){
        return -1;
    }
    uint8_t * buf = (uint8_t *)malloc(size);
    int retval = armv7m_get_memory_direct(addr, size, buf, cpu);
    int i;
    if(retval > 0){
        for(i = 0; i < size; i++){
            stub->send_buf[stub->send_len++] = hex_to_char(buf[i] / 0x10);
            stub->send_buf[stub->send_len++] = hex_to_char(buf[i] % 0x10);
        }
    }
    free(buf);
    return retval;
}

static int write_mem(uint32_t addr, uint32_t size, char *data, cpu_t *cpu)
{
    char *recv_buf = data;
    char *start;
    char temp_ch;
    uint8_t hex;
    int i, ret;
    for(i = 0; i < size; i++){
        start = recv_buf;
        recv_buf += 2;
        temp_ch = *recv_buf;
        *recv_buf = '\0';
        hex = strtohex_u32(start);
        *recv_buf = temp_ch;
        ret = armv7m_set_memory_direct(addr + i, 1, &hex, cpu);
        if(ret < 0){
            return ret;
        }
    }
    return 0;
}

int bp_do_hash(hash_t *hash, int key)
{
    return (uint32_t)key % hash->max_size;
}

/* add break point to the table */
static int add_breakpoint(hash_t *hash, uint32_t addr, uint32_t mem)
{
    return hash_insert(hash, addr, (hash_data_t)mem, bp_do_hash);
}

static int set_memory_breakpoint(gdb_stub_t *stub, uint32_t addr, int kind, cpu_t *cpu)
{
    int ret;
    uint32_t break_opcode;
    uint32_t mem;
    switch(kind){
    case 2:
        break_opcode = 0xBE00;
        armv7m_get_memory_direct(addr, 2, (uint8_t *)&mem, cpu);
        ret = add_breakpoint(stub->sw_breakpoint, addr, mem);
        if(ret < 0){
            goto out;
        }
        ret = armv7m_set_memory_direct(addr, 2, (uint8_t *)&break_opcode, cpu);
        //armv7m_get_memory_direct(addr, 2, (uint8_t *)&mem, cpu);
        //printf("read back memory %x\n", mem);
        if(ret < 0){
            goto out;
        }
        break;
    }
out:
    return ret;
}

static int clear_memory_breakpoint(gdb_stub_t *stub, uint32_t addr, int kind, cpu_t *cpu)
{
    uint32_t mem;
    int ret = 0;
    hash_element_t *elem;
    switch(kind){
    case 2:
        elem = hash_find(stub->sw_breakpoint, addr, bp_do_hash);
        if(elem == NULL){
            ret = -1;
            goto out;
        }
        mem = elem->data.uidata;
        printf("mem is %x\n", mem);
        ret = armv7m_set_memory_direct(addr, 2, (uint8_t *)&mem, cpu);
        if(ret < 0){
            goto out;
        }
    }
out:
    return ret;
}

static int break_at(gdb_stub_t *stub, int type, uint32_t addr, int kind, cpu_t *cpu)
{
    /* memory break point */
    if(type == 0){
        return set_memory_breakpoint(stub, addr, kind, cpu);
    }
    /* hardware break point */
    else if(type == 1){
    }
    /* write watch point */
    else if(type == 2){
    }
    /* read watch point */
    else if(type == 3){
    }
    /* access watch point */
    else if(type == 4){
    }

    return -1;
}

static int clear_break_at(gdb_stub_t *stub, int type, uint32_t addr, int kind, cpu_t *cpu)
{
    switch(type){
    case 0:
        return clear_memory_breakpoint(stub, addr, kind, cpu);
    default:
        return -1;
    }
}

bool_t is_sw_breakpoint(gdb_stub_t *stub, uint32_t addr)
{
    hash_element_t *elem = hash_find(stub->sw_breakpoint, addr, bp_do_hash);
    if(elem == NULL){
        return FALSE;
    }

    return TRUE;
}

/* the main handler for RSP */
int handle_rsp(gdb_stub_t *stub, cpu_t *cpu)
{
    // send reply to 'c' and 's' command
    if(stub->status == RSP_CONTINUE || stub->status == RSP_STEP){
        stub->status = RSP_HALTING;
        make_packet_head(stub, CHECKSUM_OK);
        make_packet(stub, "S05"); // SIGTRAP refering to /include/gdb/signal.def in gdb sources
        goto rest_send;
    }

    int bytes = get_packet(stub);
    if(bytes <= 0){
        return bytes;
    }

    char *buf = stub->recv_buf;
    LOG(LOG_DEBUG, "Received GDB command: %s\n", buf);

    // check the ack first
    // ack error: re-send the last packet
    if(*buf == '-'){
        LOG(LOG_DEBUG, "Received fail condition\n");
        put_packet(stub);
        return -1;
    }
    // single '+': ignore it
    else if(*buf == '+' && bytes == 1){
        return 0;
    }

    // start packet translate
    buf++;

    // check whether receive successfully
    int check_status = validate_checksum(buf);
    make_check_packet(stub, check_status);
    put_packet(stub);
    if(check_status == CHECKSUM_FAIL){
        return -2;
    }

    make_packet_head(stub, CHECKSUM_NONE);

    // ignore starting $
    while(*buf == '$'){
        buf++;
    }

    uint32_t params[3];
    int retval;
    switch(*buf++){
    /* now buf points to the first character of the packet content */
    case 's':
        retval = next_division(&buf, "x#", params);
        if(retval == 1){
            cpu->set_raw_pc(params[0], cpu);
        }
        cpu->run_info.halting = FALSE;
        stub->status = RSP_STEP;
        goto direct_out;
    case 'c':
        retval = next_division(&buf, "x#", params);
        if(retval == 1){
            cpu->set_raw_pc(params[0], cpu);
        }
        cpu->run_info.halting = FALSE;
        stub->status = RSP_CONTINUE;
        goto direct_out;
    case 'z':
        next_division(&buf, "x,,#", params);
        retval = clear_break_at(stub, params[0], params[1], params[2], cpu);
        if(retval < 0){
            LOG(LOG_ERROR, "fail to restore break at %x\n", params[1]);
            make_packet(stub, "E0");
        }else{
            LOG(LOG_DEBUG, "restore  at %x\n", params[1]);
            make_packet(stub, "OK");
        }
        break;
    case 'Z':
        retval = next_division(&buf, "x,,#", params);
        printf("%d\n", retval);
        retval = break_at(stub, params[0], params[1], params[2], cpu);
        if(retval < 0){
            LOG(LOG_ERROR, "fail to add break at %x\n", params[1]);
            make_packet(stub, "E0");
        }else{
            LOG(LOG_DEBUG, "break at %x\n", params[1]);
            make_packet(stub, "OK");
        }
        break;
    case 'm':
        next_division(&buf, "x,#", params);
        retval = read_mem(stub, params[0], params[1], cpu);
        if(retval < 0){
            make_packet(stub, "E30");
            break;
        }
        break;
    case 'M':
        next_division(&buf, "x,:", params);
        retval = write_mem(params[0], params[1], buf, cpu);
        if(retval < 0){
            make_packet(stub, "E30");
        }else{
            make_packet(stub, "OK");
        }
        break;
    case 'p':
        next_division(&buf, "x#", params);
        get_registers(stub, params[0], params[0], cpu);
        break;
    case 'P':
        next_division(&buf, "x=", params);
        retval = set_registers(buf, params[0], params[0], cpu);
        if(retval < 0){
            make_packet(stub, "E3");
        }else{
            make_packet(stub, "OK");
        }
        break;
    case 'g':
        get_registers(stub, 0, MAX_ARM_REG_NUM - 1, cpu);
        break;
    case 'G':
        retval = set_registers(buf, 0, MAX_ARM_REG_NUM - 1, cpu);
        if(retval < 0){
            make_packet(stub, "E3");
        }else{
            make_packet(stub, "OK");
        }
        break;
    case '?':
        make_packet(stub, "S05"); // SIGTRAP refering to /include/gdb/signal.def in gdb sources
        break;
    case 'q':
        if(strncmp(buf, "Supported", 9) == 0){
            make_packet(stub, "PacketSize=%X", MAX_PACKET_SIZE);
        }
        break;
    case 'k':
        LOG(LOG_WARN, "Client disconnected, wait for connection...\n");
        wait_for_rsp_client(stub);
        goto direct_out;
    }

rest_send:
    make_packet_tail(stub);
    LOG(LOG_DEBUG, "%s\n", stub->send_buf);

    put_packet(stub);

direct_out:
    return 0;
}

