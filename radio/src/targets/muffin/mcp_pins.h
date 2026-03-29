#ifndef _MCP_PINS_FOR_BOARD_H_
#define _MCP_PINS_FOR_BOARD_H_

#define GPIO_PIN_0 0
#define GPIO_PIN_1 1
#define GPIO_PIN_2 2
#define GPIO_PIN_3 3
#define GPIO_PIN_4 4
#define GPIO_PIN_5 5
#define GPIO_PIN_6 6
#define GPIO_PIN_7 7

#define MCP_PORT_0 0
#define MCP_PORT_1 1
#define MCP_PORT_2 2
#define MCP_PORT_3 3

#define MCP23017_DIR_REG 0x00FFFFFF
#define MCP23017_PULLUP  0x00FFFFFF

#define MCP_PWR_SW_DET (1 << (8 * 1 + 7))  // G0B7

// output pins
#define MCP_PWR_EN (8 * 3 + 6)  // G1B6
#define MCP_5V_EN  (8 * 3 + 5)  // G1B5
#define MCP_INTMOD_5V_EN  (8 * 3 + 2)  // G1B2
#define MCP_INTMOD_BOOT   (8 * 3 + 1)  // G1B1
#define MCP_INTERNAL_PROTO_LED   (8 * 3 + 0)  // G1B0
#define MCP_EXTMOD_5V_EN  (8 * 3 + 4)  // G1B4
#define MCP_EXTMOD_BOOT   (8 * 3 + 3)  // G1B3

#endif // _MCP_PINS_FOR_BOARD_H_