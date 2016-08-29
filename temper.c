#include <stdio.h>
#include <string.h>
#include <usb.h>
#include <errno.h>
#include <math.h>

/*
 * Temper.c by Robert Kavaler (c) 2009 (relavak.com)
 * All rights reserved.
 *
 * Temper driver for linux. This program can be compiled either as a
 * library or as a standalone program (-DUNIT_TEST). The driver will
 * work with some TEMPer usb devices from RDing (www.PCsensor.com).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *     * Redistributions of source code must retain the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY Robert Kavaler ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL Robert kavaler BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 */

#include "temper.h"

#define VENDOR_ID  0x1130
#define PRODUCT_ID 0x660c

struct Temper {
	struct usb_device *device;
	usb_dev_handle *handle;
	int debug;
	int timeout;
	int sensorType;
};

Temper *
TemperCreate(struct usb_device *dev, int timeout, int debug)
{
	Temper *t;
	int ret;

	t = calloc(1, sizeof(*t));
	t->device = dev;
	t->debug = debug;
	t->timeout = timeout;
	t->handle = usb_open(t->device);
	if(!t->handle) {
		free(t);
		return NULL;
	}
	if(t->debug) {
		printf("Trying to detach kernel driver\n");
	}

	ret = usb_detach_kernel_driver_np(t->handle, 0);
	if(ret) {
		if(errno == ENODATA) {
			if(t->debug) {
				printf("Device already detached\n");
			}
		} else {
			if(t->debug) {
				printf("Detach failed: %s[%d]\n",
				       strerror(errno), errno);
				printf("Continuing anyway\n");
			}
		}
	} else {
		if(t->debug) {
			printf("detach successful\n");
		}
	}
	ret = usb_detach_kernel_driver_np(t->handle, 1);
	if(ret) {
		if(errno == ENODATA) {
			if(t->debug)
				printf("Device already detached\n");
		} else {
			if(t->debug) {
				printf("Detach failed: %s[%d]\n",
				       strerror(errno), errno);
				printf("Continuing anyway\n");
			}
		}
	} else {
		if(t->debug) {
			printf("detach successful\n");
		}
	}

	if(usb_set_configuration(t->handle, 1) < 0 ||
	   usb_claim_interface(t->handle, 0) < 0 ||
	   usb_claim_interface(t->handle, 1)) {
		usb_close(t->handle);
		free(t);
		return NULL;
	}
	return t;
}

Temper *
TemperCreateFromDeviceNumber(int deviceNum, int timeout, int debug)
{
	struct usb_bus *bus;
	int n;

	n = 0;
	for(bus=usb_get_busses(); bus; bus=bus->next) {
	    struct usb_device *dev;

	    for(dev=bus->devices; dev; dev=dev->next) {
		if(debug) {
			printf("Found device: %04x:%04x\n",
			       dev->descriptor.idVendor,
			       dev->descriptor.idProduct);
		}
		if(dev->descriptor.idVendor == VENDOR_ID &&
		   dev->descriptor.idProduct == PRODUCT_ID) {
			if(debug) {
			    printf("Found deviceNum %d\n", n);
			}
			if(n == deviceNum) {
				return TemperCreate(dev, timeout, debug);
			}
			n++;
		}
	    }
	}
	return NULL;
}

void
TemperFree(Temper *t)
{
	if(t) {
		if(t->handle) {
			usb_close(t->handle);
		}
		free(t);
	}
}

static int
TemperSendCommand(Temper *t, int a, int b, int c, int d, int e, int f, int g, int h)
{
	unsigned char buf[32];
	int ret;

	bzero(buf, 32);
	buf[0] = a;
	buf[1] = b;
	buf[2] = c;
	buf[3] = d;
	buf[4] = e;
	buf[5] = f;
	buf[6] = g;
	buf[7] = h;

	if(t->debug) {
		printf("sending bytes %02x, %02x, %02x, %02x, %02x, %02x, %02x, %02x\n",
		       a, b, c, d, e, f, g, h);
	}

/*
		/* No interrput out endpoint. Use the Control Endpoint * /
		res = libusb_control_transfer(dev->device_handle,
			LIBUSB_REQUEST_TYPE_CLASS|LIBUSB_RECIPIENT_INTERFACE|LIBUSB_ENDPOINT_OUT,
			0x09/*HID Set_Report* /,
			(2/*HID output* / << 8) | report_number,
			dev->interface,
			(unsigned char *)data, length,
			1000/*timeout millis* /);
*/
	// hidapi: hid_write report 0, extra 0 byte for report_number so 33
	ret = usb_control_msg(t->handle, 0x21, 9, 0x200, 0x01,
			    (char *) buf, 32, t->timeout);
	if(ret != 32) {
		perror("usb_control_msg failed");
		return -1;
	}
	return 0;
}

static int
TemperGetData(Temper *t, char *buf, int len)
{
	int ret;
/*
	res = libusb_control_transfer(dev->device_handle,
		LIBUSB_REQUEST_TYPE_CLASS|LIBUSB_RECIPIENT_INTERFACE|LIBUSB_ENDPOINT_IN,
		0x01/*HID get_report* /,
		(3/*HID feature* / << 8) | report_number,
		dev->interface,
		(unsigned char *)data, length,
		1000/*timeout millis* /);
*/
	// hidapi: hid_get_feature_report 0, 257 (extra byte for report_number
	return usb_control_msg(t->handle, 0xa1, 1, 0x300, 0x01,
			    (char *) buf, len, t->timeout);
}

int
TemperGetTempAndRelHum(Temper *t, double *tempC, double *relhum)
{
	char buf[256];
	int ret, temperature, rh, i;
	double temp_hum;

	TemperSendCommand(t, 10, 11, 12, 13, 0, 0, 2, 0);
	if (t->sensorType == 0x58) {
		TemperSendCommand(t, 0x54, 0, 0, 0, 0, 0, 0, 0);
	}
	else {
	TemperSendCommand(t, 0x48, 0, 0, 0, 0, 0, 0, 0);
	}
	for(i = 0; i < 7; i++) {
		TemperSendCommand(t, 0, 0, 0, 0, 0, 0, 0, 0);
	}
	TemperSendCommand(t, 10, 11, 12, 13, 0, 0, 1, 0);

	/* According to Sensirion datasheet for SHT1x the time for
	8/12/14 bit measurements is 20/80/320 ms. Trial and error
	suggests that sleeping less that 400ms can produce spurious
	measurements */
	usleep(400000); ret = TemperGetData(t, buf, 256); if(ret < 2)
	{ return -1; }

	if(t->debug) {
	  printf("TempAndRelHum (%d bytes):\n", ret);
	  for(i = 0; i < ret; i++) {
	    printf(" %02x", buf[i]);
	    if(i % 16 == 15) {
	      printf("\n");
	    }
	  }
	  printf("\n");
	}

	/* Numerical constants below come from the Sensirion SHT1x
	datasheet (Table 9 for temperature and Table 6 for humidity */
	temperature = (buf[1] & 0xFF) + (buf[0] << 8);
	if (t->sensorType == 0x58) {
		*tempC = temperature * 125.0 / 32000.0; // RKR FM75
	}
	else {
		*tempC = -39.7 + .01*temperature;
	}

	rh = (buf[3] & 0xFF) + ((buf[2] & 0xFF) << 8);
	temp_hum = -2.0468 + 0.0367*rh - 1.5955e-6*rh*rh;
	*relhum = (*tempC-25)*(.01 + .00008 * rh) + temp_hum;

	return 0;
}

int
TemperGetOtherStuff(Temper *t, char *buf, int length)
{
  int i;
	TemperSendCommand(t, 10, 11, 12, 13, 0, 0, 2, 0);
	TemperSendCommand(t, 0x52, 0, 0, 0, 0, 0, 0, 0);
	for(i = 0; i < 7; i++) {
		TemperSendCommand(t, 0, 0, 0, 0, 0, 0, 0, 0);
	}
	TemperSendCommand(t, 10, 11, 12, 13, 0, 0, 1, 0);
	return TemperGetData(t, buf, length);
}

#ifdef UNIT_TEST

#define USB_TIMEOUT 1000	/* milliseconds */

int
main(void)
{
	Temper *t;
	char buf[256];
	int i, ret;

	usb_set_debug(0);
	usb_init();
	usb_find_busses();
	usb_find_devices();

	t = TemperCreateFromDeviceNumber(0, USB_TIMEOUT, 0);
	if(!t) {
		perror("TemperCreate");
		exit(-1);
	}

	bzero(buf, 256);
	ret = TemperGetOtherStuff(t, buf, 256);
	if (ret >= 2) {
		t->sensorType = buf[1];
    }
	if(t->debug) {
	  printf("Other Stuff (%d bytes):\n", ret);
	  for(i = 0; i < ret; i++) {
	    printf(" %02x", buf[i]);
	    if(i % 16 == 15) {
	      printf("\n");
	    }
	  }
	  printf("\n");
	}

	double tempc;
	double rh;

	if(TemperGetTempAndRelHum(t, &tempc, &rh) < 0) {
	  perror("TemperGetTemperatureAndRelHum");
	  exit(1);
	}
	/* printf("Temperature: %.2f°F, %.2f°C\tRelative humidity: %.2f%%\n", (9.0 / 5.0 * tempc + 32.0), tempc, rh); */
	double dew, tn, m;
	if (tempc > 0)
	{
		tn = 243.12;
		m = 17.62;
	}
	else
	{
		tn = 272.62;
		m = 22.46;
	}
	dew = tn * (log(rh/100)+(m*tempc/(tn+tempc))) / (m - log(rh/100) - (m*tempc/(tn+tempc)));

	printf("%.2f %.2f %.2f\n", tempc, rh, dew);

	return 0;
}

#endif
