// Vector table and reset handler.
#include <stdint.h>

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern int main(void);

#define VTOR (*(volatile uint32_t *)0xE000ED08u)
#define APP_BASE 0x08004000u

void Reset_Handler(void);
void Default_Handler(void) { for (;;) { } }

#define WEAK_HANDLER(name) void name(void) __attribute__((weak, alias("Default_Handler")))
WEAK_HANDLER(NMI_Handler);
WEAK_HANDLER(HardFault_Handler);
WEAK_HANDLER(SVC_Handler);
WEAK_HANDLER(PendSV_Handler);
WEAK_HANDLER(SysTick_Handler);
WEAK_HANDLER(USBFS_IRQHandler);

typedef void (*isr_t)(void);

// 16 core entries + 68 interrupts (USBFS is IRQ 67). No interrupt is ever enabled, USB is polled.
__attribute__((section(".isr_vector"), used))
const isr_t vectors[16 + 68] = {
    (isr_t)&_estack, Reset_Handler, NMI_Handler, HardFault_Handler,
    0, 0, 0, 0, 0, 0, 0, SVC_Handler, 0, 0, PendSV_Handler, SysTick_Handler,
    [16 + 67] = USBFS_IRQHandler,
};

void Reset_Handler(void)
{
    // The bootloader may have left interrupts enabled or pending.
    for (int i = 0; i < 3; i++) {
        *(volatile uint32_t *)(0xE000E180u + 4u * i) = 0xFFFFFFFFu;   // clear enable
        *(volatile uint32_t *)(0xE000E280u + 4u * i) = 0xFFFFFFFFu;   // clear pending
    }
    *(volatile uint32_t *)0xE000E010u = 0;                              // SysTick off
    VTOR = APP_BASE;
    for (uint32_t *s = &_sidata, *d = &_sdata; d < &_edata;)
        *d++ = *s++;
    for (uint32_t *d = &_sbss; d < &_ebss;)
        *d++ = 0;
    main();
    for (;;) { }
}
