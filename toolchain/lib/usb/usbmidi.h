#define USB_FUNCTION_USBMIDI_IFACE_COUNT 2
#define USB_FUNCTION_USBMIDI_TX_EP_COUNT 1
#define USB_FUNCTION_USBMIDI_RX_EP_COUNT 1

#define USBMIDI_TX_EP 1
#define USBMIDI_TX_SIZE	8
#define USBMIDI_TX_BUFFER 64

#define USBMIDI_RX_EP 1
#define USBMIDI_RX_SIZE 8

struct usbmidi_ctx {
        struct usbd_function_ctx_header header;
        struct usbd_ep_pipe_state_t *tx_pipe;
        struct usbd_ep_pipe_state_t *rx_pipe;
        void *cbdata;
        struct fifo_ctx tx_fifo;
        
        int (*on_midi_recv)(void *data, uint8_t addr_and_type, uint8_t ctl, uint8_t data1, uint8_t data2);

        uint8_t outbuf[USBMIDI_TX_SIZE];
        uint8_t inbuf[USBMIDI_RX_SIZE];
        uint8_t tx_fifo_buf[USBMIDI_TX_BUFFER];
        uint16_t last_tx_length;
};

enum usb_audio_if_class {
        USB_IF_CLASS_AUDIO = 0x01,
};

enum usb_audio_dev_subclass {
        USB_IF_SUBCLASS_AUDIOCONTROL = 0x01,
        USB_IF_SUBCLASS_AUDIOSTREAMING = 0x02,
        USB_IF_SUBCLASS_MIDISTREAMING = 0x03,
};

enum usb_audio_dev_protocol {
        USB_IF_PROTOCOL_UNDEFINED = 0x00,
};

struct usb_audio_desc_function_common_t {
        uint8_t bLength;
        struct usb_desc_type_t bDescriptorType; /* USB_DESC_IFACE or USB_DESC_EP; class specific */
        enum {
                /* Audio control interface */
                USB_DESC_SUBTYPE_HEADER = 0x01,
                USB_DESC_SUBTYPE_INPUT_TERMINAL = 0x02,
                USB_DESC_SUBTYPE_OUTPUT_TERMINAL = 0x03,
                USB_DESC_SUBTYPE_SELECTOR_UNIT = 0x05,

                /* Audio streaming interface */
                USB_DESC_SUBTYPE_GENERAL = 0x01,
                USB_DESC_SUBTYPE_FORMAT_TYPE = 0x02,
        } bDescriptorSubtype;
} __packed;

struct usb_audio_ctrl_header_t {
        struct usb_audio_desc_function_common_t;
        struct usb_bcd_t bcdAudio; /* 1.0 */
        uint16_t wTotalLength;
} __packed;
CTASSERT_SIZE_BYTE(struct usb_audio_ctrl_header_t, 7);

enum jack_type_t {
        JACK_EMBEDDED = 1,
        JACK_EXTERNAL = 2,
};

struct usb_midi_in_jack_t {
        struct usb_audio_desc_function_common_t;
        enum jack_type_t bJackType;
        uint8_t bJackID;
        uint8_t iJack;
} __packed;
CTASSERT_SIZE_BYTE(struct usb_midi_in_jack_t, 6);

struct usb_midi_out_jack_t {
        struct usb_audio_desc_function_common_t;
        enum jack_type_t bJackType:8;
        uint8_t bJackID;
        uint8_t bNrInputPins;
        uint8_t baSourceID[1];
        uint8_t baSourcePin[1];
        uint8_t iJack;
} __packed;
CTASSERT_SIZE_BYTE(struct usb_midi_out_jack_t, 9);

struct usb_midi_streaming_ep_t {
        struct usb_audio_desc_function_common_t;
        uint8_t bNumEmbMIDIJack;
        uint8_t baAssocJackID[1];
} __packed;
CTASSERT_SIZE_BYTE(struct usb_midi_streaming_ep_t, 5);

struct usb_midi_function_desc {
        struct usb_desc_iface_t ctrl_iface;
        struct usb_audio_ctrl_header_t ctrl_header;
        uint8_t bInCollection;
        uint8_t baInterfaceNr[1];
        struct usb_desc_iface_t data_iface;
        struct usb_audio_ctrl_header_t midistr_header;
        struct usb_midi_in_jack_t in_jack1;
        struct usb_midi_in_jack_t in_jack2;
        struct usb_midi_out_jack_t out_jack1;
        struct usb_midi_out_jack_t out_jack2;
        struct usb_desc_ep_t rx_ep; /* IN ep 1 */
        uint8_t space[2];
        struct usb_midi_streaming_ep_t rx_ep_descr;
        struct usb_desc_ep_t tx_ep; /* OUT ep 1 */
        uint8_t space2[2];
        struct usb_midi_streaming_ep_t tx_ep_descr;
} __packed;
CTASSERT_SIZE_BYTE(struct usb_midi_function_desc, 92);

#define USB_FUNCTION_DESC_USBMIDI_DECL \
        struct usb_midi_function_desc

#define USB_FUNCTION_DESC_USBMIDI(state...)                             \
        {                                                               \
        .ctrl_iface = {                                                 \
                .bLength = sizeof(struct usb_desc_iface_t),             \
                .bDescriptorType = USB_DESC_IFACE,                      \
                .bInterfaceNumber = USB_FUNCTION_IFACE(0, state),       \
                .bAlternateSetting = 0,                                 \
                .bNumEndpoints = 0,                                     \
                .bInterfaceClass = USB_IF_CLASS_AUDIO,                  \
                .bInterfaceSubClass = USB_IF_SUBCLASS_AUDIOCONTROL,     \
                .bInterfaceProtocol = USB_IF_PROTOCOL_UNDEFINED,        \
                .iInterface = 0                                         \
        },                                                              \
                .ctrl_header = {                                        \
                .bLength = sizeof(struct usb_audio_ctrl_header_t) + 2,  \
                .bDescriptorType = {                                    \
                        .id = USB_DESC_IFACE,                           \
                        .type_type = USB_DESC_TYPE_CLASS                \
                },                                                      \
                .bDescriptorSubtype = USB_DESC_SUBTYPE_HEADER,          \
                .bcdAudio = { .maj = 1, .min = 0 },                     \
                .wTotalLength = 9,                                      \
        },                                                              \
                .bInCollection = 1,                                     \
                .baInterfaceNr = { 1 },                                 \
                                                                        \
                .data_iface = {                                         \
                .bLength = sizeof(struct usb_desc_iface_t),             \
                .bDescriptorType = USB_DESC_IFACE,                      \
                .bInterfaceNumber = USB_FUNCTION_IFACE(1, state),       \
                .bAlternateSetting = 0,                                 \
                .bNumEndpoints = 2,                                     \
                .bInterfaceClass = USB_IF_CLASS_AUDIO,                  \
                .bInterfaceSubClass = USB_IF_SUBCLASS_MIDISTREAMING,    \
                .bInterfaceProtocol = USB_IF_PROTOCOL_UNDEFINED,        \
                .iInterface = 0                                         \
        },                                                              \
                .midistr_header = {                                     \
                .bLength = sizeof(struct usb_audio_ctrl_header_t),      \
                .bDescriptorType = {                                    \
                        .id = USB_DESC_IFACE,                           \
                        .type_type = USB_DESC_TYPE_CLASS                \
                },                                                      \
                .bDescriptorSubtype = USB_DESC_SUBTYPE_HEADER,          \
                .bcdAudio = { .maj = 1, .min = 0 },                     \
                .wTotalLength = sizeof(struct usb_audio_ctrl_header_t)  \
                        + 2 * sizeof(struct usb_midi_in_jack_t)         \
                        + 2 * sizeof(struct usb_midi_out_jack_t),       \
        },                                                              \
                .in_jack1 = {                                           \
                .bLength = sizeof(struct usb_midi_in_jack_t),           \
                .bDescriptorType = {                                    \
                        .id = USB_DESC_IFACE,                           \
                        .type_type = USB_DESC_TYPE_CLASS                \
                },                                                      \
                .bDescriptorSubtype = USB_DESC_SUBTYPE_INPUT_TERMINAL,  \
                .bJackType = JACK_EXTERNAL,                             \
                .bJackID = 1,                                           \
                .iJack = 0,                                             \
        },                                                              \
                .in_jack2 = {                                           \
                .bLength = sizeof(struct usb_midi_in_jack_t),           \
                .bDescriptorType = {                                    \
                        .id = USB_DESC_IFACE,                           \
                        .type_type = USB_DESC_TYPE_CLASS                \
                },                                                      \
                .bDescriptorSubtype = USB_DESC_SUBTYPE_INPUT_TERMINAL,  \
                .bJackType = JACK_EMBEDDED,                             \
                .bJackID = 2,                                           \
                .iJack = 0,                                             \
        },                                                              \
                .out_jack1 = {                                          \
                .bLength = sizeof(struct usb_midi_out_jack_t),          \
                .bDescriptorType = {                                    \
                        .id = USB_DESC_IFACE,                           \
                        .type_type = USB_DESC_TYPE_CLASS                \
                },                                                      \
                .bDescriptorSubtype = USB_DESC_SUBTYPE_OUTPUT_TERMINAL, \
                .bJackType = JACK_EXTERNAL,                             \
                .bJackID = 3,                                           \
                .bNrInputPins = 1,                                      \
                .baSourceID = { 1 },                                    \
                .baSourcePin = { 1 },                                   \
                .iJack = 0,                                             \
        },                                                              \
                .out_jack2 = {                                          \
                .bLength = sizeof(struct usb_midi_out_jack_t),          \
                .bDescriptorType = {                                    \
                        .id = USB_DESC_IFACE,                           \
                        .type_type = USB_DESC_TYPE_CLASS                \
                },                                                      \
                .bDescriptorSubtype = USB_DESC_SUBTYPE_OUTPUT_TERMINAL, \
                .bJackType = JACK_EMBEDDED,                             \
                .bJackID = 4,                                           \
                .bNrInputPins = 1,                                      \
                .baSourceID = { 2 },                                    \
                .baSourcePin = { 1 },                                   \
                .iJack = 0,                                             \
        },                                                              \
                .rx_ep = {                                              \
                .bLength = sizeof(struct usb_desc_ep_t) + 2,            \
                .bDescriptorType = USB_DESC_EP,                         \
                .ep_num = USB_FUNCTION_RX_EP(USBMIDI_RX_EP, state),     \
                .in = 0,                                                \
                .type = USB_EP_BULK,                                    \
                .wMaxPacketSize = USBMIDI_RX_SIZE,                      \
                .bInterval = 0xff                                       \
        },                                                              \
                {0, 0}, \
                .rx_ep_descr = {                                        \
                .bLength = sizeof(struct usb_midi_streaming_ep_t),      \
                .bDescriptorType = {                                    \
                        .id = USB_DESC_EP,                              \
                        .type_type = USB_DESC_TYPE_CLASS                \
                },                                                      \
                .bDescriptorSubtype = USB_DESC_SUBTYPE_GENERAL,         \
                .bNumEmbMIDIJack = 1,                                   \
                .baAssocJackID = { 3 },                                 \
        },                                                              \
                .tx_ep = {                                              \
                .bLength = sizeof(struct usb_desc_ep_t) + 2,            \
                .bDescriptorType = USB_DESC_EP,                         \
                .ep_num = USB_FUNCTION_TX_EP(USBMIDI_TX_EP, state),     \
                .in = 1,                                                \
                .type = USB_EP_BULK,                                    \
                .wMaxPacketSize = USBMIDI_TX_SIZE,                      \
                .bInterval = 0xff                                       \
        },                                                              \
                {0, 0}, \
                .tx_ep_descr = {                                        \
                .bLength = sizeof(struct usb_midi_streaming_ep_t),      \
                .bDescriptorType = {                                    \
                        .id = USB_DESC_EP,                              \
                        .type_type = USB_DESC_TYPE_CLASS                \
                },                                                      \
                .bDescriptorSubtype = USB_DESC_SUBTYPE_GENERAL,         \
                .bNumEmbMIDIJack = 1,                                   \
                .baAssocJackID = { 1 },                                 \
        },                                                              \
}

struct usbmidi_ctx;

extern const struct usbd_function usbmidi_function;

/* For all messages *other* than SysEx */
int
usbmidi_tx(struct usbmidi_ctx *ctx, uint8_t addr_and_type, uint8_t ctl, uint8_t data1, uint8_t data2);

void
usbmidi_read_more(struct usbmidi_ctx *ctx);

void
usbmidi_init(struct usbmidi_ctx *ctx, int (*on_midi_recv)(void *data, uint8_t addr_and_type, uint8_t ctl, uint8_t data1, uint8_t data2), void *cbdata);

void
usbmidi_set_stdout(struct usbmidi_ctx *cdc);

inline int
usbmidi_bytes_from_ctl(uint8_t ctl)
{
        if (ctl < 0x80)
                return 0;
        if (ctl < 0xC0 || ((ctl & 0xF0) == 0xE0))
                return 3;
        if (ctl < 0xE0)
                return 2;
        if (ctl == 0xF0 || ctl == 0xF7)
                return -1;
        if (ctl < 0xF4)
                return ctl == 0xF2 ? 3 : 2;
        return 1;
}