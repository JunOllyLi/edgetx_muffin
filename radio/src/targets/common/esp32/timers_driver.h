
#ifndef _TIMERS_DRIVER_H_
#define _TIMERS_DRIVER_H_

tmr10ms_t get_tmr10ms();
uint16_t getTmr2MHz();

#define watchdogSuspend(timeout)

extern "C" volatile tmr10ms_t g_tmr10ms;

void per5ms();
void per10ms();
uint32_t timersGetMsTick();
uint32_t timersGetUsTick();

#endif