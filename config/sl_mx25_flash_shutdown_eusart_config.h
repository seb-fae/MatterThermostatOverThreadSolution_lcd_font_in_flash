/***************************************************************************//**
 * @file
 * @brief SL_MX25_FLASH_SHUTDOWN_USART Config
 *******************************************************************************
 * # License
 * <b>Copyright 2020 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/

#ifndef SL_MX25_FLASH_SHUTDOWN_CONFIG_H
#define SL_MX25_FLASH_SHUTDOWN_CONFIG_H

// <<< sl:start pin_tool >>>
// {eusart signal=TX,RX,SCLK} SL_MX25_FLASH_SHUTDOWN
// [EUSART_SL_MX25_FLASH_SHUTDOWN]
#define SL_MX25_FLASH_SHUTDOWN_PERIPHERAL        EUSART1
#define SL_MX25_FLASH_SHUTDOWN_PERIPHERAL_NO     1

// EUSART1 TX on PC01
#define SL_MX25_FLASH_SHUTDOWN_TX_PORT           SL_GPIO_PORT_C
#define SL_MX25_FLASH_SHUTDOWN_TX_PIN            1

// EUSART1 RX on PC02
#define SL_MX25_FLASH_SHUTDOWN_RX_PORT           SL_GPIO_PORT_C
#define SL_MX25_FLASH_SHUTDOWN_RX_PIN            2

// EUSART1 SCLK on PC03
#define SL_MX25_FLASH_SHUTDOWN_SCLK_PORT         SL_GPIO_PORT_C
#define SL_MX25_FLASH_SHUTDOWN_SCLK_PIN          3

// [EUSART_SL_MX25_FLASH_SHUTDOWN]

// <gpio> SL_MX25_FLASH_SHUTDOWN_CS

// $[GPIO_SL_MX25_FLASH_SHUTDOWN_CS]
#ifndef SL_MX25_FLASH_SHUTDOWN_CS_PORT          
#define SL_MX25_FLASH_SHUTDOWN_CS_PORT           SL_GPIO_PORT_C
#endif
#ifndef SL_MX25_FLASH_SHUTDOWN_CS_PIN           
#define SL_MX25_FLASH_SHUTDOWN_CS_PIN            4
#endif
// [GPIO_SL_MX25_FLASH_SHUTDOWN_CS]$

// <<< sl:end pin_tool >>>

#endif // SL_MX25_FLASH_SHUTDOWN_CONFIG_H
