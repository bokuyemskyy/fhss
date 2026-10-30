/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.h
 * @brief          : Header for main.c file.
 *                   This file contains the common defines of the application.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
  typedef enum
  {
    COLOR_MODE_RGB565,
    COLOR_MODE_RGB332
  } ColorMode_t;
/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define B1_Pin GPIO_PIN_13
#define B1_GPIO_Port GPIOC
#define NRF24L01_CE_Pin GPIO_PIN_0
#define NRF24L01_CE_GPIO_Port GPIOA
#define NRF24L01_CSN_Pin GPIO_PIN_1
#define NRF24L01_CSN_GPIO_Port GPIOA
#define USART_TX_Pin GPIO_PIN_2
#define USART_TX_GPIO_Port GPIOA
#define USART_RX_Pin GPIO_PIN_3
#define USART_RX_GPIO_Port GPIOA
#define NRF24L01_IRQ_Pin GPIO_PIN_4
#define NRF24L01_IRQ_GPIO_Port GPIOA
#define NRF24L01_IRQ_EXTI_IRQn EXTI4_IRQn
#define TFT_DC_Pin GPIO_PIN_10
#define TFT_DC_GPIO_Port GPIOA
#define TMS_Pin GPIO_PIN_13
#define TMS_GPIO_Port GPIOA
#define TCK_Pin GPIO_PIN_14
#define TCK_GPIO_Port GPIOA
#define SWO_Pin GPIO_PIN_3
#define SWO_GPIO_Port GPIOB
#define TFT_CS_Pin GPIO_PIN_4
#define TFT_CS_GPIO_Port GPIOB
#define TFT_RST_Pin GPIO_PIN_5
#define TFT_RST_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */
#define FRAME_CHUNK_SIZE 4096
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
