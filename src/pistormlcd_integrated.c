#include <exec/types.h>
#include <exec/memory.h>
#include <exec/libraries.h>
#include <exec/io.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/i2c.h>
#include <proto/devicetree.h>
#include <stdint.h>
#include <stdio.h>

/* PiStorm32Lite / Emu68: combined LCD telemetry.
 * Six screens, 2 seconds each; temperature and voltage history
 * samples are collected at every screen transition.
 * 1602 HD44780 + PCF8574 at SendI2C 8-bit address 0x4E.
 */
APTR I2C_Base = NULL;
APTR MailBox = NULL;
APTR DeviceTreeBase = NULL;
#define LCD_ADDR 0x4E
#define LCD_RS 0x01
#define LCD_EN 0x04
#define LCD_BACKLIGHT 0x08
#define GRAPH_COL 6
#define GRAPH_WIDTH 10
#define BAR_COL 10
#define BAR_WIDTH 6
#define DISPLAY_DELAY 100
#define LCD_ENABLE_US 50
#define LCD_CMD_US 50
#define LCD_CLEAR_US 2000
#define MBOX_TX_FULL (1UL<<31)
#define MBOX_RX_EMPTY (1UL<<30)
#define MBOX_CHANMASK 0x0F
#define TAG_GET_TEMP 0x00030006UL
#define TAG_GET_VOLT 0x00030003UL

static UBYTE req_storage[256];
static UBYTE next_screen[2][16], current_screen[2][16];
static int temp_history[GRAPH_WIDTH], volt_history[GRAPH_WIDTH];
static struct MsgPort *timer_port;
static struct timerequest *timer_request;
static int timer_open;
static int glyph_mode = -1; /* 0 = vertical, 1 = horizontal */

static int init_timer(void)
{
    timer_port = CreateMsgPort();
    if (!timer_port) return 0;
    timer_request = (struct timerequest *)CreateIORequest(timer_port,sizeof(struct timerequest));
    if (!timer_request) { DeleteMsgPort(timer_port); timer_port=NULL; return 0; }
    if (OpenDevice(TIMERNAME,UNIT_MICROHZ,(struct IORequest *)timer_request,0)) {
        DeleteIORequest((struct IORequest *)timer_request);
        DeleteMsgPort(timer_port);
        timer_request=NULL; timer_port=NULL; return 0;
    }
    timer_open=1;
    return 1;
}
static void delay_us(ULONG us)
{
    timer_request->tr_node.io_Command=TR_ADDREQUEST;
    timer_request->tr_time.tv_secs=us/1000000UL;
    timer_request->tr_time.tv_micro=us%1000000UL;
    DoIO((struct IORequest *)timer_request);
}
static int pcf_write(UBYTE b)
{
    ULONG result=SendI2C(LCD_ADDR,1,&b);
    return (result & 1) ? 1 : 0;
}
static void lcd_pulse(UBYTE b)
{
    pcf_write(b | LCD_EN); delay_us(LCD_ENABLE_US);
    pcf_write(b & ~LCD_EN); delay_us(LCD_ENABLE_US);
}
static void lcd_nibble(UBYTE nibble,UBYTE mode)
{
    UBYTE b=(UBYTE)(((nibble & 15)<<4)|mode|LCD_BACKLIGHT);
    pcf_write(b); lcd_pulse(b);
}
static void lcd_byte(UBYTE b,UBYTE mode)
{
    lcd_nibble((UBYTE)(b>>4),mode);
    lcd_nibble((UBYTE)(b & 15),mode);
    delay_us(LCD_CMD_US);
}
static void lcd_command(UBYTE b)
{
    lcd_byte(b,0);
    if (b==1 || b==2) delay_us(LCD_CLEAR_US);
}
static void lcd_char(UBYTE b) { lcd_byte(b,LCD_RS); }
static void lcd_goto(UBYTE row,UBYTE col)
{
    lcd_command((UBYTE)((row ? 0xC0 : 0x80)+col));
}
static void lcd_init(void)
{
    pcf_write(LCD_BACKLIGHT); delay_us(50000);
    lcd_nibble(3,0); delay_us(5000);
    lcd_nibble(3,0); delay_us(5000);
    lcd_nibble(3,0); delay_us(5000);
    lcd_nibble(2,0); delay_us(5000);
    lcd_command(0x28); lcd_command(0x0C);
    lcd_command(0x01); lcd_command(0x06);
}
static void invalidate_screen(void)
{
    int r,c;
    for (r=0;r<2;r++) for (c=0;c<16;c++) current_screen[r][c]=0xFF;
}
static void clear_next_screen(void)
{
    int r,c;
    for (r=0;r<2;r++) for (c=0;c<16;c++) next_screen[r][c]=' ';
}
static void screen_text(int row,int col,const char *s,int width)
{
    int i,ended=0;
    for (i=0;i<width;i++) {
        UBYTE ch=' ';
        if (!ended) { if (!*s) ended=1; else ch=(UBYTE)*s++; }
        if (col+i>=0 && col+i<16) next_screen[row][col+i]=ch;
    }
}
static void flush_screen(void)
{
    int row,col;
    for (row=0;row<2;row++) {
        col=0;
        while (col<16) {
            if (next_screen[row][col]==current_screen[row][col]) { col++; continue; }
            lcd_goto((UBYTE)row,(UBYTE)col);
            do {
                lcd_char(next_screen[row][col]);
                current_screen[row][col]=next_screen[row][col];
                col++;
            } while (col<16 && next_screen[row][col]!=current_screen[row][col]);
        }
    }
}
static void load_graph_chars(void)
{
    int level,row;
    for (level=1;level<=8;level++) {
        lcd_command((UBYTE)(0x40+(level-1)*8));
        for (row=0;row<8;row++) lcd_char((UBYTE)(row>=8-level ? 0x1F:0));
    }
    lcd_command(0x80);
}
static void load_bar_chars(void)
{
    int width,row;
    for (width=1;width<=5;width++) {
        UBYTE pattern=(UBYTE)((0x1F << (5-width)) & 0x1F);
        lcd_command((UBYTE)(0x40+(width-1)*8));
        for (row=0;row<8;row++) lcd_char(pattern);
    }
    lcd_command(0x80);
}
/* HD44780 CGRAM is shared by all cells. Clear before redefining
 * glyphs so the previous page doesn't briefly show wrong symbols. */
static void set_glyph_mode(int mode)
{
    if (glyph_mode==mode) return;
    lcd_command(0x01);
    if (mode==0) load_graph_chars(); else load_bar_chars();
    glyph_mode=mode;
    invalidate_screen();
}
static void screen_graph(const int *history)
{
    int i,level,upper;
    for (i=0;i<GRAPH_WIDTH;i++) {
        level=history[i];
        if (level<0) level=0;
        if (level>16) level=16;
        next_screen[1][GRAPH_COL+i]=(level==0) ? ' ' : (level>=8 ? 7 : (UBYTE)(level-1));
        upper=level-8;
        next_screen[0][GRAPH_COL+i]=(upper<=0) ? ' ' : (UBYTE)(upper-1);
    }
}
static void screen_bar(int row,ULONG free_tenths)
{
    int col,pixels;
    if (free_tenths>1000) free_tenths=1000;
    pixels=(int)((free_tenths*30UL+500UL)/1000UL);
    for (col=0;col<BAR_WIDTH;col++) {
        int filled=pixels-col*5;
        next_screen[row][BAR_COL+col]=(filled<=0) ? ' ' : (UBYTE)((filled>=5) ? 4 : filled-1);
    }
}
static void add_history(int *history,int level)
{
    int i;
    for (i=0;i<GRAPH_WIDTH-1;i++) history[i]=history[i+1];
    history[GRAPH_WIDTH-1]=level;
}
static int temperature_to_level(ULONG mC)
{
    if (mC<=30000UL) return 0;
    if (mC>=85000UL) return 16;
    return (int)(((mC-30000UL)*16UL+27500UL)/55000UL);
}
static int voltage_to_level(ULONG uV)
{
    if (uV<=1100000UL) return 0;
    if (uV>=1500000UL) return 16;
    return (int)(((uV-1100000UL)*16UL+200000UL)/400000UL);
}
static inline uint32_t LE32(uint32_t x) { return __builtin_bswap32(x); }
static CONST_APTR GetPropValueRecursive(APTR key,CONST_STRPTR property)
{
    do {
        APTR prop=DT_FindProperty(key,property);
        if (prop) return DT_GetPropValue(prop);
        key=DT_GetParent(key);
    } while (key);
    return NULL;
}

static void InitMailBox(void)
{
    APTR key;

    DeviceTreeBase = OpenResource("devicetree.resource");

    if (!DeviceTreeBase) {
        printf("ERROR: devicetree.resource missing\n");
        return;
    }
    key=DT_OpenKey("/aliases");
    if (key) {
        CONST_STRPTR alias=NULL;
        APTR prop=DT_FindProperty(key,"mailbox");
        if (prop) alias=DT_GetPropValue(prop);
        DT_CloseKey(key);
        if (alias) {
            key=DT_OpenKey(alias);
            if (key) {
                int address_cells=1;
                const ULONG *addr=(const ULONG *)GetPropValueRecursive(key,"#address-cells");
                APTR regprop=DT_FindProperty(key,"reg");
                const ULONG *reg=regprop ? DT_GetPropValue(regprop) : NULL;
                if (addr) address_cells=*addr;
                if (reg && address_cells>=1) MailBox=(APTR)reg[address_cells-1];
                DT_CloseKey(key);
            }
        }
    }
    if (!MailBox) { printf("ERROR: mailbox address missing\n"); return; }
    key=DT_OpenKey("/soc");
    if (key) {
        int address_cells=1,cpu_address_cells=1;
        const ULONG *addr=(const ULONG *)GetPropValueRecursive(key,"#address-cells");
        const ULONG *cpu_addr=NULL,*reg;
        APTR root=DT_OpenKey("/");
        APTR prop;
        if (root) {
            prop=DT_FindProperty(root,"#address-cells");
            if (prop) cpu_addr=DT_GetPropValue(prop);
            DT_CloseKey(root);
        }
        if (addr) address_cells=*addr;
        if (cpu_addr) cpu_address_cells=*cpu_addr;
        prop=DT_FindProperty(key,"ranges");
        reg=prop ? DT_GetPropValue(prop) : NULL;
        if (reg && address_cells>=1 && cpu_address_cells>=1) {
            ULONG phys_vc4=reg[address_cells-1];
            ULONG phys_cpu=reg[address_cells+cpu_address_cells-1];
            MailBox=(APTR)((ULONG)MailBox-phys_vc4+phys_cpu);
        }
        DT_CloseKey(key);
    }
    printf("Mailbox: 0x%08lx\n",(ULONG)MailBox);
}
static uint32_t mbox_recv(uint32_t channel)
{
    volatile uint32_t *read_reg=(uint32_t *)MailBox;
    volatile uint32_t *status_reg=(uint32_t *)((uintptr_t)MailBox+0x18);
    uint32_t response,status;
    do {
        do { status=LE32(*status_reg); asm volatile("nop"); }
        while (status & MBOX_RX_EMPTY);
        response=LE32(*read_reg); asm volatile("nop");
    } while ((response & MBOX_CHANMASK)!=channel);
    return response & ~MBOX_CHANMASK;
}
static void mbox_send(uint32_t channel,uint32_t data)
{
    volatile uint32_t *write_reg=(uint32_t *)((uintptr_t)MailBox+0x20);
    volatile uint32_t *status_reg=(uint32_t *)((uintptr_t)MailBox+0x18);
    uint32_t status;
    data=(data & ~MBOX_CHANMASK) | (channel & MBOX_CHANMASK);
    do { status=LE32(*status_reg); asm volatile("nop"); }
    while (status & MBOX_TX_FULL);
    *write_reg=LE32(data);
}
static ULONG get_mailbox_value(ULONG tag,ULONG id)
{
    ULONG *buf=(ULONG *)(((ULONG)req_storage+31UL)&~31UL);
    ULONG len=32;
    buf[0]=LE32(32); buf[1]=0; buf[2]=LE32(tag);
    buf[3]=LE32(8); buf[4]=0; buf[5]=LE32(id);
    buf[6]=0; buf[7]=0;
    CachePreDMA(buf,&len,0);
    mbox_send(8,(ULONG)buf); mbox_recv(8);
    CachePostDMA(buf,&len,0);
    return LE32(buf[6]);
}
static ULONG get_core_temperature(void) { return get_mailbox_value(TAG_GET_TEMP,0); }
static ULONG get_core_voltage(void) { return get_mailbox_value(TAG_GET_VOLT,1); }

struct MemoryInfo { ULONG total,free_bytes,used,free_tenths; };
static void read_memory(ULONG type,struct MemoryInfo *info)
{
    info->total=AvailMem(type | MEMF_TOTAL);
    info->free_bytes=AvailMem(type);
    if (info->free_bytes>info->total) info->free_bytes=info->total;
    info->used=info->total-info->free_bytes;
    info->free_tenths=info->total ?
        (ULONG)(((unsigned long long)info->free_bytes*1000ULL)/info->total) : 0;
}
static void format_percent(char *out,const char *name,ULONG tenths)
{
    sprintf(out,"%s %lu.%1lu%%",name,tenths/10,tenths%10);
}
static void format_free(char *out,const char *name,ULONG bytes)
{
    ULONG mb10=(ULONG)(((unsigned long long)bytes*10ULL)/(1024ULL*1024ULL));
    if (mb10>=100) sprintf(out,"%s %luMB",name,mb10/10);
    else sprintf(out,"%s %lu.%1luMB",name,mb10/10,mb10%10);
}
static void compose_temperature(ULONG temp,int range)
{
    char s[24];
    clear_next_screen();
    if (range) {
        screen_text(0,0,"H 85C ",6); screen_text(1,0,"L 30C ",6);
    } else {
        screen_text(0,0,"TEMP  ",6);
        sprintf(s,"%2lu.%1luC",temp/1000,(temp%1000)/100);
        screen_text(1,0,s,6);
    }
    screen_graph(temp_history);
}
static void compose_voltage(ULONG volt,int range)
{
    char s[24];
    clear_next_screen();
    if (range) {
        screen_text(0,0,"H1.50V",6); screen_text(1,0,"L1.10V",6);
    } else {
        screen_text(0,0,"VOLT  ",6);
        sprintf(s,"%1lu.%02luV",volt/1000000,(volt%1000000)/10000);
        screen_text(1,0,s,6);
    }
    screen_graph(volt_history);
}
static void compose_memory(const struct MemoryInfo *fast,const struct MemoryInfo *chip,int amounts)
{
    char s[32];
    clear_next_screen();
    if (amounts) {
        format_free(s,"FAST",fast->free_bytes); screen_text(0,0,s,10);
        format_free(s,"CHIP",chip->free_bytes); screen_text(1,0,s,10);
    } else {
        format_percent(s,"FAST",fast->free_tenths); screen_text(0,0,s,10);
        format_percent(s,"CHIP",chip->free_tenths); screen_text(1,0,s,10);
    }
    screen_bar(0,fast->free_tenths);
    screen_bar(1,chip->free_tenths);
}
int main(void)
{
    struct Library *I2CLib;
    struct MemoryInfo fast,chip;
    ULONG temp,volt;
    int phase=0,i;
    printf("PiStorm32Lite integrated LCD telemetry\n");
    InitMailBox();
    if (!MailBox) return 20;
    I2CLib=OpenLibrary("i2c.library",40);
    if (!I2CLib) { printf("ERROR: i2c.library unavailable\n"); return 20; }
    I2C_Base=(APTR)I2CLib;
    AllocI2C(0,"PiStormLCD"); InitI2C();
    if (!init_timer()) {
        printf("ERROR: timer.device unavailable\n");
        FreeI2C(); CloseLibrary(I2CLib); return 20;
    }
    lcd_init(); invalidate_screen();
    for (i=0;i<GRAPH_WIDTH;i++) temp_history[i]=volt_history[i]=0;
    while (1) {
        temp=get_core_temperature();
        volt=get_core_voltage();
        add_history(temp_history,temperature_to_level(temp));
        add_history(volt_history,voltage_to_level(volt));
        read_memory(MEMF_FAST,&fast);
        read_memory(MEMF_CHIP,&chip);
        if (phase<4) {
            set_glyph_mode(0);
            if (phase<2) compose_temperature(temp,phase==1);
            else compose_voltage(volt,phase==3);
        } else {
            set_glyph_mode(1);
            compose_memory(&fast,&chip,phase==5);
        }
        flush_screen();
        printf("Temp %lu.%03luC Volt %lu.%06luV FAST free %lu.%1lu%% CHIP free %lu.%1lu%%\n",
            temp/1000,temp%1000,volt/1000000,volt%1000000,
            fast.free_tenths/10,fast.free_tenths%10,
            chip.free_tenths/10,chip.free_tenths%10);
        Delay(DISPLAY_DELAY);
        phase=(phase+1)%6;
    }
    return 0;
}
