// 1. compress start-of-song events before first note
// 2. find out why next song playback is not working / rewrite the code
// 3. implement previous song
// 4. change to sysex format (mostly done)
// 5. add status feedback as sysex (mostly done)
// 6. write a client (partially done)
// 7. add SMF export
// 8. add SMF import
#include <mchck.h>

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

enum {
        SYSEX_CMD_RESET = 1,
        SYSEX_CMD_PLAY = 2,
        SYSEX_CMD_STOP = 3,
        SYSEX_CMD_ERASE = 4,
        SYSEX_CMD_ERASE_RESPONSE = 0x44,
        SYSEX_CMD_STATUS = 5,
        SYSEX_CMD_STATUS_RESPONSE = 0x45,
        SYSEX_CMD_SKIP = 6,
        SYSEX_CMD_TRANSFER = 7,
        SYSEX_CMD_TRANSFER_RESPONSE = 0x47,
};

/****************************************************************************/

static struct usbmidi_ctx umctx;

static int enabled = 0;

static struct fifo_ctx uart_tx_fifo;
static uint8_t uart_tx_fifo_buffer[32];

struct uart_ctx uart0_ctx;

void
uart_tx_unblocked(void *cbdata, unsigned space)
{
        usbmidi_read_more(&umctx);
}

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

enum PlaybackMode
{
        PM_ERASE,
        PM_RECORD,
        PM_PLAY_TO_MIDI,
        PM_X, // 3
        PM_PLAY_TO_USB,
        PM_PLAY_TO_BOTH,
        PM_SKIP_FORWARD,
};

static volatile int has_flash = -1;
static volatile int flash_access_in_progress = 0;

static struct fifo_ctx flash_write_fifo;
static volatile uint8_t flash_write_fifo_buffer[256];

static volatile uint32_t flash_write_addr = 0;

static volatile uint32_t flash_read_addr = 0;
static uint8_t flash_byte = 255;
static uint8_t request_stop = 0;
static uint8_t first_event_to_record = 0;

static volatile enum PlaybackMode seq_mode = PM_RECORD;

volatile uint32_t time_next_note = -1, time_last_delay = 0;
volatile int note_received = 0;

static uint32_t erase_pointer = 0;

uint32_t last_write_time = 0;

static void
midiflash_start_playback(int to_usb, int delay, uint32_t start_addr);

static void
midiflash_start_recording(uint32_t addr)
{
        if (addr < 1048576)
        {
                first_event_to_record = 1;
                flash_write_addr = addr;
                seq_mode = PM_RECORD;
                last_write_time = systime;
        }
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
                usb_midi_send(0x4, 0xF0, 0x47, SYSEX_CMD_ERASE_RESPONSE);
                usb_midi_send(0x5, 0xF7, 0, 0);
                midiflash_start_recording(0);
                return;
        }
        seq_mode = PM_ERASE;
        onboard_led(0 != (erase_pointer & 65536));
        flash_access_in_progress = 1;
        erase_pointer += 4096;
        spiflash_erase_sector(erase_pointer - 4096, my_erase_cb, NULL);
}

void flash_detect_cb(void *cbdata, uint8_t status)
{
        has_flash = status;
}

static void
midiflash_init(void)
{
        spi_init();
        spiflash_pins_init();
        spiflash_is_present(flash_detect_cb, NULL);
        while(has_flash == -1)
                ;
        if (has_flash)
                fifo_init(&flash_write_fifo, (uint8_t *)flash_write_fifo_buffer, sizeof(flash_write_fifo_buffer));
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
        if (has_flash && seq_mode == PM_RECORD && !flash_access_in_progress)
        {
                if (flash_write_addr >= 1048576 - 256)
                {
                        onboard_led(1);
                        crit_exit();
                        return;
                }
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
        if ((seq_mode == PM_PLAY_TO_MIDI || seq_mode == PM_PLAY_TO_USB || seq_mode == PM_PLAY_TO_BOTH) && (request_stop || systime >= time_next_note) && note_received)
        {
                note_received = 0;
                midiflash_do_playback(NULL);
        }
        if (seq_mode == PM_RECORD)
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
                if (seq_mode == PM_SKIP_FORWARD)
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
                if (seq_mode == PM_SKIP_FORWARD)
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
                if (seq_mode == PM_SKIP_FORWARD)
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
                if (seq_mode == PM_PLAY_TO_MIDI || seq_mode == PM_PLAY_TO_BOTH)
                        uart_tx(&uart0_ctx, flash_byte);
                if (seq_mode == PM_PLAY_TO_USB || seq_mode == PM_PLAY_TO_BOTH)
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
        onboard_led(0);
        seekpos --;
        while(flash_write_fifo_buffer[seekpos & 255] == 255)
                seekpos--;
        flash_write_addr = seekpos + 2;
        flash_access_in_progress = 0;
}

static void
midiflash_findend_page_cb(void *cbdata)
{
        if (seekpos >= 1048576 - 256)
        {
                onboard_led(0);
                flash_write_addr = 1048576;
                flash_access_in_progress = 0;
                return;
        }
        onboard_led(seekpos & 32768 ? 1 : 0);
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

static int sysex_state = 0;
static uint8_t sysex_buffer[16];

/****************************************************************************/

static uint8_t usb_readout_buf[20]; /* 4 bytes padding */

static void readout_cb(void *cbdata)
{
        int i;
        usb_midi_send(0x4, 0xF0, 0x47, SYSEX_CMD_TRANSFER_RESPONSE);
        
        /* 16 bytes -> 128 bits -> 19 bytes in 7-bit encoding;
        that makes 6 3-byte packets + 1 2-byte packet (including termination) */
        
        uint32_t shiftreg = 0;
        for (i = 0; i < 7; i++)
        {
                int bitc = 21 * i;
                memcpy(&shiftreg, usb_readout_buf + (bitc >> 3), 4);
                shiftreg >>= (bitc & 7);
                if (i < 6)
                        usb_midi_send(0x4, (shiftreg & 127), (shiftreg >> 7) & 127, (shiftreg >> 14) & 127);
                else
                        usb_midi_send(0x6, (shiftreg & 127), 0xF7, 0);
        }
        seq_mode = PM_RECORD;
        flash_access_in_progress = 0;
}

/****************************************************************************/

static
void handle_sysex_end(void)
{
        switch(sysex_buffer[0])
        {
        case SYSEX_CMD_RESET:
                RFVBAT_REG7 |= 1;
                sys_reset();
                return;
        case SYSEX_CMD_PLAY:
                if (sysex_state == 2)
                {
                        int pm = sysex_buffer[1];
                        if (pm == PM_PLAY_TO_MIDI || pm == PM_PLAY_TO_USB || pm == PM_PLAY_TO_BOTH)
                                midiflash_start_playback(pm, 0, 0);
                }
                if (sysex_state == 5)
                {
                        int pm = sysex_buffer[1];
                        if (pm == PM_PLAY_TO_MIDI || pm == PM_PLAY_TO_USB || pm == PM_PLAY_TO_BOTH)
                                midiflash_start_playback(pm, 0, sysex_buffer[2] + 128 * sysex_buffer[3] + 128 * 128 * sysex_buffer[4]);
                }
                break;                
        case SYSEX_CMD_STOP:
                midiflash_stop();
                break;
        case SYSEX_CMD_ERASE:
                // XXXKF this is wrong, needs to queue the operation
                if (!flash_access_in_progress && sysex_state == 3 && sysex_buffer[1] == 0x55 && sysex_buffer[2] == 0x2A)
                        my_erase_cb(NULL);
                break;                
        case SYSEX_CMD_STATUS:
                usb_midi_send(0x4, 0xF0, 0x47, SYSEX_CMD_STATUS_RESPONSE);
                usb_midi_send(0x4, seq_mode, flash_access_in_progress, 0);
                usb_midi_send(0x4, flash_write_addr & 127, (flash_write_addr >> 7) & 127, (flash_write_addr >> 14) & 127);
                usb_midi_send(0x4, flash_read_addr & 127, (flash_read_addr >> 7) & 127, (flash_read_addr >> 14) & 127);
                usb_midi_send(0x5, 0xF7, 0, 0);
                break;
        case SYSEX_CMD_SKIP:
                midiflash_next();
                break;
        case SYSEX_CMD_TRANSFER:
                if (sysex_state == 4 && seq_mode == PM_RECORD && !flash_access_in_progress)
                {
                        flash_access_in_progress = 1;
                        flash_read_addr = sysex_buffer[1] + 128 * sysex_buffer[2] + 128 * 128 * sysex_buffer[3];
                        spiflash_read_page(usb_readout_buf, flash_read_addr, 16, readout_cb, NULL);
                }
                break;                
        }
        sysex_state = 0;
}

static
void handle_sysex_byte(uint8_t byte)
{
        if (sysex_state >= 16)
                return;
        sysex_buffer[sysex_state++] = byte;
}

int
new_data(void *data, uint8_t addr_and_type, uint8_t ctl, uint8_t data1, uint8_t data2)
{
        onboard_led(-1);
        if (ctl == 0xF0 && data1 == 0x47) {
                sysex_state = 1;
                sysex_buffer[0] = data2;
                onboard_led(-1);
                return fifo_flow_control(&uart_tx_fifo, 6, uart_tx_unblocked, 0);
        }
        if (sysex_state)
        {
                if (ctl == 0xF7)
                        handle_sysex_end();
                else
                {
                        handle_sysex_byte(ctl);
                        if (data1 == 0xF7)
                                handle_sysex_end();
                        else
                        {
                                handle_sysex_byte(data1);
                                if (data2 == 0xF7)
                                        handle_sysex_end();
                                else
                                        handle_sysex_byte(data2);
                        }
                }
                onboard_led(-1);
                return fifo_flow_control(&uart_tx_fifo, 6, uart_tx_unblocked, 0);
        }                
        
        if (addr_and_type >= 2 && addr_and_type <= 7) {
                uart_tx(&uart0_ctx, ctl);
                if (addr_and_type != 5)
                        uart_tx(&uart0_ctx, data1);
                if (addr_and_type == 3 || addr_and_type == 4 || addr_and_type == 7)
                        uart_tx(&uart0_ctx, data2);
        }
        else {
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
                        u"mchck recording MIDI adapter", /* product" */
                        (init_usbmidi,       /* init */
                         USBMIDI)           /* functions */
                );

static inline void usb_midi_send(uint8_t addr_type, uint8_t ctl, uint8_t data1, uint8_t data2)
{
        if (!enabled)
                return;
        usbmidi_tx(&umctx, addr_type, ctl, data1, data2);
}

static void record_event(int bytes, uint8_t cmd, uint8_t arg1, uint8_t arg2)
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

static void on_uart_midi_received(int bytes, uint8_t cmd, uint8_t arg1, uint8_t arg2)
{
        if (seq_mode == 1 && cmd != 0xFE)
                record_event(bytes, cmd, arg1, arg2);

        switch(bytes)
        {
        case 1:
                usb_midi_send(cmd >> 4, cmd, 0, 0);
                break;
        case 2:
                if (cmd < 0xF0)
                        usb_midi_send(cmd >> 4, cmd, arg1, 0);
                else
                        usb_midi_send(2, cmd, arg1, 0);
                break;
        case 3:
                if (cmd < 0xF0)
                        usb_midi_send(cmd >> 4, cmd, arg1, arg2);
                else
                        usb_midi_send(3, cmd, arg1, arg2);
                break;
        default:
                break;
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
        
        if (seq_mode == PM_PLAY_TO_MIDI || seq_mode == PM_PLAY_TO_USB || seq_mode == PM_PLAY_TO_BOTH)
                midiflash_start_playback(seq_mode, 0, 0);
        
        usb_init(&usbmidi_device);
        
        uart_rx_enable(&uart0_ctx, uart_rx_handler, NULL);
        sys_yield_for_frogs();
}
