#include "midiflash.h"

static volatile uint8_t flash_write_fifo_buffer[256];
static uint8_t flash_byte, flash_byte_ready;
static uint8_t flash_delay_value[2] = {0, 0};
static enum SequencerMode seq_mode, next_seq_mode;
static uint8_t first_event_to_record = 0;
static uint32_t requested_flash_read_addr = 0;
static volatile uint32_t flash_write_addr = 0;
static volatile uint32_t flash_read_addr = 0;
static volatile int has_flash = -1;
static volatile uint32_t time_next_note = -1, time_last_delay = 0, time_start_playback = 0;
static uint32_t systime;
struct fifo_ctx flash_write_fifo;
static struct spiflash_ctx flash_ctx, readout_flash_ctx;
static uint32_t last_write_time = 0;
static volatile int flash_access_in_progress = 0;
static struct midi_rx_context flash_playback_midi_context;
static uint8_t playback_speed = 16;

/****************************************************************************/

static void
seq_request_flash_byte();

static void
seq_switch_mode(void);

/****************************************************************************/

void
seq_request_mode(enum SequencerMode new_mode, int playback_pos)
{
        next_seq_mode = new_mode;
        if (playback_pos != -1)
                requested_flash_read_addr = playback_pos;
}

/****************************************************************************/

void
seq_send_status(void)
{
        usb_midi_send(0x4, 0xF0, 0x47, SYSEX_CMD_STATUS_RESPONSE);
        usb_midi_send(0x4, seq_mode, flash_access_in_progress, has_flash & 127);
        usb_midi_send(0x4, flash_write_addr & 127, (flash_write_addr >> 7) & 127, (flash_write_addr >> 14) & 127);
        usb_midi_send(0x4, flash_read_addr & 127, (flash_read_addr >> 7) & 127, (flash_read_addr >> 14) & 127);
        usb_midi_send(0x5, 0xF7, 0, 0);
}

/****************************************************************************/

static void
seq_play_byte(uint8_t byte)
{
        if (byte == 255)
        {
                seq_switch_mode();
                return;
        }
        if (seq_mode == PM_PLAY_TO_MIDI || seq_mode == PM_PLAY_TO_BOTH)
                midi_uart_send_byte(byte);
        if (seq_mode == PM_PLAY_TO_USB || seq_mode == PM_PLAY_TO_BOTH)
                midi_rx_context_send_byte(&flash_playback_midi_context, byte);
}

/* Called on successful retrieval of delay length from flash */
static void
seq_flash_read_delay_callback(void *cbdata)
{
        if (next_seq_mode)
        {
                seq_switch_mode();
                return;
        }
        time_last_delay += flash_delay_value[0] + 128 * flash_delay_value[1];
        time_next_note = time_last_delay;
        seq_request_flash_byte();
}

/* Called on successful retrieval of data byte from flash */
static void
seq_read_flash_byte_callback(void *cbdata)
{
        if (flash_byte == 0xFF) // EOF
        {
                crit_enter();
                if (next_seq_mode == PM_NONE)
                        next_seq_mode = PM_RECORD;
                seq_switch_mode();
                crit_exit();
        }
        else if (flash_byte == 0xFE) // delay
        {
                crit_enter();
                flash_read_addr += 2;
                if (seq_mode == PM_SKIP_FORWARD && flash_read_addr < flash_write_addr)
                        seq_request_flash_byte();
                else
                {
                        // next 2 bytes = delay
                        spiflash_read_page(&flash_ctx, flash_delay_value, flash_read_addr - 2, 2, seq_flash_read_delay_callback, NULL);
                }
                crit_exit();
        }
        else
        {
                if (seq_mode == PM_SKIP_FORWARD)
                {
                        if (flash_read_addr < flash_write_addr)
                                seq_request_flash_byte();
                        else
                                seq_switch_mode();
                }
                else
                        flash_byte_ready = 1; /* will be processed on timer */
        }
}

static void
seq_request_flash_byte()
{
        spiflash_read_page(&flash_ctx, &flash_byte, flash_read_addr++, 1, seq_read_flash_byte_callback, NULL);        
}

/******************************** Erase ***************************************/

static uint32_t erase_pointer = 0;

static void
my_erase_cb(void *data)
{
        if (erase_pointer >= 1048576)
        {
                erase_pointer = 0;
                onboard_led(0);
                flash_read_addr = 0;
                flash_write_addr = 0;
                usb_midi_send(0x4, 0xF0, 0x47, SYSEX_CMD_ERASE_RESPONSE);
                usb_midi_send(0x5, 0xF7, 0, 0);
                seq_switch_mode();
                return;
        }
        onboard_led(0 != (erase_pointer & 65536));
        flash_access_in_progress = 1;
        erase_pointer += 4096;
        spiflash_erase_sector(&flash_ctx, erase_pointer - 4096, my_erase_cb, NULL);
}

/******************************** Find end ***************************************/

static uint32_t seekpos = 0;
static uint8_t pagedata[2];

static void
midiflash_findend_cb(void *cbdata)
{
        seekpos --;
        while(flash_write_fifo_buffer[seekpos & 255] == 255)
                seekpos--;
        onboard_led(0);
        flash_write_addr = seekpos + 2;
        seq_switch_mode();
}

static void
midiflash_findend_page_cb(void *cbdata)
{
        if (seekpos >= 1048576 - 256)
        {
                onboard_led(0);
                flash_write_addr = 1048576;
                next_seq_mode = PM_FLASH_FULL;
                seq_switch_mode();
                return;
        }
        onboard_led(seekpos & 32768 ? 1 : 0);
        if (pagedata[0] == 255 && pagedata[1] == 255)
        {
                if (seekpos == 0)
                {
                        flash_write_addr = 0;
                        seq_switch_mode();
                        return;
                }
                else
                {
                        spiflash_read_page(&flash_ctx, (uint8_t *)&flash_write_fifo_buffer, seekpos - 256U, 256, midiflash_findend_cb, NULL);
                }
        }
        else
        {
                seekpos += 256;
                spiflash_read_page(&flash_ctx, pagedata, seekpos, 2, midiflash_findend_page_cb, NULL);        
        }
}

static void
midiflash_findend(void)
{
        next_seq_mode = PM_RECORD;
        onboard_led(1);
        flash_access_in_progress = 1;
        seekpos = 0;
        spiflash_read_page(&flash_ctx, pagedata, seekpos, 2, midiflash_findend_page_cb, NULL);
}

/******************************** Readout  ***************************************/

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
        seq_switch_mode();
}

/******************************** Writing to flash ***************************************/

static
void midiflash_handle_write_queue(void);

static void
midiflash_program_cb(void *cbdata)
{
        flash_access_in_progress = 0;
        midiflash_handle_write_queue();
}

void
midiflash_handle_write_queue(void)
{
        crit_enter();
        if (!flash_access_in_progress)
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
                        spiflash_program_page(&flash_ctx, flash_write_addr++, data, 1, midiflash_program_cb, NULL);
                        return;
                }
        }
        crit_exit();
}

/**************************************************************************/

void
seq_set_speed(uint8_t speed)
{
        crit_enter();
        if (seq_mode == PM_PLAY_TO_MIDI || seq_mode == PM_PLAY_TO_USB || seq_mode == PM_PLAY_TO_BOTH)
                time_start_playback = systime - ((uint64_t)systime - time_start_playback) * playback_speed / speed;
        playback_speed = speed;
        crit_exit();
}

/******************************** Mode switching ***************************************/

static void
seq_panic(void)
{
        for (int i = 0; i < 16; i++)
        {
                seq_play_byte(0xB0 + i);
                seq_play_byte(120);
                seq_play_byte(0);
                seq_play_byte(123);
                seq_play_byte(0);
        }
}

static void
seq_init_mode(enum SequencerMode prev_mode)
{
        if (seq_mode == PM_RECORD)
        {
                if (flash_write_addr < 1048576)
                {
                        first_event_to_record = 1;
                        last_write_time = systime;
                }
                return;
        }
        if (seq_mode == PM_ERASE)
        {
                next_seq_mode = PM_RECORD;
                fifo_clear(&flash_write_fifo);
                erase_pointer = 0;
                my_erase_cb(NULL);
                return;
        }
        if (seq_mode == PM_SKIP_FORWARD)
        {
                if (prev_mode != PM_ERASE && prev_mode != PM_SKIP_FORWARD)
                        next_seq_mode = prev_mode;
                else
                        next_seq_mode = PM_RECORD;
                requested_flash_read_addr = -1;
                if (flash_read_addr < flash_write_addr)
                        seq_request_flash_byte();
                else
                        seq_switch_mode();
                return;
        }
        if (seq_mode == PM_PLAY_TO_MIDI || seq_mode == PM_PLAY_TO_USB || seq_mode == PM_PLAY_TO_BOTH)
        {
                next_seq_mode = PM_NONE;
                flash_byte_ready = 0;
                
                time_start_playback = systime;
                time_last_delay = 0;
                time_next_note = time_last_delay;

                flash_access_in_progress = 1;
                onboard_led(1);
                if (requested_flash_read_addr != -1)
                        flash_read_addr = requested_flash_read_addr;
                seq_request_flash_byte();
        }
        if (seq_mode == PM_TRANSFER)
        {
                next_seq_mode = PM_RECORD;
                flash_access_in_progress = 1;
                flash_read_addr = requested_flash_read_addr;
                spiflash_read_page(&readout_flash_ctx, usb_readout_buf, flash_read_addr, 16, readout_cb, NULL);
                return;
        }
        if (seq_mode == PM_FIND_END)
        {
                next_seq_mode = PM_RECORD;
                flash_access_in_progress = 1;
                midiflash_findend();
                return;
        }
}

static void
seq_cleanup_mode(void)
{
        flash_access_in_progress = 0;
        if (seq_mode == PM_PLAY_TO_MIDI || seq_mode == PM_PLAY_TO_USB || seq_mode == PM_PLAY_TO_BOTH)
        {
                seq_panic();
                onboard_led(0);
        }
}

static void
seq_switch_mode(void)
{
        enum SequencerMode prev = seq_mode, next = next_seq_mode;
        next_seq_mode = PM_NONE;
        seq_cleanup_mode();
        //midiflash_end_playback();

        crit_enter();
        seq_mode = next;
        seq_init_mode(prev);
        crit_exit();
}

/**************************************************************************/

void PIT0_Handler(void)
{
        systime++;
        if (seq_mode == PM_PLAY_TO_MIDI || seq_mode == PM_PLAY_TO_USB || seq_mode == PM_PLAY_TO_BOTH)
        {
                if (next_seq_mode != PM_NONE)
                {
                        flash_byte_ready = 0;
                        seq_switch_mode();
                }
                else 
                {
                        int32_t reltime = (int32_t)(systime - time_start_playback);
                        if (reltime > 0)
                        {
                                reltime = ((uint64_t)reltime * playback_speed) >> 4;
                                if (reltime >= time_next_note && flash_byte_ready)
                                {
                                        flash_byte_ready = 0;
                                        seq_play_byte(flash_byte);
                                        seq_request_flash_byte();
                                }
                        }
                }
        }
        else if (seq_mode == PM_RECORD && has_flash)
        {
                midiflash_handle_write_queue();
        }
        if (seq_mode == PM_RECORD && next_seq_mode && !fifo_get_read_len(&flash_write_fifo))
                seq_switch_mode();
        PIT.timer[0].tflg.tif = 1;
}

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

void
seq_record_to_flash(struct midi_rx_context *ctx, int bytes, int is_sysex)
{
        if (!has_flash || seq_mode != PM_RECORD || ctx->buffer[0] == 0xFE || is_sysex)
                return;

        while (((int)(systime - last_write_time)) > 0)
        {
                uint32_t cur_systime = systime;
                uint32_t delay = cur_systime - last_write_time;
                if (delay > 16383 && !first_event_to_record)
                {
                        fifo_write_byte(&flash_write_fifo, 0xFF);
                        last_write_time = cur_systime;
                        break;
                }
                if (first_event_to_record && delay > 10)
                        delay = 10;
                
                fifo_write_byte(&flash_write_fifo, 0xFE);
                fifo_write_byte(&flash_write_fifo, delay & 127);
                fifo_write_byte(&flash_write_fifo, delay >> 7);
                last_write_time = cur_systime;
                // We don't care about times > 16s
                break;
        }
        uint8_t cmd = is_sysex ? 0 : ctx->buffer[0];
        if (cmd >= 0x90 && cmd < 0xA0)
                first_event_to_record = 0;
        for (int i = 0; i < bytes; i++)
                fifo_write_byte(&flash_write_fifo, ctx->buffer[i]);
}

/**************************************************************************/

void flash_detect_cb(void *cbdata, uint8_t mfg_id, uint8_t memtype, uint8_t capacity)
{
        has_flash = (mfg_id == SPIFLASH_MFGID_WINBOND)
                && (memtype == SPIFLASH_MEMTYPE_WINBOND_FLASH)
                && (capacity == SPIFLASH_WINBOND_SIZE_1MB);
        if (has_flash)
                next_seq_mode = PM_FIND_END;
        else
                next_seq_mode = PM_NO_FLASH;
        seq_switch_mode();
}

/**************************************************************************/

void
seq_init(void)
{
        pit_init();
        midi_rx_context_init(&flash_playback_midi_context, send_from_midi_context_to_usb, NULL);

        spi_init();
        spiflash_pins_init();
        fifo_init(&flash_write_fifo, (uint8_t *)flash_write_fifo_buffer, sizeof(flash_write_fifo_buffer));
        
        seq_mode = PM_INIT;
        spiflash_get_id(&flash_ctx, flash_detect_cb, NULL);
}
