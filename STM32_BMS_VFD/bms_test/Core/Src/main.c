/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Daly BMS RS485 - 2 BMS, 2 ayri UART testi (Nucleo-F401RE)
  *
  *  USART1 (PA9=TX,  PA10=RX)  -> Modul#1 (izole TTL<->RS485) -> BMS#1
  *  USART6 (PA11=TX, PA12=RX)  -> Modul#2 (izole TTL<->RS485) -> BMS#2
  *  USART2 (PA2/PA3, 115200)   -> ST-Link sanal COM portu (PC terminali)
  *
  *  Her iki hat icin de adres taramasi yapiliyor (0x40, 0x01..0x07) --
  *  hangi BMS hangi adreste cevap veriyor gorelim.
  ******************************************************************************
  */
/* USER CODE END Header */
#include "main.h"
#include "modbus_vfd.h"
#include "daly_bms.h"
#include "jetson_link.h"
#include <stdio.h>
#include <string.h>

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;   /* BMS#1 / RS485 hatti     */
UART_HandleTypeDef huart6;   /* BMS#2 / RS485 hatti     */
UART_HandleTypeDef huart2;   /* PC debug (ST-Link VCP)  */

/* USER CODE BEGIN PV */
static char dbg[160];
static char jsonBuf[512];

/* Taranacak aday adresler: coklu-BMS Daly adreslemesi 0x40 tabanli ofsetli --
 * BoardNo=1 -> 0x40, BoardNo=2 -> 0x41, ... BoardNo=8 -> 0x47.
 * Dashboard'un "x" indexiyle birebir eslesecek sekilde x = addr - 0x40. */
static const uint8_t candidates[] = { 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47 };
#define NUM_CANDIDATES (sizeof(candidates)/sizeof(candidates[0]))
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART6_UART_Init(void);

/* USER CODE BEGIN PFP */
static void DbgSend(const char *s)
{
  HAL_UART_Transmit(&huart2, (uint8_t*)s, strlen(s), 200);
}

/* Bir BMS'in verisini, Spectraloop dashboard'unun (index.html::updBms)
 * bekledigi JSON semasina gore satir olarak USART2/Jetson hattina yazar:
 *   {"x":idx,"k":ok,"n":"BMSn","v":V,"s":SOC,"i":A,"t":C,"c":[hucre,...]}
 * ok=0 ise sadece x/k/n gonderilir (dashboard karti "offline" gosterir). */
static void SendBmsJson(uint8_t addr, const Daly_Data_t *d)
{
  int idx = addr - 0x40;
  int n = sprintf(jsonBuf, "{\"x\":%d,\"k\":%d,\"n\":\"BMS%d\"", idx, d->ok ? 1 : 0, idx + 1);

  if (d->ok)
  {
    n += sprintf(jsonBuf + n, ",\"v\":%.1f,\"s\":%.1f,\"i\":%.1f,\"t\":%d,\"c\":[",
                 d->voltage, d->soc, d->current, (int)d->temperature);
    for (uint8_t i = 0; i < d->cellCount; i++)
      n += sprintf(jsonBuf + n, "%s%.3f", i ? "," : "", d->cells[i]);
    n += sprintf(jsonBuf + n, "]");
  }
  n += sprintf(jsonBuf + n, "}\r\n");

  DbgSend(jsonBuf);
}

/* Tum aday adresleri tarar; her biri icin Daly_ReadAll cagirip JSON yollar. */
static void ScanBus(void)
{
  sprintf(dbg, "-- BMS bus taraniyor --\r\n");
  DbgSend(dbg);

  for (uint8_t i = 0; i < NUM_CANDIDATES; i++)
  {
    uint8_t addr = candidates[i];
    Daly_Data_t d;
    Daly_ReadAll(addr, &d);
    SendBmsJson(addr, &d);
    /* Her adresten sonra Jetson'dan gelen komutu kontrol et -- BMS taramasi
     * (bos adreslerin timeout'lari yuzunden) saniyeler surebiliyor; MOTOR_STOP
     * gibi guvenlik-kritik bir komutun tum tarama bitene kadar beklememesi
     * icin buraya serpistirildi (bkz. jetson_link.h). Ayni sebeple
     * VFD_MotorKeepAlive() de buraya serpistirildi -- motor calisirken VFD'nin
     * kendi haberlesme-kaybi guvenlik zaman asimina (~1-2s) yakalanmamasi icin. */
    JetsonLink_Poll();
    VFD_MotorKeepAlive();
    HAL_Delay(50);
  }
}


/* VFD (Delta MS300, Modbus RTU, USART6) icin SADECE OKUMA testi --
 * motoru kimse gozetmiyorken kazayla calistirmamak icin bilerek
 * VFD_MotorGo() / Modbus_WriteRegister() burada CAGRILMIYOR. */
static void ScanVFD(void)
{
  sprintf(dbg, "-- [VFD/USART6] Modbus RTU okuma testi (slave=%d) --\r\n", VFD_SLAVE_ID);
  DbgSend(dbg);

  for (uint8_t i = 0; i < VFD_PARAMS_COUNT; i++)
  {
    const VFD_Param_t *p = &VFD_PARAMS[i];
    uint16_t raw;
    if (Modbus_ReadRegister(p->addr, &raw))
    {
      sprintf(dbg, "[VFD] %s (0x%04X) %-28s ham=%u  deger=%.2f\r\n",
              p->code, p->addr, p->name, raw, raw * p->scale);
      DbgSend(dbg);
    }
    else
    {
      sprintf(dbg, "[VFD] %s (0x%04X) %-28s -> cevap yok\r\n", p->code, p->addr, p->name);
      DbgSend(dbg);
    }
    JetsonLink_Poll();
    VFD_MotorKeepAlive();
    HAL_Delay(100);
  }
}
/* USER CODE END PFP */

/**
  * @brief  The application entry point.
  */
int main(void)
{
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_USART6_UART_Init();

  /* USER CODE BEGIN 2 */
  HAL_Delay(300);
  Daly_Init(&huart1);
  Modbus_Init(&huart6);
  JetsonLink_Init(&huart2);
  DbgSend("\r\n==== BMS bus (USART1, JSON) + VFD Modbus RTU okuma testi (USART6) + Jetson komut hatti (USART2, IT) ====\r\n");
  /* USER CODE END 2 */

  /* USER CODE BEGIN WHILE */
  while (1)
  {
    ScanBus();
    ScanVFD();
    DbgSend("---- tur tamamlandi, 2 sn sonra tekrar ----\r\n\r\n");
    /* Duz HAL_Delay(2000) yerine: bu bekleme boyunca da Jetson komutlarini
     * (MOTOR_STOP dahil) ve VFD keepalive'i 100ms araliklarla islemeye devam et. */
    for (uint8_t i = 0; i < 20; i++)
    {
      JetsonLink_Poll();
      VFD_MotorKeepAlive();
      HAL_Delay(100);
    }
  }
  /* USER CODE END WHILE */
}

/**
  * @brief System Clock Configuration (HSI -> PLL -> 84MHz)
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 84;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) Error_Handler();
}

/**
  * @brief USART1 Initialization Function (BMS#1 / RS485 hatti)
  */
static void MX_USART1_UART_Init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 9600;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK) Error_Handler();
}

/**
  * @brief USART6 Initialization Function (BMS#2 / RS485 hatti)
  */
static void MX_USART6_UART_Init(void)
{
  huart6.Instance = USART6;
  huart6.Init.BaudRate = 9600;
  huart6.Init.WordLength = UART_WORDLENGTH_8B;
  huart6.Init.StopBits = UART_STOPBITS_1;
  huart6.Init.Parity = UART_PARITY_NONE;
  huart6.Init.Mode = UART_MODE_TX_RX;
  huart6.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart6.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart6) != HAL_OK) Error_Handler();
}

/**
  * @brief USART2 Initialization Function (PC debug / ST-Link VCP)
  */
static void MX_USART2_UART_Init(void)
{
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK) Error_Handler();
}

/**
  * @brief GPIO Initialization Function
  */
static void MX_GPIO_Init(void)
{
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
}

/**
  * @brief  This function is executed in case of error occurrence.
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif /* USE_FULL_ASSERT */
