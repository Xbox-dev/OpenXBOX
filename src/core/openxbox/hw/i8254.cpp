#include "i8254.h"

#include <openxbox/thread.h>

namespace openxbox {

// TODO: Refer to QEMU if we ever need to complete this implementation:
// https://github.com/qemu/qemu/blob/master/hw/timer/i8254_common.c
// https://github.com/qemu/qemu/blob/master/hw/timer/i8254.c


// i8254 timer thread function
static uint32_t i8254ThreadFunc(void *data) {
    i8254 *pit = (i8254 *)data;
    pit->Run();
    return 0;
}

i8254::i8254(Cpu *cpu, float tickRate)
    : m_cpu(cpu)
    , m_tickRate(tickRate)
    , m_running(false)
{
}

void i8254::Reset() {
    m_running = false;
}

void i8254::IORead(uint32_t addr, uint32_t *value, uint16_t size) {
    *value = 0;
}

void i8254::IOWrite(uint32_t addr, uint32_t value, uint16_t size) {
    // HACK: The Xbox always inits the PIT to the same value:
    //   Timer 0, Mode 2, 1ms interrupt interval.
    // Rather than fully implement the PIC, we just wait for the command to
    // start operating, and then simply issue IRQ 0 in a timer thread.
    if (value == 0x34) {
        m_running = true;
        Thread_Create("[HW] i8254", i8254ThreadFunc, this);
    }
}

void i8254::Run() {
    using namespace std::chrono;
    auto nextStop = high_resolution_clock::now();
    auto interval = duration<long long, std::ratio<1, 1000000>>((long long)(1000000.0f / m_tickRate));

    m_running = true;
    while (m_running) {
        m_cpu->Interrupt(0x30);

        nextStop += interval;
        std::this_thread::sleep_until(nextStop);
    }
}

}