// 1. compress start-of-song events before first note
// 2. find out why next song playback is not working / rewrite the code
// 3. implement previous song
// 4. change to sysex format
// 5. add status feedback as sysex
// 6. write a client
// 7. add SMF export
// 8. add SMF import
#include <mchck.h>
#include "spiflash.h"

#include <usb/usb.h>
#include <usb/usbmidi.h>

#define DEBUG

/****************************************************************************/

enum midi_rx_state
{
        MRS_WAIT,
        MRS_CMD_11,
        MRS_CMD_21,
        MRS_CMD_22,
        MRS_SYSEX0,
        MRS_SYSEX1,
        MRS_SYSEX2,
};

struct midi_rx
{
        enum midi_rx_state state:8;
        uint8_t buffer[3];
} mrx;

/****************************************************************************/

static struct usbmidi_ctx umctx;

static int enabled = 0;

static struct fifo_ctx uart_tx_fifo;
static uint8_t uart_tx_fifo_buffer[32];

struct uart_ctx uart0_ctx;

/****************************************************************************/

static inline void
usb_midi_send(uint8_t addr_type, uint8_t ctl, uint8_t data1, uint8_t data2);

static void
uart_rx_handler(void *cbdata, uint8_t value);

/****************************************************************************
 * Timer handling
 ****************************************************************************/

uint32_t systime;

static void
pit_init(void)
{
        SIM.scgc6.pit = 1;
        PIT.mcr.mdis = 0;
        PIT.timer[0].ldval = 50000;
        PIT.timer[0].tctrl.tie = 1;
        PIT.timer[0].tctrl.ten = 1;
        PIT.mcr.frz = 1;
        int_enable(IRQ_PIT0);
}
 
/****************************************************************************
 * Flash handling
 ****************************************************************************/

static int has_flash = 0;
static volatile int flash_access_in_progress = 0;

static struct fifo_ctx flash_write_fifo;
static volatile uint8_t flash_write_fifo_buffer[256];

static volatile uint32_t flash_write_addr = 0;

static volatile uint32_t flash_read_addr = 0;
static uint8_t flash_byte = 255;
static uint8_t request_stop = 0;
static uint8_t first_event_to_record = 0;

static volatile uint8_t seq_mode = 2;

volatile uint32_t time_next_note = -1, time_last_delay = 0;
volatile int note_received = 0;

static uint32_t erase_pointer = 0;

uint32_t last_write_time = 0;

static void
midiflash_start_playback(int to_usb, int delay, uint32_t start_addr);

static void
midiflash_start_recording(uint32_t addr)
{
        first_event_to_record = 1;
        flash_write_addr = addr;
        seq_mode = 1;
        last_write_time = systime;
        fifo_clear(&flash_write_fifo);
}

static void
my_erase_cb(void *data)
{
        if (erase_pointer >= 1048576)
        {
                erase_pointer = 0;
                onboard_led(0);
                flash_access_in_progress = 0;
                flash_read_addr = 0;
                midiflash_start_recording(0);
                return;
        }
        seq_mode = 0;
        onboard_led(0 != (erase_pointer & 65536));
        flash_access_in_progress = 1;
        erase_pointer += 4096;
        spiflash_erase_sector(erase_pointer - 4096, my_erase_cb, NULL);
}

static void
midiflash_init(void)
{
        spi_init();
        spiflash_pins_init();
        has_flash = spiflash_is_present();
        if (has_flash)
        {
                fifo_init(&flash_write_fifo, (uint8_t *)flash_write_fifo_buffer, sizeof(flash_write_fifo_buffer));
                //spiflash_program_page(0, (const uint8_t *)"Test", 4, dummy_cb, NULL);
                //spiflash_read_page(&rdvalue[0], 0, 2, dummy_cb, NULL);
        }
}

static void
midiflash_do_program(void);

static void
midiflash_do_playback(void *cbdata);

static void
midiflash_program_cb(void *cbdata)
{
        flash_access_in_progress = 0;
        midiflash_do_program();
}

static void
midiflash_do_program(void)
{
        crit_enter();
        if (has_flash && seq_mode == 1 && !flash_access_in_progress)
        {
                const uint8_t *data = fifo_read_byte_inplace(&flash_write_fifo);
                if (data)
                {
                        flash_access_in_progress = 1;
                        crit_exit();
                        spiflash_program_page(flash_write_addr++, data, 1, midiflash_program_cb, NULL);
                        return;
                }
        }
        crit_exit();
}

void PIT0_Handler(void)
{
        systime++;
        if ((seq_mode == 2 || seq_mode == 4 || seq_mode == 5) && (request_stop || systime >= time_next_note) && note_received)
        {
                note_received = 0;
                midiflash_do_playback(NULL);
        }
        if (seq_mode == 1)
                midiflash_do_program();
        PIT.timer[0].tflg.tif = 1;
}

static uint8_t delay_value[2] = {0, 0};

static void
midiflash_read_callback(void *cbdata);

static void
midiflash_end_playback(void)
{
        flash_access_in_progress = 0;
        onboard_led(0);
        if (flash_write_addr < 1048576 - 256)
                midiflash_start_recording(flash_write_addr);
}

static void
flash_read_delay_callback(void *cbdata)
{
        if (request_stop)
        {
                int val = request_stop;
                request_stop = 0;
                midiflash_end_playback();
                for (int i = 0; i < 16; i++)
                {
                        uart_tx(&uart0_ctx, 0xB0 + i);
                        uart_tx(&uart0_ctx, 120);
                        uart_tx(&uart0_ctx, 0);
                        uart_tx(&uart0_ctx, 123);
                        uart_tx(&uart0_ctx, 0);
                }
                if (val > 1)
                        midiflash_start_playback(val, 0, flash_read_addr);
                
                return;
        }
        time_last_delay += delay_value[0] + 128 * delay_value[1];
        time_next_note = time_last_delay;
        spiflash_read_page(&flash_byte, flash_read_addr++, 1, midiflash_read_callback, NULL);
}

static void
midiflash_read_callback(void *cbdata)
{
        if (flash_byte == 255) // EOF
        {
                if (seq_mode == 6)
                {
                        flash_access_in_progress = 0;
                        midiflash_start_playback(2, 0, flash_read_addr);
                }
                else
                        midiflash_end_playback();
        }
        else if (flash_byte == 0xFE) // delay
        {
                flash_read_addr += 2;
                if (seq_mode == 6)
                {
                        spiflash_read_page(&flash_byte, flash_read_addr++, 1, midiflash_read_callback, NULL);
                }
                else
                {
                        // next 2 bytes = delay
                        spiflash_read_page(delay_value, flash_read_addr - 2, 2, flash_read_delay_callback, NULL);
                }
        }
        else
        {
                if (seq_mode == 6)
                        spiflash_read_page(&flash_byte, flash_read_addr++, 1, midiflash_read_callback, NULL);
                else
                        note_received = 1;
        }
}

static void
midiflash_do_playback(void *cbdata)
{
        if (flash_byte != 255)
        {
                note_received = 0;
                if (seq_mode == 2 || seq_mode == 5)
                        uart_tx(&uart0_ctx, flash_byte);
                if (seq_mode == 4 || seq_mode == 5)
                        uart_rx_handler(NULL, flash_byte);

                spiflash_read_page(&flash_byte, flash_read_addr++, 1, midiflash_read_callback, NULL);
        }
        else
                midiflash_end_playback();
}

static void
midiflash_start_playback(int mode, int delay, uint32_t start_addr)
{
        if (flash_access_in_progress)
                return;
        note_received = 0;
        
        time_last_delay = systime + 100 * delay;
        time_next_note = time_last_delay;
        flash_read_addr = start_addr;
        seq_mode = mode;
        flash_access_in_progress = 1;
        onboard_led(1);
        spiflash_read_page(&flash_byte, flash_read_addr++, 1, midiflash_read_callback, NULL);
}

static void
midiflash_stop(void)
{
        request_stop = 1;
}

static void
midiflash_next(void)
{
        request_stop = 6;
}

static uint32_t seekpos = 0;
static uint8_t pagedata[2];

static void
midiflash_findend_cb(void *cbdata)
{
        seekpos --;
        while(flash_write_fifo_buffer[seekpos & 255] == 255)
                seekpos--;
        flash_write_addr = seekpos + 2;
        flash_access_in_progress = 0;
}

static void
midiflash_findend_page_cb(void *cbdata)
{
        if (pagedata[0] == 255 && pagedata[1] == 255)
        {
                if (seekpos == 0)
                {
                        flash_write_addr = 0;
                        flash_access_in_progress = 0;
                }
                else
                {
                        spiflash_read_page((uint8_t *)&flash_write_fifo_buffer, seekpos - 256U, 256, midiflash_findend_cb, NULL);
                }
        }
        else
        {
                seekpos += 256;
                spiflash_read_page(pagedata, seekpos, 2, midiflash_findend_page_cb, NULL);        
        }
}

static void
midiflash_findend(void)
{
        onboard_led(1);
        flash_access_in_progress = 1;
        seekpos = 0;
        spiflash_read_page(pagedata, seekpos, 2, midiflash_findend_page_cb, NULL);
        while(flash_access_in_progress)
                ;
        if (flash_write_addr < 1048576 - 256)
        {
                flash_read_addr = flash_write_addr;
                midiflash_start_recording(flash_write_addr);
        }
        onboard_led(0);
}

/****************************************************************************/

void
uart_tx_unblocked(void *cbdata, unsigned space)
{
        usbmidi_read_more(&umctx);
}

int
new_data(void *data, uint8_t addr_and_type, uint8_t ctl, uint8_t data1, uint8_t data2)
{
        onboard_led(-1);
        if (addr_and_type >= 2 && addr_and_type <= 7) {
                uart_tx(&uart0_ctx, ctl);
                if (addr_and_type != 5)
                        uart_tx(&uart0_ctx, data1);
                if (addr_and_type == 3 || addr_and_type == 4 || addr_and_type == 7)
                        uart_tx(&uart0_ctx, data2);
        }
        else {
#ifdef DEBUG
                if (ctl == 0xF9) {
                        RFVBAT_REG7 |= 1;
                        sys_reset();
                }
                if (ctl == 0xF8) {
                        if (has_flash)
                        {
                                if (erase_pointer)
                                        usb_midi_send(0xB, 0xB0, (erase_pointer >> 12) & 127, erase_pointer >> (12 + 7));
                                else if (seq_mode == 1)
                                        usb_midi_send(0xA, 0xA0, flash_write_addr & 127, flash_write_addr >> 7);
                                else
                                        usb_midi_send(0x9, 0x90, flash_read_addr & 127, flash_read_addr >> 7);
                        }
                        else
                                usb_midi_send(0x8, 0x80, 0, 0);
                }
                if (ctl == 0x90) {  /* play first */
                        midiflash_start_playback(data1, data2, 0);
                }
                if (ctl == 0x91 && !flash_access_in_progress) {  /* erase flash */
                        my_erase_cb(NULL);
                }
                if (ctl == 0x92) { /* stop */
                        midiflash_stop();
                }
                if (ctl == 0x93) { /* play next */
                        //midiflash_start_playback(6, data2, flash_read_addr);
                        midiflash_next();
                }
#endif
                if (ctl >= 0x80)
                {
                        int bytes = usbmidi_bytes_from_ctl(ctl);
                        if (bytes >= 1)
                        {
                                uart_tx(&uart0_ctx, ctl);
                                if (bytes >= 2)
                                        uart_tx(&uart0_ctx, data1);
                                if (bytes >= 3)
                                        uart_tx(&uart0_ctx, data2);
                        }
                }
        }
        onboard_led(-1);
        return fifo_flow_control(&uart_tx_fifo, 6, uart_tx_unblocked, 0);
}

static void
init_usbmidi(int config)
{
        usbmidi_init(&umctx, new_data, NULL);
        enabled = 1;
}

const struct usbd_device usbmidi_device = 
        USB_INIT_DEVICE(0x2323,              /* vid */
                        5,                   /* pid */
                        u"mchck.org",        /* vendor */
                        u"USB MIDI adapter", /* product" */
                        (init_usbmidi,       /* init */
                         USBMIDI)           /* functions */
                );

static inline void usb_midi_send(uint8_t addr_type, uint8_t ctl, uint8_t data1, uint8_t data2)
{
        if (!enabled)
                return;
        usbmidi_tx(&umctx, addr_type, ctl, data1, data2);
}

static void on_uart_midi_received(int bytes, uint8_t cmd, uint8_t arg1, uint8_t arg2)
{
        if (seq_mode == 1 && cmd != 0xFE)
        {
                while (((int)(systime - last_write_time)) > 0)
                {
                        uint32_t cur_systime = systime;
                        uint32_t delay = cur_systime - last_write_time;
                        if (delay > 16383)
                                delay = 16383;
                        if (first_event_to_record && delay > 10)
                                delay = 10;
                        
                        fifo_write_byte(&flash_write_fifo, 0xFE);
                        fifo_write_byte(&flash_write_fifo, delay & 127);
                        fifo_write_byte(&flash_write_fifo, delay >> 7);
                        last_write_time = cur_systime;
                        // We don't care about times > 16s
                        break;
                }
                if (cmd >= 0x90 && cmd < 0xA0)
                        first_event_to_record = 0;
                if (bytes >= 1)
                        fifo_write_byte(&flash_write_fifo, cmd);
                if (bytes >= 2)
                        fifo_write_byte(&flash_write_fifo, arg1);
                if (bytes >= 3)
                        fifo_write_byte(&flash_write_fifo, arg2);
        }
        if (bytes == 1)
        {
                usb_midi_send(cmd >> 4, cmd, 0, 0);
        }
        if (bytes == 2)
        {
                if (cmd < 0xF0)
                        usb_midi_send(cmd >> 4, cmd, arg1, 0);
                else
                        usb_midi_send(2, cmd, arg1, 0);
        }
        if (bytes == 3)
        {
                if (cmd < 0xF0)
                        usb_midi_send(cmd >> 4, cmd, arg1, arg2);
                else
                        usb_midi_send(3, cmd, arg1, arg2);
        }
}

static void uart_rx_handler(void *cbdata, uint8_t value)
{
        onboard_led(-1);
        uint8_t *buffer = mrx.buffer;
        if (mrx.state == MRS_SYSEX0 || mrx.state == MRS_SYSEX1 || mrx.state == MRS_SYSEX2)
        {
                buffer[mrx.state - MRS_SYSEX0] = value;
                if (value == 0xF7) {
                        usb_midi_send(5 + mrx.state - MRS_SYSEX0, buffer[0], buffer[1], buffer[2]);
                        mrx.state = MRS_WAIT;
                }
                else {
                        mrx.state = mrx.state < MRS_SYSEX2 ? mrx.state + 1 : MRS_SYSEX0;
                        if (mrx.state == MRS_SYSEX0) {
                                usb_midi_send(4, buffer[0], buffer[1], buffer[2]);
                                memset(buffer, 0, 3);
                        }
                }
                onboard_led(-1);
                return;
        }
        if (value <= 0x80)
        {
                switch(mrx.state)
                {
                case MRS_WAIT:
                        break;
                case MRS_CMD_11:
                        on_uart_midi_received(2, buffer[0], value, 0);
                        break;
                case MRS_CMD_21:
                        buffer[1] = value;
                        mrx.state = MRS_CMD_22;
                        break;
                case MRS_CMD_22:
                        on_uart_midi_received(3, buffer[0], buffer[1], value);
                        break;
                case MRS_SYSEX0:
                case MRS_SYSEX1:
                case MRS_SYSEX2:
                        break;
                }
                onboard_led(-1);
                return;
        }
        buffer[0] = value;
        int bytes = usbmidi_bytes_from_ctl(value);
        if (bytes == 3)
        {
                mrx.state = MRS_CMD_21;
        }
        else
        if (bytes == 2)
        {
                mrx.state = MRS_CMD_11;
        }
        else
        if (bytes == 1)
        {
                on_uart_midi_received(1, value, 0, 0);
        }
        else
        if (bytes == -1 && value == 0xF0)
        {
                mrx.state = MRS_SYSEX1;
                buffer[0] = 0xF0;
                // usb_midi_send(value >> 4, value, 0, 0);
        }
        onboard_led(-1);
}

void
main(void)
{
#ifdef DEBUG
        if (RFVBAT_REG7 & 1)
        {
            RFVBAT_REG7 &= ~1;
            sys_jump_to_programmer();
        }
#endif
        pit_init();
        midiflash_init();
        midiflash_findend();
        fifo_init(&uart_tx_fifo, uart_tx_fifo_buffer, sizeof(uart_tx_fifo_buffer));

        uart_init(&uart0_ctx, UART_CH0);
        uart_set_baud_rate(&uart0_ctx, 31250);
        uart_tx_enable(&uart0_ctx, &uart_tx_fifo);
        uart_tx_set_invert(&uart0_ctx, 1);
        
        //if (seq_mode == 2)
        //        midiflash_start_playback(2, 0, 0);
        
        usb_init(&usbmidi_device);
        
        uart_rx_enable(&uart0_ctx, uart_rx_handler, NULL);
        sys_yield_for_frogs();
}
