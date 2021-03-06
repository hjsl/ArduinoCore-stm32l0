/*
 * Copyright (c) 2017-2018 Thomas Roell.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal with the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimers.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimers in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of Thomas Roell, nor the names of its contributors
 *     may be used to endorse or promote products derived from this Software
 *     without specific prior written permission.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * WITH THE SOFTWARE.
 */

#include "stm32l0_exti.h"
#include "stm32l0_gpio.h"
#include "stm32l0_system.h"

extern void EXTI0_1_IRQHandler(void);
extern void EXTI2_3_IRQHandler(void);
extern void EXTI4_15_IRQHandler(void);

extern void SPI1_IRQHandler(void);
extern void SPI2_IRQHandler(void);

typedef struct _stm32l0_exti_device_t {
    uint16_t                events;
    uint16_t                mask;
    volatile uint16_t       pending;
    volatile uint16_t       priority[4];
    stm32l0_exti_callback_t callback[16];
    void                    *context[16];
} stm32l0_exti_device_t;

static stm32l0_exti_device_t stm32l0_exti_device;

void __stm32l0_exti_initialize(void)
{
    /* set default IMR/RTSR/FTSR
     *
     * 16 * PVD
     * 17 * RTC alarm
     * 19 * RTC tamper/timestamp, CCS_LSE
     * 20 * RTC wakeup timer
     * 29 * LPTIM1
     *
     * I2C needs to be done in driver (enable)
     * USART/LPUART needs to be done in driver (enable)
     * COMP1/COMP2 needs to be done in driver (enable, polarity)
     * USB needs to be done in driver (enable)
     * GPIO needs to be done in driver (enable, polarity)
     *
     * Use direct ones as default ? (I2C/USART/LPUART remove ?)
     */
    
    stm32l0_exti_device.events = 0;
    stm32l0_exti_device.mask = ~0;
    stm32l0_exti_device.pending = 0;
    stm32l0_exti_device.priority[0] = 0;
    stm32l0_exti_device.priority[1] = 0;
    stm32l0_exti_device.priority[2] = 0;
    stm32l0_exti_device.priority[3] = 0;

    NVIC_SetPriority(EXTI0_1_IRQn, ARMV6M_IRQ_PRIORITY_CRITICAL);
    NVIC_SetPriority(EXTI2_3_IRQn, ARMV6M_IRQ_PRIORITY_CRITICAL);
    NVIC_SetPriority(EXTI4_15_IRQn, ARMV6M_IRQ_PRIORITY_CRITICAL);
    NVIC_SetPriority(SPI1_IRQn, ARMV6M_IRQ_PRIORITY_HIGH);
    NVIC_SetPriority(SPI2_IRQn, ARMV6M_IRQ_PRIORITY_MEDIUM);

    NVIC_EnableIRQ(EXTI0_1_IRQn);
    NVIC_EnableIRQ(EXTI2_3_IRQn);
    NVIC_EnableIRQ(EXTI4_15_IRQn);
    NVIC_EnableIRQ(SPI1_IRQn);
    NVIC_EnableIRQ(SPI2_IRQn);
}

bool stm32l0_exti_attach(uint16_t pin, uint32_t control, stm32l0_exti_callback_t callback, void *context)
{
    unsigned int mask, index, group;

    index = (pin & STM32L0_GPIO_PIN_INDEX_MASK) >> STM32L0_GPIO_PIN_INDEX_SHIFT;
    group = (pin & STM32L0_GPIO_PIN_GROUP_MASK) >> STM32L0_GPIO_PIN_GROUP_SHIFT;

    mask = 1ul << index;

    if (stm32l0_exti_device.events & mask)
    {
        return false;
    }

    stm32l0_exti_device.callback[index] = callback;
    stm32l0_exti_device.context[index] = context;

    armv6m_atomic_modify(&SYSCFG->EXTICR[index >> 2], (0x0000000f << ((index & 3) << 2)), (group << ((index & 3) << 2)));

    if (control & STM32L0_EXTI_CONTROL_EDGE_RISING)
    {
        armv6m_atomic_or(&EXTI->RTSR, mask);
    }
    else
    {
        armv6m_atomic_and(&EXTI->RTSR, ~mask);
    }
    
    if (control & STM32L0_EXTI_CONTROL_EDGE_FALLING)
    {
        armv6m_atomic_or(&EXTI->FTSR, mask);
    }
    else
    {
        armv6m_atomic_and(&EXTI->FTSR, ~mask);
    }

    armv6m_atomic_andh(&stm32l0_exti_device.priority[0], ~mask);
    armv6m_atomic_andh(&stm32l0_exti_device.priority[1], ~mask);
    armv6m_atomic_andh(&stm32l0_exti_device.priority[2], ~mask);
    armv6m_atomic_andh(&stm32l0_exti_device.priority[3], ~mask);
    armv6m_atomic_orh(&stm32l0_exti_device.priority[(control & STM32L0_EXTI_CONTROL_PRIORITY_MASK) >> STM32L0_EXTI_CONTROL_PRIORITY_SHIFT], mask);
    armv6m_atomic_orh(&stm32l0_exti_device.events, mask);

    armv6m_atomic_or(&EXTI->IMR, (stm32l0_exti_device.events & stm32l0_exti_device.mask));

    return true;
}

void stm32l0_exti_detach(uint16_t pin)
{
    unsigned int mask, index;

    index = (pin & STM32L0_GPIO_PIN_INDEX_MASK) >> STM32L0_GPIO_PIN_INDEX_SHIFT;

    mask = 1ul << index;

    armv6m_atomic_and(&EXTI->IMR, ~mask);
    armv6m_atomic_andh(&stm32l0_exti_device.events, ~mask);
}

bool stm32l0_exti_control(uint16_t pin, uint32_t control)
{
    unsigned int mask, index;

    index = (pin & STM32L0_GPIO_PIN_INDEX_MASK) >> STM32L0_GPIO_PIN_INDEX_SHIFT;

    mask = 1ul << index;

    if (!(stm32l0_exti_device.events & mask))
    {
        return false;
    }

    armv6m_atomic_and(&EXTI->IMR, ~mask);

    armv6m_atomic_andh(&stm32l0_exti_device.priority[0], ~mask);
    armv6m_atomic_andh(&stm32l0_exti_device.priority[1], ~mask);
    armv6m_atomic_andh(&stm32l0_exti_device.priority[2], ~mask);
    armv6m_atomic_andh(&stm32l0_exti_device.priority[3], ~mask);
    armv6m_atomic_andh(&stm32l0_exti_device.events, ~mask);

    return true;
}

void stm32l0_exti_block(uint32_t mask)
{
    mask &= stm32l0_exti_device.events;

    if (mask)
    {
        armv6m_atomic_and(&EXTI->IMR, ~mask);
    }

    armv6m_atomic_andh(&stm32l0_exti_device.mask, ~mask);
}

void stm32l0_exti_unblock(uint32_t mask)
{
    mask &= stm32l0_exti_device.events;

    armv6m_atomic_orh(&stm32l0_exti_device.mask, mask);

    if (mask)
    {
        armv6m_atomic_or(&EXTI->IMR, (mask & 0x0000ffff));
    }
}

void EXTI0_1_IRQHandler(void)
{
    uint32_t mask, pending;

    mask = (EXTI->PR & stm32l0_exti_device.mask) & 0x0003;

    EXTI->PR = mask;

    if (stm32l0_exti_device.priority[0] & mask)
    {
        pending = 0x0000;
          
        if (mask & 0x0001)
        {
            if (stm32l0_exti_device.priority[0] & 0x0001)
            {
                (*stm32l0_exti_device.callback[0])(stm32l0_exti_device.context[0]);
            }
            else
            {
                pending |= 0x0001;
            }
        }

        if (mask & 0x0002)
        {
            if (stm32l0_exti_device.priority[0] & 0x0002)
            {
                (*stm32l0_exti_device.callback[1])(stm32l0_exti_device.context[1]);
            }
            else
            {
                pending |= 0x0002;
            }
        }
    }
    else
    {
        pending = mask;
    }
    
    if (pending)
    {
        __armv6m_atomic_orh(&stm32l0_exti_device.pending, pending);
        
        if (stm32l0_exti_device.priority[1] & pending)
        {
            NVIC_SetPendingIRQ(SPI1_IRQn);
        }
        else
        {
            if (stm32l0_exti_device.priority[2] & pending)
            {
                NVIC_SetPendingIRQ(SPI2_IRQn);
            }
            else
            {
                armv6m_pendsv_raise(ARMV6M_PENDSV_SWI_EXTI);
            }
        }
    }
}

void EXTI2_3_IRQHandler(void)
{
    uint32_t mask, pending;

    mask = (EXTI->PR & stm32l0_exti_device.mask) & 0x000c;

    EXTI->PR = mask;
    
    if (stm32l0_exti_device.priority[0] & mask)
    {
        pending = 0x0000;

        if (mask & 0x0004)
        {
            if (stm32l0_exti_device.priority[0] & 0x0004)
            {
                (*stm32l0_exti_device.callback[2])(stm32l0_exti_device.context[2]);
            }
            else
            {
                pending |= 0x0004;
            }
        }

        if (mask & 0x0008)
        {
            if (stm32l0_exti_device.priority[0] & 0x0008)
            {
                (*stm32l0_exti_device.callback[3])(stm32l0_exti_device.context[3]);
            }
            else
            {
                pending |= 0x0002;
            }
        }
    }
    else
    {
        pending = mask;
    }

    if (pending)
    {
        __armv6m_atomic_orh(&stm32l0_exti_device.pending, pending);
        
        if (stm32l0_exti_device.priority[1] & pending)
        {
            NVIC_SetPendingIRQ(SPI1_IRQn);
        }
        else
        {
            if (stm32l0_exti_device.priority[2] & pending)
            {
                NVIC_SetPendingIRQ(SPI2_IRQn);
            }
            else
            {
                armv6m_pendsv_raise(ARMV6M_PENDSV_SWI_EXTI);
            }
        }
    }
}

void EXTI4_15_IRQHandler(void)
{
    uint32_t mask, pending, bit, index;

    mask = (EXTI->PR & stm32l0_exti_device.mask) & 0xfff0;

    EXTI->PR = mask;

    if (stm32l0_exti_device.priority[0] & mask)
    {
        pending = 0x0000;

        for (bit = 0x0010, index = 4; index < 16; bit <<= 1, index++)
        {
            if (mask & bit)
            {
                if (stm32l0_exti_device.priority[0] & bit)
                {
                    (*stm32l0_exti_device.callback[index])(stm32l0_exti_device.context[index]);
                }
                else
                {
                    pending |= bit;
                }
            }
        }
    }
    else
    {
        pending = mask;
    }
    
    if (pending)
    {
        __armv6m_atomic_orh(&stm32l0_exti_device.pending, pending);
        
        if (stm32l0_exti_device.priority[1] & pending)
        {
            NVIC_SetPendingIRQ(SPI1_IRQn);
        }
        else
        {
            if (stm32l0_exti_device.priority[2] & pending)
            {
                NVIC_SetPendingIRQ(SPI2_IRQn);
            }
            else
            {
                armv6m_pendsv_raise(ARMV6M_PENDSV_SWI_EXTI);
            }
        }
    }
}

void SPI1_IRQHandler(void)
{
    uint32_t mask, bit, index;

    NVIC_ClearPendingIRQ(SPI1_IRQn);

    mask = (stm32l0_exti_device.pending & stm32l0_exti_device.mask) & stm32l0_exti_device.priority[1];

    armv6m_atomic_andh(&stm32l0_exti_device.pending, ~mask);

    for (bit = 0x0010, index = 4; index < 16; bit <<= 1, index++)
    {
        if (mask & bit)
        {
            (*stm32l0_exti_device.callback[index])(stm32l0_exti_device.context[index]);
        }
    }
}

void SPI2_IRQHandler(void)
{
    uint32_t mask, bit, index;

    NVIC_ClearPendingIRQ(SPI2_IRQn);

    mask = (stm32l0_exti_device.pending & stm32l0_exti_device.mask) & stm32l0_exti_device.priority[2];

    armv6m_atomic_andh(&stm32l0_exti_device.pending, ~mask);

    for (bit = 0x0010, index = 4; index < 16; bit <<= 1, index++)
    {
        if (mask & bit)
        {
            (*stm32l0_exti_device.callback[index])(stm32l0_exti_device.context[index]);
        }
    }
}

void SWI_EXTI_IRQHandler(void)
{
    uint32_t mask, bit, index;

    mask = (stm32l0_exti_device.pending & stm32l0_exti_device.mask) & stm32l0_exti_device.priority[3];

    armv6m_atomic_andh(&stm32l0_exti_device.pending, ~mask);

    for (bit = 0x0010, index = 4; index < 16; bit <<= 1, index++)
    {
        if (mask & bit)
        {
            (*stm32l0_exti_device.callback[index])(stm32l0_exti_device.context[index]);
        }
    }
}
