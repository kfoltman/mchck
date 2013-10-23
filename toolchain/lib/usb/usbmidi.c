#include <mchck.h>
#include <usb/usb.h>
#include "usbmidi.h"

void
usbmidi_rx_done(void *buf, ssize_t len, void *data)
{
        struct usbmidi_ctx *ctx = data;
        const uint8_t *bufb = buf;
        int ready = 1;
        
        while(len >= 4) {
                ready = ctx->on_midi_recv(ctx->cbdata, bufb[0], bufb[1], bufb[2], bufb[3]);
                bufb += 4;
                len -= 4;
        }
        if (ready)
                usbmidi_read_more(ctx);
}

void
usbmidi_read_more(struct usbmidi_ctx *ctx)
{
        usb_rx(ctx->rx_pipe, ctx->inbuf, sizeof(ctx->inbuf), usbmidi_rx_done, ctx);
}

void
usbmidi_tx_done(void *buf, ssize_t len, void *data);

void
usbmidi_tx_from_fifo(struct usbmidi_ctx *ctx)
{
        crit_enter();
        uint16_t bytes = fifo_get_read_len(&ctx->tx_fifo);
        if (bytes >= 4) {
                bytes = bytes &~ 3;
                if (bytes > USBMIDI_TX_SIZE)
                        bytes = USBMIDI_TX_SIZE;
                fifo_read_block(&ctx->tx_fifo, ctx->outbuf, bytes);
                ctx->last_tx_length = bytes;
                usb_tx(ctx->tx_pipe, ctx->outbuf, bytes, USBMIDI_TX_SIZE, usbmidi_tx_done, ctx);
        }
        else
                ctx->last_tx_length = 0;
        crit_exit();
}

void
usbmidi_tx_done(void *buf, ssize_t len, void *data)
{
        struct usbmidi_ctx *ctx = data;

        if (len <= 0) {
                if (ctx->last_tx_length > 0) {
                        /* Retransmit last */
                        usb_tx(ctx->tx_pipe, ctx->outbuf, ctx->last_tx_length, USBMIDI_TX_SIZE, usbmidi_tx_done, ctx);
                }
        }
        else
                usbmidi_tx_from_fifo(ctx);
}

int usbmidi_tx(struct usbmidi_ctx *ctx,
               uint8_t addr_and_type,
               uint8_t ctl, uint8_t data1, uint8_t data2)
{
        if (fifo_get_write_space(&ctx->tx_fifo) < 4)
                return 0;

        uint8_t tx_buffer[4];
        tx_buffer[0] = addr_and_type;
        tx_buffer[1] = ctl;
        tx_buffer[2] = data1;
        tx_buffer[3] = data2;
        crit_enter();
        fifo_write_block(&ctx->tx_fifo, tx_buffer, 4);
        if (!ctx->last_tx_length)
                usbmidi_tx_from_fifo(ctx);
        crit_exit();
        return 1;
}

const struct usbd_function usbmidi_function = {
        .interface_count = USB_FUNCTION_USBMIDI_IFACE_COUNT,
};

void
usbmidi_init(struct usbmidi_ctx *ctx, 
             int (*on_midi_recv)(void *data, uint8_t addr_and_type, uint8_t ctl, uint8_t data1, uint8_t data2),
             void *cbdata)
{
        usb_attach_function(&usbmidi_function, &ctx->header);
        ctx->last_tx_length = 0;
	ctx->on_midi_recv = on_midi_recv;
        ctx->cbdata = cbdata;
        ctx->tx_pipe = usb_init_ep(&ctx->header, USBMIDI_TX_EP, USB_EP_TX, USBMIDI_TX_SIZE);
	ctx->rx_pipe = usb_init_ep(&ctx->header, USBMIDI_RX_EP, USB_EP_RX, USBMIDI_RX_SIZE);
        fifo_init(&ctx->tx_fifo, &ctx->tx_fifo_buf, USBMIDI_TX_BUFFER);
        usbmidi_read_more(ctx);
}

