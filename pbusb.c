#include <stdlib.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/f1/nvic.h>
#include <libopencm3/usb/dfu.h>

#include "usb.h"
#include "output.h"
#include "led.h"
#include "battery.h"
#include "button.h"
#include "piezo.h"
#include "dfu-bootloader/usbdfu.h"

#define delay(x) do { for (int i = 0; i < x * 1000; i++) \
                          __asm__("nop"); \
                    } while(0);

bool re_enter_bootloader = false;

static usbd_device *usbd_dev;

static const struct usb_device_descriptor usb_descr = {
        .bLength = USB_DT_DEVICE_SIZE,
        .bDescriptorType = USB_DT_DEVICE,
        .bcdUSB = 0x0200,
        .bDeviceClass = 0,
        .bDeviceSubClass = 0,
        .bDeviceProtocol = 0,
        .bMaxPacketSize0 = 64,
        .idVendor = SR_DEV_VID,
        .idProduct = SR_DEV_PID,
        .bcdDevice = SR_DEV_REV,
        .iManufacturer = 1,
        .iProduct = 2,
        .iSerialNumber = 3,
        .bNumConfigurations = 1,
};

const struct usb_dfu_descriptor sr_dfu_function = {
        .bLength = sizeof(struct usb_dfu_descriptor),
        .bDescriptorType = DFU_FUNCTIONAL,
        .bmAttributes = USB_DFU_CAN_DOWNLOAD | USB_DFU_WILL_DETACH,
        .wDetachTimeout = 255,
        .wTransferSize = 128,
        .bcdDFUVersion = 0x011A,
};

const struct usb_interface_descriptor dfu_iface = {
        .bLength = USB_DT_INTERFACE_SIZE,
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = 0,
        .bAlternateSetting = 0,
        .bNumEndpoints = 0,
        .bInterfaceClass = 0xFE, // Application specific class code
        .bInterfaceSubClass = 0x01, // DFU
        .bInterfaceProtocol = 0x01, // Protocol 1.0
        .iInterface = 4,
	.extra = &sr_dfu_function,
	.extralen = sizeof(sr_dfu_function),
};

const struct usb_interface usb_ifaces[] = {{
        .num_altsetting = 1,
        .altsetting = &dfu_iface,
}};

static const struct usb_config_descriptor usb_config = {
        .bLength = USB_DT_CONFIGURATION_SIZE,
        .bDescriptorType = USB_DT_CONFIGURATION,
        .wTotalLength = 0,
        .bNumInterfaces = 1,
        .bConfigurationValue = 1,
        .iConfiguration = 0,
        .bmAttributes = 0xC0,  // Bit 6 -> self powered
        .bMaxPower = 5,        // Will consume 10ma from USB (a guess)
	.interface = usb_ifaces,
};

static const char *usb_strings[] = {
        "Student Robotics",
        "Power board v4",
        (const char *)SERIALNUM_BOOTLOADER_LOC,
	"Student Robotics Power board v4", // Iface 1
	"Student Robotics Power board DFU loader", // IFace 2, DFU
};

static uint8_t usb_data_buffer[128];

static int
read_output(int *len, uint8_t **buf, int output)
{
	if (*len < 4)
		return USBD_REQ_NOTSUPP;

	uint32_t *u32ptr = (uint32_t*)*buf;
	*u32ptr = current_sense_read(output);
	*len = 4;
	return USBD_REQ_HANDLED;
}

static int
handle_read_req(struct usb_setup_data *req, int *len, uint8_t **buf)
{
	int result = USBD_REQ_NOTSUPP; // Will result in a USB stall
	uint16_t *u16ptr;
	uint32_t *u32ptr;

	// Precise command, as enumerated in usb.h, is in wIndex
	switch (req->wIndex) {
	case POWERBOARD_READ_OUTPUT0:
		result = read_output(len, buf, 0); break;
	case POWERBOARD_READ_OUTPUT1:
		result = read_output(len, buf, 1); break;
	case POWERBOARD_READ_OUTPUT2:
		result = read_output(len, buf, 2); break;
	case POWERBOARD_READ_OUTPUT3:
		result = read_output(len, buf, 3); break;
	case POWERBOARD_READ_OUTPUT4:
		result = read_output(len, buf, 4); break;
	case POWERBOARD_READ_OUTPUT5:
		result = read_output(len, buf, 5); break;
	case POWERBOARD_READ_5VRAIL:
		if (*len < 4)
			break;

		*len = 4;

		// Clocking i2c can take a lot of time!
		u16ptr = (uint16_t*) *buf;
#if 0
		// XXX jmorse
		*u16ptr++ = f_vshunt();
		*u16ptr++ = f_vbus();
#endif
		result = USBD_REQ_HANDLED;
		break;

	case POWERBOARD_READ_BATT:
		if (*len < 8)
			break;

		*len = 8;

		u32ptr = (uint32_t*) *buf;
		*u32ptr++ = read_battery_current();
		*u32ptr++ = read_battery_voltage();
		result = USBD_REQ_HANDLED;
		break;

	case POWERBOARD_READ_BUTTON:
		if (*len < 4)
			break;

		*len = 4;
		u32ptr = (uint32_t*)*buf;
		*u32ptr++ = button_pressed();
		result = USBD_REQ_HANDLED;
		break;
	case POWERBOARD_READ_FWVER:
		if (*len < 4)
			break;

		*len = 4;

		u32ptr = (uint32_t*)*buf;
		*u32ptr++ = FW_VER;
		result = USBD_REQ_HANDLED;
		break;
	default:
		break;
	}

	return result;
}

static void
write_output(int id, uint16_t param)
{

	if (param == 0) {
		// Set output off
		output_off(id);
	} else {
		output_on(id);
	}
}

static void
write_led(int id, uint16_t param)
{

	if (param == 0) {
		// Set led off
		led_clear(id);
	} else {
		led_set(id);
	}
}

static int
handle_write_req(struct usb_setup_data *req)
{

	switch (req->wIndex) {
	case POWERBOARD_WRITE_OUTPUT0:
		write_output(0, req->wValue); break;
	case POWERBOARD_WRITE_OUTPUT1:
		write_output(1, req->wValue); break;
	case POWERBOARD_WRITE_OUTPUT2:
		write_output(2, req->wValue); break;
	case POWERBOARD_WRITE_OUTPUT3:
		write_output(3, req->wValue); break;
	case POWERBOARD_WRITE_OUTPUT4:
		write_output(4, req->wValue); break;
	case POWERBOARD_WRITE_OUTPUT5:
		write_output(5, req->wValue); break;
	case POWERBOARD_WRITE_RUNLED:
		write_led(LED_RUN, req->wValue); break;
	case POWERBOARD_WRITE_ERRORLED:
		write_led(LED_ERROR, req->wValue); break;
	case POWERBOARD_WRITE_PIEZO:
		if (!piezo_recv(req->wLength, usb_data_buffer))
			break;
	default:
		return USBD_REQ_NOTSUPP; // Will result in a USB stall
	}

	return USBD_REQ_HANDLED;
}

static int
control(usbd_device *usbd_dev, struct usb_setup_data *req, uint8_t **buf,
	uint16_t *len,
	void (**complete)(usbd_device *usbd_dev, struct usb_setup_data *req))
{
	// Only respond to a single device request, number 64. USB spec 3.1
	// Section 9.3.1 allows us to define additional requests, and Table 9.5
	// identifies all reserved requests. So, pick 64, it could be any.
	if (req->bRequest != 64)
		return USBD_REQ_NEXT_CALLBACK;

	// Data and length are in *buf and *len respectively. Output occurs by
	// modifying what those point at.

	if (req->bmRequestType & USB_REQ_TYPE_IN) { // i.e., input to host
		return handle_read_req(req, len, buf);
	} else {
		return handle_write_req(req);
	}
}

static int
iface_control(usbd_device *usbd_dev, struct usb_setup_data *req, uint8_t **buf,
	uint16_t *len,
	void (**complete)(usbd_device *usbd_dev, struct usb_setup_data *req))
{

	// For standard requests, handle only set_iface, with no alternative
	// ifaces.
	if (req->bmRequestType == (USB_REQ_TYPE_STANDARD|USB_REQ_TYPE_INTERFACE)
			&& req->bRequest == USB_REQ_SET_INTERFACE
			&& req->wValue == 0) {
		// Two ifaces: this one and DFU.
		if (req->wIndex == 0) {
			// Do a special dance; but later.
			return USBD_REQ_HANDLED;
		}
	}

	// Otherwise, we might be getting DFU requests
	if ((req->bmRequestType & (USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT))
		== (USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE))
	{
		switch (req->bRequest) {
		case DFU_GETSTATUS:
			*len = 6;
			(*buf)[0] = STATE_APP_IDLE;
			(*buf)[1] = 100; // ms
			(*buf)[2] = 0;
			(*buf)[3] = 0;
			(*buf)[4] = STATE_APP_IDLE;
			(*buf)[5] = 0;
			return USBD_REQ_HANDLED;
		case DFU_DETACH:
			re_enter_bootloader = true;
			return USBD_REQ_HANDLED;
		}
	}

	return USBD_REQ_NOTSUPP;
}

static void
set_config_cb(usbd_device *usbd_dev, uint16_t wValue)
{

  // We want to handle device requests sent to the default endpoint: match the
  // device request type (0), with zero recpient address. Use type + recipient
  // mask to filter requests.
  usbd_register_control_callback(usbd_dev, USB_REQ_TYPE_DEVICE,
		  USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
		  control);

  // Additionally, register our own SR-interface callback. This is simply to
  // handle SET_INTERFACE, which libopencm3 doesn't do. Filter options are to
  // match standard request, to the interface recipient.
  usbd_register_control_callback(usbd_dev,
		  USB_REQ_TYPE_STANDARD | USB_REQ_TYPE_INTERFACE,
		  USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
		  iface_control);

  // Use the same function to catch initial DFU requests. These are class
  // commands.
  usbd_register_control_callback(usbd_dev,
		  USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
		  USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
		  iface_control);

  // Indicate (on boot) that we've been enumerated -- the LED will have been
  // RED/GREEN, it will now be solid green.
  led_clear(LED_ERROR);
}

extern void usb_reset_callback(void);

void
usb_init()
{

  gpio_clear(GPIOA, GPIO8);
  gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO8);

  usbd_dev = usbd_init(&stm32f103_usb_driver, &usb_descr, &usb_config,
		 usb_strings, 3, usb_data_buffer, sizeof(usb_data_buffer));

  usbd_register_set_config_callback(usbd_dev, set_config_cb);
  usbd_register_reset_callback(usbd_dev, usb_reset_callback);

  gpio_set(GPIOA, GPIO8);

  // Enable low priority general purpose USB intr
  nvic_enable_irq(NVIC_USB_LP_CAN_RX0_IRQ);
  // Set USB to be low priority: it will still execute in interrupt context
  // and block the main thread, however all the other interrupts (clock,
  // analogue, etc) will interrupt on top of this.
  nvic_set_priority(NVIC_USB_LP_CAN_RX0_IRQ, 16);
}

void
usb_deinit()
{

  // Gate USB; this will cause a reset for us and the  host.
  gpio_clear(GPIOA, GPIO8);

  // Disable intr
  nvic_disable_irq(NVIC_USB_LP_CAN_RX0_IRQ);

  // Do nothing for a few ms, then poll a few times to ensure that the driver
  // has reset itself
  delay(20);
  usbd_poll(usbd_dev);
  usbd_poll(usbd_dev);
  usbd_poll(usbd_dev);
  usbd_poll(usbd_dev);

  // That should be enough
  return;
}

void
usb_lp_can_rx0_isr()
{

	usbd_poll(usbd_dev);
}
