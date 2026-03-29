/*!
 * @file Adafruit_MCP23XXX.h
 */

#ifndef __ADAFRUIT_MCP23XXX_H__
#define __ADAFRUIT_MCP23XXX_H__

// registers
#define MCP23XXX_IODIR 0x00   //!< I/O direction register
#define MCP23XXX_IPOL 0x01    //!< Input polarity register
#define MCP23XXX_GPINTEN 0x02 //!< Interrupt-on-change control register
#define MCP23XXX_DEFVAL                                                        \
  0x03 //!< Default compare register for interrupt-on-change
#define MCP23XXX_INTCON 0x04 //!< Interrupt control register
#define MCP23XXX_IOCON 0x05  //!< Configuration register
#define MCP23XXX_GPPU 0x06   //!< Pull-up resistor configuration register
#define MCP23XXX_INTF 0x07   //!< Interrupt flag register
#define MCP23XXX_INTCAP 0x08 //!< Interrupt capture register
#define MCP23XXX_GPIO 0x09   //!< Port register
#define MCP23XXX_OLAT 0x0A   //!< Output latch register

#define MCP23XXX_ADDR 0x20 //!< Default I2C Address

#define MCP23XXX_INT_ERR 255 //!< Interrupt error

#define MCP_PIN(port, pin) ((port << 3) | pin)
#define MCP_PORT(pin) (pin >> 3)
#define MCP_HANDLE(port) mcp[(port & 0x02) >> 1]
#define MCP_REG_ADDR(baseAddr, port) ((baseAddr << 1) | (port & 0x01))
#define MCP_HANDLE_FOR_PIN(pin) mcp[(pin >> 4) & 0x01]
#define MCP_REG_FOR_PIN(baseAddr, pin) ((baseAddr << 1) | ((pin >> 3) & 0x01))


#endif