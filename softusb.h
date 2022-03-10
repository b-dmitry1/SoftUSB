#pragma once

/////////////////////////////////////////////////////////////////////////
// Platform macros
/////////////////////////////////////////////////////////////////////////

// STM32F4XX
#include "platform_stm32f4.h"

/////////////////////////////////////////////////////////////////////////
// Consts
/////////////////////////////////////////////////////////////////////////

#define USB_DEVICE_NOT_CONNECTED		0
#define USB_DEVICE_KEYBOARD				1
#define USB_DEVICE_MOUSE				2
#define USB_DEVICE_FULLSPEED			254
#define USB_DEVICE_UNKNOWN				255

#define HID_MAX_PRESSED_KEYS			3

#define KEYBOARD_BUFFER_SIZE			32

#define KEYBOARD_CONTROL_CTRL			1
#define KEYBOARD_CONTROL_SHIFT			2
#define KEYBOARD_CONTROL_ALT			4

#define MOUSE_LEFT_LIMIT				0
#define MOUSE_TOP_LIMIT					0
#define MOUSE_RIGHT_LIMIT				639
#define MOUSE_BOTTOM_LIMIT				479

/////////////////////////////////////////////////////////////////////////
// USB descriptors
/////////////////////////////////////////////////////////////////////////

// Standard USB device descriptor
typedef struct
{
	unsigned char length;
	unsigned char descr_type;
	unsigned char usb_spec[2];
	unsigned char device_class;
	unsigned char device_subclass;
	unsigned char protocol;
	unsigned char packet_size_ep0;
	unsigned char vendor[2];
	unsigned char product[2];
	unsigned char version[2];
	unsigned char manufacturer_str_index;
	unsigned char product_str_index;
	unsigned char serial_number_str_index;
	unsigned char num_configurations;
} usb_device_descriptor_t;

// Configuration descriptor
typedef struct
{
	unsigned char length;
	unsigned char descr_type;
	unsigned char total_length[2];
	unsigned char num_interfaces;
	unsigned char configuration_value;
	unsigned char configuration_str_index;
	unsigned char atrributes;
	unsigned char power;
} usb_configuration_descriptor_t;

// Interface descriptor
typedef struct
{
	unsigned char length;
	unsigned char descr_type;
	unsigned char interface_number;
	unsigned char alternate_setting;
	unsigned char num_endpoints;
	unsigned char interface_class;
	unsigned char interface_subclass;
	unsigned char protocol;
	unsigned char interface_str_index;
} usb_interface_descriptor_t;


/////////////////////////////////////////////////////////////////////////
// SoftUsb
/////////////////////////////////////////////////////////////////////////

enum SoftUsbState
{
	su_nodevice, su_fullspeed, su_debounce, su_reset, su_connected,
	su_read_descr, su_set_address, su_wait_address,
	su_query_conf_descr, su_read_conf_descr,
	su_set_conf, su_wait_conf, su_work
};

// Circular buffer
class KeyboardBuffer
{
public:
	KeyboardBuffer();

	void add(unsigned char code);
	int get();
	int is_empty();

private:
	unsigned char _buffer[KEYBOARD_BUFFER_SIZE];
	int _rp;
	int _wp;
	int _n;
};

class SoftUsb
{
public:
	SoftUsb(unsigned int port, unsigned int mpin, unsigned int ppin);
	
	// State machine timer should be called every 1 ms
	void timer1ms(int allow_long_work = 1);

	// Low-level information
	SoftUsbState get_state();
	const usb_device_descriptor_t *get_device_descriptor();
	const usb_configuration_descriptor_t *get_conf_descriptor();
	const usb_interface_descriptor_t *get_interface_descriptor();
	const unsigned char *get_device_report();

	// Connection status and identification
	int is_connected();
	int get_device_type();
	unsigned short get_vendor_id();
	unsigned short get_device_id();

	// Keyboard
	int getch();
	int kbhit();
	int get_key_code();
	
	// Mouse
	void get_mouse_pos(int &x, int &y, int &buttons, int &wheel);

private:
	SoftUsbState _state;
	unsigned int _port;
	unsigned int _mpin;
	unsigned int _ppin;
	unsigned int _mmask;
	unsigned int _pmask;
	unsigned int _mpmask;
	unsigned int _timer;
	unsigned int _state_timer;
	unsigned int _retries;
	unsigned char _descriptor[18];
	unsigned char _conf_descriptor[18];
	unsigned char _report[8];
	unsigned int _descr_offset;
	int _data_0;

	unsigned short _vendor_id;
	unsigned short _device_id;
	unsigned char _device_class;
	unsigned char _device_subclass;

	// HID data
	unsigned char _keys_pressed[HID_MAX_PRESSED_KEYS];
	unsigned char _keys_pressed_prev[HID_MAX_PRESSED_KEYS];
	unsigned char _keyb_control;
	KeyboardBuffer _keyb_buffer;
	KeyboardBuffer _keyb_chars_buffer;
	int _mouse_x;
	int _mouse_y;
	int _mouse_b;
	int _mouse_wheel;

	SOFTUSB_PLATFORM_PRIVATE;

	// CRC calculation
	int token_data(int addr, int ep);
	unsigned short crc16(const unsigned char *data, int count);
	
	// Low-level
	void wait(int n);
	void eop();
	void keepalive();
	void send(unsigned char *data, int count);
	void send_token(int pid, int addr, int ep);
	int receive(unsigned char *buffer, int n);

	// Transport
	int usb_write(int trans_type, int addr, int ep, const unsigned char *data, int count);
	int usb_read(int trans_type, int addr, int ep, unsigned char *buffer);

	// State machine
	void set_state(SoftUsbState newstate);
	void process_nodevice();
	void process_fullspeed();
	void process_debounce();
	void process_reset();
	void process_connected();
	void process_read_descr();
	void process_query_conf_descr();
	void process_read_conf_descr();
	void process_set_address();
	void process_wait_address();
	void process_set_conf();
	void process_wait_conf();
	void process_work();
	
	// Reports
	void parse_keyboard_report();
	void parse_mouse_report();
	void add_key(int code);
};
