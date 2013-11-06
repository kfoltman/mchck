#include <mchck.h>

void
fifo_init(struct fifo_ctx *fifo, void *buffer, uint16_t len)
{
        fifo->buffer = buffer;
        fifo->read_ptr = 0;
        fifo->len = len;
        fifo->write_ptr = 0;
        fifo->write_space_cb = NULL;
        fifo->write_space_threshold = 0;
}

void
fifo_clear(struct fifo_ctx *fifo)
{
        fifo->read_ptr = 0;
        fifo->write_ptr = 0;
}

int
fifo_read_block(struct fifo_ctx *fifo, void *data, uint16_t len)
{
        int before_end;
        int bytes = fifo_get_read_len(fifo);
        if (!bytes)
                return 0;
        if (bytes < len)
                len = bytes;
        bytes = len;
        /* The number of bytes before wraparound */
        before_end = fifo->len - fifo->read_ptr;
        if (bytes >= before_end)
        {
                memcpy(data, fifo->buffer + fifo->read_ptr, before_end);
                data = ((uint8_t *)data) + before_end;
                bytes -= before_end;
                fifo->read_ptr = 0;
        }
        memcpy(data, fifo->buffer + fifo->read_ptr, bytes);
        fifo_read_advance(fifo, bytes);
        return len;
}

int
fifo_write_block(struct fifo_ctx *fifo, const void *data, uint16_t len)
{
        int before_end;
        int bytes = fifo_get_write_space(fifo);
        if (!bytes)
                return 0;
        if (bytes < len)
                len = bytes;
        bytes = len;
        /* The number of bytes before wraparound */
        before_end = fifo->len - fifo->write_ptr;
        if (bytes >= before_end)
        {
                memcpy(fifo->buffer + fifo->write_ptr, data, before_end);
                data = ((uint8_t *)data) + before_end;
                bytes -= before_end;
                fifo->write_ptr = 0;
        }
        memcpy(fifo->buffer + fifo->write_ptr, data, bytes);
        fifo_write_advance(fifo, bytes);
        return len;
}

int
fifo_flow_control(struct fifo_ctx *fifo, unsigned space, fifo_write_space_cb cb,
                  void *cbdata)
{
        unsigned free_space = fifo_get_write_space(fifo);
        if (free_space >= space)
                return 1;
        crit_enter();
        fifo->write_space_threshold = space;
        fifo->write_space_cb = cb;
        fifo->write_space_cbdata = cbdata;
        crit_exit();
        return 0;
}
