#include "usb_hori.h"

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_usb.h>
#include <string.h>

#include "usb.h"

#define HORI_VID 0x0F0D
#define HORI_PID 0x0092

#define EP_IN       0x81
#define EP_OUT      0x02
#define EP_SIZE     64
#define EP_INTERVAL 4

#define HORI_EP0_SIZE 8

#define STR_MANUF   1
#define STR_PRODUCT 2

#define HID_DTYPE_HID    0x21
#define HID_DTYPE_REPORT 0x22
#define HID_REQ_GET_REPORT   0x01
#define HID_REQ_GET_IDLE     0x02
#define HID_REQ_GET_PROTOCOL 0x03
#define HID_REQ_SET_REPORT   0x09
#define HID_REQ_SET_IDLE     0x0A
#define HID_REQ_SET_PROTOCOL 0x0B
#define USB_CLASS_HID_CODE   0x03

/* HORI Pokken report descriptor */
static const uint8_t hori_report_desc[] = {
    0x05, 0x01,
    0x09, 0x05,
    0xA1, 0x01,
    0x15, 0x00,
    0x25, 0x01,
    0x35, 0x00,
    0x45, 0x01,
    0x75, 0x01,
    0x95, 0x10,
    0x05, 0x09,
    0x19, 0x01,
    0x29, 0x10,
    0x81, 0x02,
    0x05, 0x01,
    0x25, 0x07,
    0x46, 0x3B, 0x01,
    0x75, 0x04,
    0x95, 0x01,
    0x65, 0x14,
    0x09, 0x39,
    0x81, 0x42,
    0x65, 0x00,
    0x95, 0x01,
    0x81, 0x01,
    0x26, 0xFF, 0x00,
    0x46, 0xFF, 0x00,
    0x09, 0x30,
    0x09, 0x31,
    0x09, 0x32,
    0x09, 0x35,
    0x75, 0x08,
    0x95, 0x04,
    0x81, 0x02,
    0x06, 0x00, 0xFF,
    0x09, 0x20,
    0x95, 0x01,
    0x81, 0x02,
    0x0A, 0x21, 0x26,
    0x95, 0x08,
    0x91, 0x02,
    0xC0,
};

struct HoriHidDescriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdHID;
    uint8_t bCountryCode;
    uint8_t bNumDescriptors;
    uint8_t bDescriptorType0;
    uint16_t wDescriptorLength0;
} FURI_PACKED;

struct HoriConfigDescriptor {
    struct usb_config_descriptor config;
    struct usb_interface_descriptor intf;
    struct HoriHidDescriptor hid;
    struct usb_endpoint_descriptor ep_in;
    struct usb_endpoint_descriptor ep_out;
} FURI_PACKED;

static const struct usb_device_descriptor hori_device_desc = {
    .bLength = sizeof(struct usb_device_descriptor),
    .bDescriptorType = USB_DTYPE_DEVICE,
    .bcdUSB = VERSION_BCD(2, 0, 0),
    .bDeviceClass = USB_CLASS_PER_INTERFACE,
    .bDeviceSubClass = USB_SUBCLASS_NONE,
    .bDeviceProtocol = USB_PROTO_NONE,
    .bMaxPacketSize0 = HORI_EP0_SIZE,
    .idVendor = HORI_VID,
    .idProduct = HORI_PID,
    .bcdDevice = VERSION_BCD(1, 0, 0),
    .iManufacturer = STR_MANUF,
    .iProduct = STR_PRODUCT,
    .iSerialNumber = NO_DESCRIPTOR,
    .bNumConfigurations = 1,
};

static const struct HoriConfigDescriptor hori_cfg_desc = {
    .config =
        {
            .bLength = sizeof(struct usb_config_descriptor),
            .bDescriptorType = USB_DTYPE_CONFIGURATION,
            .wTotalLength = sizeof(struct HoriConfigDescriptor),
            .bNumInterfaces = 1,
            .bConfigurationValue = 1,
            .iConfiguration = NO_DESCRIPTOR,
            .bmAttributes = USB_CFG_ATTR_RESERVED,
            .bMaxPower = USB_CFG_POWER_MA(500),
        },
    .intf =
        {
            .bLength = sizeof(struct usb_interface_descriptor),
            .bDescriptorType = USB_DTYPE_INTERFACE,
            .bInterfaceNumber = 0,
            .bAlternateSetting = 0,
            .bNumEndpoints = 2,
            .bInterfaceClass = USB_CLASS_HID_CODE,
            .bInterfaceSubClass = 0,
            .bInterfaceProtocol = 0,
            .iInterface = NO_DESCRIPTOR,
        },
    .hid =
        {
            .bLength = sizeof(struct HoriHidDescriptor),
            .bDescriptorType = HID_DTYPE_HID,
            .bcdHID = VERSION_BCD(1, 1, 1),
            .bCountryCode = 0,
            .bNumDescriptors = 1,
            .bDescriptorType0 = HID_DTYPE_REPORT,
            .wDescriptorLength0 = sizeof(hori_report_desc),
        },
    .ep_in =
        {
            .bLength = sizeof(struct usb_endpoint_descriptor),
            .bDescriptorType = USB_DTYPE_ENDPOINT,
            .bEndpointAddress = EP_IN,
            .bmAttributes = USB_EPTYPE_INTERRUPT,
            .wMaxPacketSize = EP_SIZE,
            .bInterval = EP_INTERVAL,
        },
    .ep_out =
        {
            .bLength = sizeof(struct usb_endpoint_descriptor),
            .bDescriptorType = USB_DTYPE_ENDPOINT,
            .bEndpointAddress = EP_OUT,
            .bmAttributes = USB_EPTYPE_INTERRUPT,
            .wMaxPacketSize = EP_SIZE,
            .bInterval = EP_INTERVAL,
        },
};

static const struct usb_string_descriptor hori_manuf_desc = USB_STRING_DESC("HORI CO.,LTD.");
static const struct usb_string_descriptor hori_prod_desc = USB_STRING_DESC("POKKEN CONTROLLER");

static HoriReport s_report;
static HoriReport s_tx;
static volatile bool s_configured = false;
static volatile bool s_suspended = false;
static volatile uint32_t s_connection_id = 0;
static FuriHalUsbInterface* s_prev_intf = NULL;

void hori_report_neutral(HoriReport* r) {
    memset(r, 0, sizeof(*r));
    r->hat = HORI_HAT_CENTER;
    r->lx = r->ly = r->rx = r->ry = HORI_STICK_MID;
}

static void hori_send_current(usbd_device* dev) {
    FURI_CRITICAL_ENTER();
    s_tx = s_report;
    FURI_CRITICAL_EXIT();
    usbd_ep_write(dev, EP_IN, &s_tx, sizeof(s_tx));
}

static void hori_ep_callback(usbd_device* dev, uint8_t event, uint8_t ep) {
    UNUSED(ep);
    if(event == usbd_evt_eptx) {
        hori_send_current(dev);
    } else if(event == usbd_evt_eprx) {
        uint8_t buf[EP_SIZE];
        usbd_ep_read(dev, EP_OUT, buf, sizeof(buf));
    }
}

static usbd_respond hori_ep_config(usbd_device* dev, uint8_t cfg) {
    switch(cfg) {
    case 0:
        s_configured = false;
        usbd_ep_deconfig(dev, EP_IN);
        usbd_ep_deconfig(dev, EP_OUT);
        usbd_reg_endpoint(dev, EP_IN, NULL);
        usbd_reg_endpoint(dev, EP_OUT, NULL);
        return usbd_ack;
    case 1:
        usbd_ep_config(dev, EP_IN, USB_EPTYPE_INTERRUPT, EP_SIZE);
        usbd_ep_config(dev, EP_OUT, USB_EPTYPE_INTERRUPT, EP_SIZE);
        usbd_reg_endpoint(dev, EP_IN, hori_ep_callback);
        usbd_reg_endpoint(dev, EP_OUT, hori_ep_callback);
        hori_send_current(dev);
        s_connection_id++;
        s_configured = true;
        return usbd_ack;
    default:
        return usbd_fail;
    }
}

static usbd_respond
    hori_control(usbd_device* dev, usbd_ctlreq* req, usbd_rqc_callback* callback) {
    UNUSED(callback);
    const uint8_t kind = req->bmRequestType & (USB_REQ_RECIPIENT | USB_REQ_TYPE);

    if(kind == (USB_REQ_INTERFACE | USB_REQ_CLASS) && req->wIndex == 0) {
        switch(req->bRequest) {
        case HID_REQ_SET_IDLE:
        case HID_REQ_SET_REPORT:
        case HID_REQ_SET_PROTOCOL:
            return usbd_ack;
        case HID_REQ_GET_REPORT:
            dev->status.data_ptr = &s_tx;
            dev->status.data_count = sizeof(s_tx);
            return usbd_ack;
        default:
            return usbd_fail;
        }
    }

    if(kind == (USB_REQ_INTERFACE | USB_REQ_STANDARD) && req->wIndex == 0 &&
       req->bRequest == USB_STD_GET_DESCRIPTOR) {
        switch(req->wValue >> 8) {
        case HID_DTYPE_HID:
            dev->status.data_ptr = (uint8_t*)&hori_cfg_desc.hid;
            dev->status.data_count = sizeof(hori_cfg_desc.hid);
            return usbd_ack;
        case HID_DTYPE_REPORT:
            dev->status.data_ptr = (uint8_t*)hori_report_desc;
            dev->status.data_count = sizeof(hori_report_desc);
            return usbd_ack;
        default:
            return usbd_fail;
        }
    }
    return usbd_fail;
}

static void hori_init(usbd_device* dev, FuriHalUsbInterface* intf, void* ctx) {
    UNUSED(intf);
    UNUSED(ctx);
    s_configured = false;
    s_suspended = false;
    usbd_reg_config(dev, hori_ep_config);
    usbd_reg_control(dev, hori_control);
    usbd_connect(dev, true);
}

static void hori_deinit(usbd_device* dev) {
    s_configured = false;
    usbd_reg_config(dev, NULL);
    usbd_reg_control(dev, NULL);
    /* Our callbacks are unloaded with the app */
    usbd_ep_deconfig(dev, EP_IN);
    usbd_ep_deconfig(dev, EP_OUT);
    usbd_reg_endpoint(dev, EP_IN, NULL);
    usbd_reg_endpoint(dev, EP_OUT, NULL);
}

static void hori_wakeup(usbd_device* dev) {
    UNUSED(dev);
    s_suspended = false;
}

static void hori_suspend(usbd_device* dev) {
    UNUSED(dev);
    s_suspended = true;
}

static FuriHalUsbInterface usb_hori = {
    .init = hori_init,
    .deinit = hori_deinit,
    .wakeup = hori_wakeup,
    .suspend = hori_suspend,
    .dev_descr = (struct usb_device_descriptor*)&hori_device_desc,
    .str_manuf_descr = (void*)&hori_manuf_desc,
    .str_prod_descr = (void*)&hori_prod_desc,
    .str_serial_descr = NULL,
    .cfg_descr = (void*)&hori_cfg_desc,
};

bool hori_usb_start(void) {
    hori_report_neutral(&s_report);
    s_tx = s_report;
    s_prev_intf = furi_hal_usb_get_config();
    furi_hal_usb_unlock();
    return furi_hal_usb_set_config(&usb_hori, NULL);
}

void hori_usb_stop(void) {
    if(furi_hal_usb_set_config(s_prev_intf, NULL)) return;

    FURI_LOG_W("Flipparina", "restoring USB mode failed, retrying");
    furi_hal_usb_unlock();
    if(furi_hal_usb_set_config(s_prev_intf, NULL)) return;

    FURI_LOG_E("Flipparina", "could not restore USB mode, disabling it");
    furi_hal_usb_set_config(NULL, NULL);
}

uint32_t hori_usb_connection_id(void) {
    return s_connection_id;
}

static uint32_t s_woken_id = 0;

bool hori_usb_needs_wake(void) {
    return s_woken_id != s_connection_id;
}

void hori_usb_wake(uint16_t button, uint32_t hold_ms, uint32_t settle_ms) {
    HoriReport report;
    hori_report_neutral(&report);
    report.buttons = button;
    hori_usb_set_report(&report);
    furi_delay_ms(hold_ms);
    hori_report_neutral(&report);
    hori_usb_set_report(&report);
    furi_delay_ms(settle_ms);
    s_woken_id = s_connection_id;
}

bool hori_usb_is_connected(void) {
    return s_configured && !s_suspended;
}

void hori_usb_set_report(const HoriReport* r) {
    FURI_CRITICAL_ENTER();
    s_report = *r;
    FURI_CRITICAL_EXIT();
}
