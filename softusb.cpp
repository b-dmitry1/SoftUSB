#include "softusb.h"


/////////////////////////////////////////////////////////////////////////
// Consts
/////////////////////////////////////////////////////////////////////////

#define DEBOUNCE_MS				500
#define RESET_MS				20
#define SOFTUSB_RETRIES			50
#define SOFTUSB_PACKET_PAUSE_MS	10
#define SOFTUSB_BUFFER_SIZE		20

#define TOKEN_OUT				0xE1
#define TOKEN_IN				0x69
#define TOKEN_SETUP				0x2D

#define DATA_DATA0				0xC3
#define DATA_DATA1				0x4B

#define HANDSHAKE_ACK			0xD2
#define HANDSHAKE_NAK			0x5A
#define HANDSHAKE_STALL			0x1E


#define TRANS_OUT				0xE1
#define TRANS_IN				0x69
#define TRANS_SETUP				0x2D




#if (HID_MAX_PRESSED_KEYS < 1 || HID_MAX_PRESSED_KEYS > 4)
	#error "HID_MAX_PRESSED_KEYS must be in range 1..4"
#endif

/////////////////////////////////////////////////////////////////////////
// SoftUSB
/////////////////////////////////////////////////////////////////////////

const unsigned char xt_codes[] =
{
	0x00, 0x00, 0x00, 0x00, 0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26,
	0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C, 0x02, 0x03,
	0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x1C, 0x01, 0x0E, 0x0F, 0x39, 0x0C, 0x0D, 0x1A,
	0x1B, 0x2B, 0x2B, 0x27, 0x28, 0x29, 0x33, 0x34, 0x35, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40,
	0x41, 0x42, 0x43, 0x44, 0x57, 0x58, 0x00, 0x46, 0x00, 0x52, 0x47, 0x49, 0x53, 0x4F, 0x51, 0x4D,
	0x4B, 0x50, 0x48, 0x45, 0x35, 0x37, 0x4A, 0x4E, 0x1C, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47,
	0x48, 0x49, 0x52, 0x53, 0x2B, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const unsigned char ascii_lower[] =
{
	0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 8, 9,
	'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 13, 0, 'a', 's',
	'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '~', 0, '\\', 'z', 'x', 'c', 'v',
	'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '-', '+', 0, 0, 0, 0
};

const unsigned char ascii_upper[] =
{
	0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 8, 9,
	'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 10, 0, 'A', 'S',
	'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', 34, '`', 0, '|', 'Z', 'X', 'C', 'V',
	'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '-', '+', 0, 0, 0, 0
};

KeyboardBuffer::KeyboardBuffer()
{
	_rp = 0;
	_wp = 0;
	_n = 0;
}

void KeyboardBuffer::add(unsigned char code)
{
	// Check for buffer overflow
	if (_n >= sizeof(_buffer) / sizeof(_buffer[0]))
	{
		_rp %= KEYBOARD_BUFFER_SIZE;
		_n--;
	}
	
	_buffer[_wp++] = code;
	_wp %= KEYBOARD_BUFFER_SIZE;
	_n++;
}

int KeyboardBuffer::get()
{
	int res;
	
	if (_n <= 0)
	{
		return 0;
	}
	
	res = _buffer[_rp++];
	_rp %= KEYBOARD_BUFFER_SIZE;
	_n--;
	
	return res;
}

int KeyboardBuffer::is_empty()
{
	return _n == 0;
}

SoftUsb::SoftUsb(unsigned int port, unsigned int mpin, unsigned int ppin)
{
	int i;
	
	set_state(su_nodevice);
	_port = port;
	_mpin = mpin;
	_ppin = ppin;
	_mmask = 1u << _mpin;
	_pmask = 1u << _ppin;
	_mpmask = _mmask | _pmask;
	_timer = 0;
	_state_timer = 0;
	_retries = 0;
	_data_0 = 1;
	_descr_offset = 0;
	
	_keyb_control = 0;
	_mouse_x = 0;
	_mouse_y = 0;
	_mouse_b = 0;
	_mouse_wheel = 0;
	
	for (i = 0; i < sizeof(_keys_pressed); i++)
	{
		_keys_pressed[i] = 0;
	}
	
	for (i = 0; i < sizeof(_keys_pressed_prev); i++)
	{
		_keys_pressed_prev[i] = 0;
	}
	
	SOFTUSB_PLATFORM_CTOR;
}

void SoftUsb::timer1ms(int allow_long_work)
{
	if (_timer > 0)
	{
		_timer--;
		return;
	}
	
	switch (_state)
	{
		case su_nodevice:
			process_nodevice();
			return;
		case su_fullspeed:
			process_fullspeed();
			return;
		case su_debounce:
			process_debounce();
			return;
		case su_reset:
			process_reset();
			return;
		default:
			break;
	}
	
	keepalive();
	
	if (_state_timer > 0)
	{
		_state_timer--;
		return;
	}
	
	if (!allow_long_work)
	{
		return;
	}
	
	switch (_state)
	{
		case su_connected:
			process_connected();
			break;
		case su_read_descr:
			process_read_descr();
			break;
		case su_query_conf_descr:
			process_query_conf_descr();
			break;
		case su_read_conf_descr:
			process_read_conf_descr();
			break;
		case su_set_address:
			process_set_address();
			break;
		case su_wait_address:
			process_wait_address();
			break;
		case su_set_conf:
			process_set_conf();
			break;
		case su_wait_conf:
			process_wait_conf();
			break;
		case su_work:
			process_work();
			break;
		default:
			break;
	}
}

SoftUsbState SoftUsb::get_state()
{
	return _state;
}

int SoftUsb::is_connected()
{
	return _state == su_work;
}

int SoftUsb::get_device_type()
{
	if (_state == su_fullspeed)
	{
		return USB_DEVICE_FULLSPEED;
	}
	
	if (!is_connected())
	{
		return USB_DEVICE_NOT_CONNECTED;
	}
	
	const usb_interface_descriptor_t *intf = get_interface_descriptor();
	
	if (intf->interface_subclass == 1)
	{
		switch (get_interface_descriptor()->protocol)
		{
			case 1:
				return USB_DEVICE_KEYBOARD;
			case 2:
				return USB_DEVICE_MOUSE;
		}
	}
	
	return USB_DEVICE_UNKNOWN;
}

const usb_device_descriptor_t *SoftUsb::get_device_descriptor()
{
	return (usb_device_descriptor_t *)_descriptor;
}

const usb_configuration_descriptor_t *SoftUsb::get_conf_descriptor()
{
	return (usb_configuration_descriptor_t *)_conf_descriptor;
}

const usb_interface_descriptor_t *SoftUsb::get_interface_descriptor()
{
	return (usb_interface_descriptor_t *)&_conf_descriptor[9];
}

const unsigned char *SoftUsb::get_device_report()
{
	return _report;
}

unsigned short SoftUsb::get_vendor_id()
{
	return _vendor_id;
}

unsigned short SoftUsb::get_device_id()
{
	return _vendor_id;
}

int SoftUsb::getch()
{
	int res;
	
	SOFTUSB_DISABLE_IRQ;
	
	res = _keyb_chars_buffer.get();
	
	SOFTUSB_ENABLE_IRQ;
	
	return res;
}

int SoftUsb::kbhit()
{
	return !_keyb_chars_buffer.is_empty();
}

int SoftUsb::get_key_code()
{
	int res;
	
	SOFTUSB_DISABLE_IRQ;
	
	res = _keyb_buffer.get();
	
	SOFTUSB_ENABLE_IRQ;
	
	return res;
}

void SoftUsb::get_mouse_pos(int &x, int &y, int &buttons, int &wheel)
{
	x = _mouse_x;
	y = _mouse_y;
	buttons = _mouse_b;
	wheel = _mouse_wheel;
}

void SoftUsb::add_key(int code)
{
	_keyb_buffer.add(code);
	
	if (code & 0x80)
	{
		return;
	}
	
	if (_keyb_control & KEYBOARD_CONTROL_SHIFT)
	{
		code = ascii_upper[code];
	}
	else
	{
		code = ascii_lower[code];
	}
	
	if (code == 0)
	{
		return;
	}
	
	_keyb_chars_buffer.add(code);
}

void SoftUsb::set_state(SoftUsbState newstate)
{
	_timer = 0;
	_state_timer = 0;
	_retries = 0;
	
	_state = newstate;
}

void SoftUsb::wait(int n)
{
	unsigned int t;
	int i;
	for (i = 0; i < n; i++)
	{
		SOFTUSB_WAIT;
	}
}

void SoftUsb::eop()
{
	unsigned int t;
	SOFTUSB_WAIT;
	SOFTUSB_Z;
	SOFTUSB_WAIT;
	SOFTUSB_WAIT;
	SOFTUSB_M;
	SOFTUSB_WAIT;
}

void SoftUsb::keepalive()
{
	unsigned int v;
	
	SOFTUSB_READ(v);
	v &= _mpmask;
	
	if (v != _mmask)
	{
		set_state(su_nodevice);
		_timer = DEBOUNCE_MS;
		return;
	}
	
	SOFTUSB_OUTPUT;
	
	eop();
	
	SOFTUSB_INPUT;
}

// Send packet
void SoftUsb::send(unsigned char *data, int count)
{
	unsigned int t;
	int i, j;
	unsigned int b = _m;

	SOFTUSB_OUTPUT;
	
	SOFTUSB_M;
	
	SOFTUSB_WAIT;
	
	SOFTUSB_BEGIN_INTERVAL;
	
	for (i = 0; i < count; i++)
	{
		for (j = 0; j < 8; j++)
		{
			if ((data[i] & (1 << j)) == 0)
				b ^= _m | _p;
			SOFTUSB_WAIT_TICK;
			SOFTUSB_OUT(b);
			SOFTUSB_BEGIN_INTERVAL;
		}
	}

	SOFTUSB_WAIT_TICK;
	
	SOFTUSB_Z;
	SOFTUSB_WAIT;
	SOFTUSB_WAIT;
	SOFTUSB_M;

	SOFTUSB_BEGIN_INTERVAL;

	SOFTUSB_INPUT;

	SOFTUSB_WAIT;
}

// Create token with addr and ep values
int SoftUsb::token_data(int addr, int ep)
{
	unsigned short b = 0x1F;
	int i;
	unsigned short a = addr + ep * 128;
	unsigned short d = a;
	
	for (i = 0; i <= 10; i++)
	{
		if ((d ^ b) & 1)
		{
			b >>= 1;
			b ^= 0x14;
		}
		else
			b >>= 1;
		d >>= 1;
	}
	
	b ^= 0x1F;
	
	return a + (b << 11);
}

// USB CRC16
unsigned short SoftUsb::crc16(const unsigned char *data, int count)
{
	int i, j;
	unsigned short crc = 0xFFFFu;
	
	for (i = 0; i < count; i++)
	{
		crc ^= data[i];
		
		for (j = 0; j < 8; j++)
		{
			if (crc & 0x0001)
				crc = (crc >> 1) ^ 0xA001;
			else
				crc = crc >> 1;
		}
	}
	
	return crc ^ 0xFFFFu;
}

void SoftUsb::send_token(int pid, int addr, int ep)
{
	unsigned char buf[4];
	unsigned short data;
	
	data = token_data(addr, ep);
	
	buf[0] = 0x80;
	buf[1] = pid;
	buf[2] = data & 0xFF;
	buf[3] = data >> 8;
	
	SOFTUSB_OUTPUT;
	
	eop();

	send(buf, sizeof(buf));
}

// This is the most important function
// We should run very quickly
// The timer used to measure bit intervals should be very accurate
int SoftUsb::receive(unsigned char *buffer, int n)
{
	unsigned int t;
	int res = 0;
	int i, j;
	unsigned int v = _pmask, g = _mmask;
	int ones = 0;
	volatile int w;

	// Wait for response
	i = 0;
	while (1)
	{
		SOFTUSB_READ(g);
		
		if (g & _pmask)
		{
			break;
		}
		
		if (i++ > 10000)
			return -1;
	}
	
	t = 0;
	
	TIMER_1500_KHZ_SYNC;
	
	for (i = 0; i < n; i++)
	{
		for (j = i == 0; j < 8; j++)
		{
			SOFTUSB_WAIT_TICK;
			SOFTUSB_BEGIN_INTERVAL;
			res >>= 1;
			SOFTUSB_READ(g);
			g &= _mpmask;
			
			// Detect EOP
			if (g == 0)
			{
				break;
			}
			
			if ((v ^ g) == 0)
			{
				res |= 0x80;
				ones++;
				if (ones == 6)
				{
					ones = 0;
					SOFTUSB_WAIT_TICK;
					SOFTUSB_BEGIN_INTERVAL;
					SOFTUSB_READ(g);
					g &= _mpmask;
				}
			}
			else
				ones = 0;
			v = g;
		}
		
		if (g == 0)
		{
			break;
		}
		
		buffer[i] = res;
		res = 0;
	}
	
	return i;
}


int SoftUsb::usb_write(int trans_type, int addr, int ep, const unsigned char *data, int count)
{
	unsigned char buf[12];
	int i;
	unsigned short crc = crc16(data, count);
	
	if (count > 8)
	{
		count = 8;
	}
	
	buf[0] = 0x80;
	buf[1] = trans_type == TRANS_OUT ? DATA_DATA1 : DATA_DATA0;
	
	_data_0 = !_data_0;
	
	for (i = 0; i < count; i++)
	{
		buf[2 + i] = data[i];
	}
	
	buf[2 + count] = crc & 0xFF;
	buf[3 + count] = crc >> 8;

	send_token(trans_type, addr, ep);
	
	send(buf, 4 + count);
	
	i = receive(buf, 2);

	if (i == 2)
	{
		return buf[1];
	}
	
	return -1;
}

int SoftUsb::usb_read(int trans_type, int addr, int ep, unsigned char *buffer)
{
	unsigned char buf[SOFTUSB_BUFFER_SIZE];
	unsigned char buf1[SOFTUSB_BUFFER_SIZE];
	int i, n;
	unsigned short crc;
	
	// Erase CRC
	for (i = 8; i < sizeof(buf); i++)
	{
		buf[i] = i;
	}
	
	send_token(trans_type, addr, ep);
	
	n = receive(buf, 12);
	
	SOFTUSB_OUTPUT;

	buf1[0] = 0x80;
	buf1[1] = HANDSHAKE_ACK;
	
	if (buf[1] == DATA_DATA0 || buf[1] == DATA_DATA1)
	{
		crc = crc16(&buf[2], n - 4);
		
		if ((buf[n - 2] != (crc & 0xFF)) || (buf[n - 1] != (crc >> 8)))
		{
			buf1[1] = HANDSHAKE_NAK;
		}
	}
	
	send(buf1, 2);
	
	if (n < 2)
	{
		return -1;
	}
	
	if (buf1[1] == HANDSHAKE_NAK)
	{
		return HANDSHAKE_NAK;
	}
	
	if (n > SOFTUSB_BUFFER_SIZE)
	{
		n = SOFTUSB_BUFFER_SIZE;
	}
	
	if ((buf[1] != DATA_DATA0) && (buf[1] != DATA_DATA1))
	{
		return buf[1];
	}
	
	for (i = 0; i < n; i++)
	{
		buffer[i] = buf[2+ i];
	}
	
	return n;
}

// State machine
void SoftUsb::process_nodevice()
{
	unsigned int v;
	
	SOFTUSB_INPUT;
	
	SOFTUSB_READ(v);
	
	if (v & _pmask)
	{
		set_state(su_fullspeed);
		return;
	}

	if (v & _mmask)
	{
		set_state(su_debounce);
		_timer = DEBOUNCE_MS;
	}
}

void SoftUsb::process_fullspeed()
{
	unsigned int v;
	
	SOFTUSB_READ(v);
	
	if (v & _pmask)
	{
		return;
	}
	
	set_state(su_nodevice);
}

void SoftUsb::process_debounce()
{
	unsigned int v;
	
	SOFTUSB_READ(v);
	
	if (v & _mmask)
	{
		set_state(su_reset);
		_timer = RESET_MS;
		SOFTUSB_Z;
		SOFTUSB_OUTPUT;
		return;
	}
	
	set_state(su_nodevice);
}

void SoftUsb::process_reset()
{
	SOFTUSB_M;
	SOFTUSB_INPUT;
	
	set_state(su_connected);
	_state_timer = 100;
}

void SoftUsb::process_connected()
{
	int res;

	const unsigned char get_descriptor[8] = {0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x12, 0x00};
	
	res = usb_write(TRANS_SETUP, 0, 0, get_descriptor, sizeof(get_descriptor));
	
	if (res != HANDSHAKE_ACK)
	{
		_retries++;
		_state_timer = SOFTUSB_PACKET_PAUSE_MS;
		
		if (_retries > SOFTUSB_RETRIES)
		{
			set_state(su_nodevice);
		}
		
		return;
	}
	
	_data_0 = 1;
	_descr_offset = 0;
	
	set_state(su_read_descr);
	_state_timer = SOFTUSB_PACKET_PAUSE_MS;
}

void SoftUsb::process_read_descr()
{
	int res;
	unsigned char buf[SOFTUSB_BUFFER_SIZE];
	int i;

	res = usb_read(TRANS_IN, 0, 0, buf);

	if (res <= 0 || res == HANDSHAKE_NAK)
	{
		_retries++;
		_state_timer = SOFTUSB_PACKET_PAUSE_MS;
		
		if (_retries > SOFTUSB_RETRIES)
		{
			set_state(su_nodevice);
		}
		
		return;
	}
	
	for (i = 0; i < 8; i++)
	{
		if (i + _descr_offset >= sizeof(_descriptor))
		{
			break;
		}
		
		_descriptor[i + _descr_offset] = buf[i];
	}
	
	_descr_offset += 8;
	
	if (_descr_offset < 18)
	{
		_data_0 = !_data_0;
	}
	else
	{
		_device_class = _descriptor[4];
		_device_subclass = _descriptor[5];
		_vendor_id = _descriptor[9] * 256 + _descriptor[8];
		_device_id = _descriptor[11] * 256 + _descriptor[10];
		
		set_state(su_set_address);
		_state_timer = SOFTUSB_PACKET_PAUSE_MS;
	}
}

void SoftUsb::process_set_address()
{
	int res;
	
	const unsigned char set_address[8] = {0x00, 0x05, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};

	res = usb_write(TRANS_SETUP, 0, 0, set_address, sizeof(set_address));

	if (res != HANDSHAKE_ACK)
	{
		_retries++;
		_state_timer = SOFTUSB_PACKET_PAUSE_MS;
		
		if (_retries > SOFTUSB_RETRIES)
		{
			set_state(su_nodevice);
		}
		
		return;
	}
	
	set_state(su_wait_address);
}

void SoftUsb::process_wait_address()
{
	int res;
	unsigned char buf[SOFTUSB_BUFFER_SIZE];

	res = usb_read(TRANS_IN, 0, 0, buf);

	if (res == HANDSHAKE_NAK)
	{
		_retries++;
		_state_timer = SOFTUSB_PACKET_PAUSE_MS;
		
		if (_retries > SOFTUSB_RETRIES)
		{
			set_state(su_nodevice);
		}
		
		return;
	}
	
	set_state(su_set_conf);
	_state_timer = SOFTUSB_PACKET_PAUSE_MS;
}

void SoftUsb::process_set_conf()
{
	int res;
	
	const unsigned char set_configuration[8] = {0x00, 0x09, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};

	res = usb_write(TRANS_SETUP, 1, 0, set_configuration, sizeof(set_configuration));

	if (res != HANDSHAKE_ACK)
	{
		_retries++;
		_state_timer = SOFTUSB_PACKET_PAUSE_MS;
		
		if (_retries > SOFTUSB_RETRIES)
		{
			set_state(su_nodevice);
		}
		
		return;
	}
	
	set_state(su_wait_conf);
}

void SoftUsb::process_wait_conf()
{
	int res;
	unsigned char buf[SOFTUSB_BUFFER_SIZE];

	res = usb_read(TRANS_IN, 1, 0, buf);

	if (res <= 0)
	{
		_retries++;
		_state_timer = SOFTUSB_PACKET_PAUSE_MS;
		
		if (_retries > SOFTUSB_RETRIES)
		{
			set_state(su_nodevice);
		}
		
		return;
	}
	
	set_state(su_query_conf_descr);
	_state_timer = SOFTUSB_PACKET_PAUSE_MS;
}

void SoftUsb::process_query_conf_descr()
{
	int res;

	const unsigned char get_descriptor[8] = {0x80, 0x06, 0x00, 0x02, 0x00, 0x00, 0x12, 0x00};
	
	res = usb_write(TRANS_SETUP, 1, 0, get_descriptor, sizeof(get_descriptor));
	
	if (res != HANDSHAKE_ACK)
	{
		_retries++;
		_state_timer = SOFTUSB_PACKET_PAUSE_MS;
		
		if (_retries > SOFTUSB_RETRIES)
		{
			set_state(su_nodevice);
		}
		
		return;
	}
	
	_data_0 = 1;
	
	_descr_offset = 0;

	set_state(su_read_conf_descr);
	_state_timer = SOFTUSB_PACKET_PAUSE_MS;
}

void SoftUsb::process_read_conf_descr()
{
	int res;
	unsigned char buf[SOFTUSB_BUFFER_SIZE];
	int i;

	res = usb_read(TRANS_IN, 1, 0, buf);

	if (res <= 0 || res == HANDSHAKE_NAK)
	{
		_retries++;
		_state_timer = SOFTUSB_PACKET_PAUSE_MS;
		
		if (_retries > SOFTUSB_RETRIES)
		{
			set_state(su_nodevice);
		}
		
		return;
	}
	
	for (i = 0; i < 8; i++)
	{
		if (i + _descr_offset >= sizeof(_conf_descriptor))
		{
			break;
		}
		
		_conf_descriptor[i + _descr_offset] = buf[i];
	}
	
	_descr_offset += 8;
	
	if (_descr_offset < 18)
	{
		_data_0 = !_data_0;
	}
	else
	{
		set_state(su_work);
		_state_timer = SOFTUSB_PACKET_PAUSE_MS;
	}
}

static int is_in_list(const unsigned char *list, int size, unsigned char value)
{
	int i;
	for (i = 0; i < size; i++)
	{
		if (list[i] == value)
		{
			return 1;
		}
	}
	return 0;
}

void SoftUsb::parse_keyboard_report()
{
	int i, found = 1;
	unsigned char t;
	unsigned char code;
	
	// Read control keys
	_keyb_control = _report[0];
	
	// Don't need right control keys
	if (_keyb_control & 0x08) _keyb_control |= 0x01;
	if (_keyb_control & 0x10) _keyb_control |= 0x02;
	if (_keyb_control & 0x20) _keyb_control |= 0x04;
	
	// Read keys
	for (i = 0; i < HID_MAX_PRESSED_KEYS; i++)
	{
		_keys_pressed[i] = _report[2 + i];
	}
	
	// Bubble-sort keys
	while (found)
	{
		found = 0;
		for (i = 0; i < HID_MAX_PRESSED_KEYS - 1; i++)
		{
			if (_keys_pressed[i] < _keys_pressed[i + 1])
			{
				t = _keys_pressed[i];
				_keys_pressed[i] = _keys_pressed[i + 1];
				_keys_pressed[i + 1] = t;
				found = 1;
			}
		}
	}
	
	// Check released keys
	for (i = 0; i < HID_MAX_PRESSED_KEYS; i++)
	{
		code = _keys_pressed_prev[i];
		if (code == 0)
		{
			break;
		}
		
		if (!is_in_list(_keys_pressed, sizeof(_keys_pressed), code))
		{
			// Key released
			code = xt_codes[code];
			
			if (code > 0)
			{
				add_key(code | 0x80);
			}
		}
	}

	// Check pressed keys
	for (i = 0; i < HID_MAX_PRESSED_KEYS; i++)
	{
		code = _keys_pressed[i];
		if (code == 0)
		{
			break;
		}
		
		if (!is_in_list(_keys_pressed_prev, sizeof(_keys_pressed_prev), code))
		{
			// Key pressed
			code = xt_codes[code];
			
			if (code > 0)
			{
				add_key(code);
			}
		}
	}

	// Save keys state
	for (i = 0; i < HID_MAX_PRESSED_KEYS; i++)
	{
		_keys_pressed_prev[i] = _keys_pressed[i];
	}
}

void SoftUsb::parse_mouse_report()
{
	signed char dx, dy, dw;

	dx = _report[1];
	dy = _report[2];
	dw = _report[3];
	_mouse_b = _report[0];
	_mouse_x += dx;
	_mouse_y += dy;
	_mouse_wheel += dw;
	
	if (_mouse_x < MOUSE_LEFT_LIMIT) _mouse_x = MOUSE_LEFT_LIMIT;
	if (_mouse_x > MOUSE_RIGHT_LIMIT) _mouse_x = MOUSE_RIGHT_LIMIT;
	if (_mouse_y < MOUSE_TOP_LIMIT) _mouse_y = MOUSE_TOP_LIMIT;
	if (_mouse_y > MOUSE_BOTTOM_LIMIT) _mouse_y = MOUSE_BOTTOM_LIMIT;
}

void SoftUsb::process_work()
{
	int res;
	unsigned char buf[SOFTUSB_BUFFER_SIZE];
	int i;
	
	res = usb_read(TRANS_IN, 1, 1, buf);

	if (res > 0 && res <= 12)
	{
		for (i = 0; i < 8; i++)
		{
			_report[i] = buf[i];
		}
		
		switch (get_device_type())
		{
			case USB_DEVICE_KEYBOARD:
				parse_keyboard_report();
				break;
			case USB_DEVICE_MOUSE:
				parse_mouse_report();
				break;
		}
	}
	else if (res == HANDSHAKE_NAK)
	{
	}
	else
	{
		_retries++;
		_state_timer = SOFTUSB_PACKET_PAUSE_MS;
		
		if (_retries > SOFTUSB_RETRIES)
		{
			set_state(su_nodevice);
		}
		
		return;
	}
}
