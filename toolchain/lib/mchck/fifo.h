typedef void (*fifo_write_space_cb)(void *cbdata, unsigned space);

struct fifo_ctx
{
        uint8_t *buffer;
        volatile uint16_t read_ptr;
        uint16_t len;
        volatile uint16_t write_ptr;
        
        fifo_write_space_cb write_space_cb;
        void *write_space_cbdata;
        uint16_t write_space_threshold;
};

void
fifo_init(struct fifo_ctx *fifo, void *buffer, uint16_t len);

void
fifo_clear(struct fifo_ctx *fifo);

/**
 * Check the amount of data that can be read from the FIFO
 * @retval The number of bytes available for reading.
 */
static inline uint16_t
fifo_get_read_len(const struct fifo_ctx *fifo)
{
        int bytes = fifo->write_ptr - fifo->read_ptr;
        return (bytes >= 0) ? bytes : bytes + fifo->len;
        
}

/**
 * Check the amount of data that can be written into the FIFO
 * @retval The number of bytes available for writing.
 */
static inline uint16_t
fifo_get_write_space(const struct fifo_ctx *fifo)
{
        int bytes = fifo->read_ptr - fifo->write_ptr - 1;
        return (bytes >= 0) ? bytes : bytes + fifo->len;
}

static inline void
fifo_call_write_space_cb(struct fifo_ctx *fifo, unsigned space)
{
        fifo->write_space_threshold = 0;
        fifo->write_space_cb(fifo->write_space_cbdata, space);
}

/**
 * @note amount must be < len
 */
static inline void
fifo_read_advance(struct fifo_ctx *fifo, uint16_t amount)
{
        uint16_t new_read_ptr = fifo->read_ptr + amount;
        if (new_read_ptr >= fifo->len)
                new_read_ptr -= fifo->len;
        fifo->read_ptr = new_read_ptr;

        if (fifo->write_space_threshold)
        {
                unsigned space = fifo_get_write_space(fifo);
                if (space >= fifo->write_space_threshold)
                        fifo_call_write_space_cb(fifo, space);
        }
}

/**
 * @note amount must be < len
 */
static inline void
fifo_write_advance(struct fifo_ctx *fifo, uint16_t amount)
{
        uint16_t new_write_ptr = fifo->write_ptr + amount;
        if (new_write_ptr >= fifo->len)
                new_write_ptr -= fifo->len;
        fifo->write_ptr = new_write_ptr;
}

/**
 * Read a single byte from the FIFO, if possible.
 * @retval The single value (if available), or -1 if no data
 */
static inline int
fifo_read_byte(struct fifo_ctx *fifo)
{
        if (fifo->read_ptr == fifo->write_ptr)
                return -1;
        uint8_t result = fifo->buffer[fifo->read_ptr];
        fifo_read_advance(fifo, 1);
        return result;
}

/**
 * Read a single byte from the FIFO, if possible.
 * @retval The single value (if available), or -1 if no data
 */
static inline const uint8_t *
fifo_read_byte_inplace(struct fifo_ctx *fifo)
{
        if (fifo->read_ptr == fifo->write_ptr)
                return NULL;
        const uint8_t *result = &fifo->buffer[fifo->read_ptr];
        fifo_read_advance(fifo, 1);
        return result;
}

/**
 * Write a single byte into the FIFO, if possible.
 * @retval The written value (if available), or -1 if no space
 */
static inline int
fifo_write_byte(struct fifo_ctx *fifo, uint8_t val)
{
        if (fifo->read_ptr == fifo->write_ptr + 1 || 
            fifo->read_ptr == fifo->write_ptr + 1 - fifo->len)
                return -1;
        
        fifo->buffer[fifo->write_ptr] = val;
        fifo_write_advance(fifo, 1);
        return val;
}

/**
 * Read a block of data from the FIFO.
 * @param data target buffer
 * @param len maximum amount of data to copy
 * @retval The amount of data actually copied.
 */
int
fifo_read_block(struct fifo_ctx *fifo, void *data, uint16_t len);

/**
 * Write a block of data into the FIFO. 
 * @param data target buffer
 * @param len maximum amount of data to copy
 * @retval The amount of data actually copied.
 */
int
fifo_write_block(struct fifo_ctx *fifo, const void *data, uint16_t len);

/**
 * Check if it's possible to write N bytes to the FIFO, and if not, set the
 * callback.
 */
int
fifo_flow_control(struct fifo_ctx *fifo, unsigned space, fifo_write_space_cb cb,
                  void *cbdata);
