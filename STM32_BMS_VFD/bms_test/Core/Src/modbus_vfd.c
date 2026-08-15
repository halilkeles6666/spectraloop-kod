#include "modbus_vfd.h"
#include <string.h>

static UART_HandleTypeDef *s_huart;
static uint8_t s_motorDirForward = 1;
static uint8_t s_motorRunning = 0;

/* vfd_modbus.py::VFD_PARAMS ile birebir ayni tablo, + 00-20/00-21 eklendi
 * (bkz. asagidaki not -- orijinal Pi tablosunda yoktu, VFD'nin Modbus
 * komutlarini kabul etmesi icin kesinlikle ayarlanmasi gerekiyor). */
const VFD_Param_t VFD_PARAMS[] = {
  {"00-11", 0x000B, "Frekans Komut Kaynagi",          1.0f},
  /* 00-20/00-21: Delta MS300 icin frekans/calistirma komut kaynagini
   * Modbus-RTU'ya cevirir -- bunlar dogru degerde (00-20=1, 00-21=2)
   * olmadan VFD, Modbus'tan gelen MOTOR_GO/MOTOR_DIR_* yazma isteklerini
   * SESSIZCE yoksayar (yazma Modbus seviyesinde basarili doner ama motor
   * fiilen calismaz) -- bkz. proje gecmisi 2026-08-12. */
  {"00-20", 0x0014, "Frekans Komut Kaynagi (RS485)",   1.0f},
  {"00-21", 0x0015, "Calistirma Komut Kaynagi (RS485)",1.0f},
  {"00-22", 0x0016, "Frekans EEPROM Kayit",           1.0f},
  {"00-23", 0x0017, "Son Frekansla Ac",                1.0f},
  {"01-00", 0x0100, "Maks Cikis Frekansi",             0.01f},
  {"01-01", 0x0101, "Baz Frekans",                     0.01f},
  {"01-02", 0x0102, "Maks Cikis Gerilimi",             0.1f},
  {"01-03", 0x0103, "Orta Nokta Frekansi 1",            0.01f},
  {"01-04", 0x0104, "Orta Nokta Gerilimi 1",            0.1f},
  {"01-05", 0x0105, "Min Cikis Frekansi",               0.01f},
  {"01-06", 0x0106, "Min Cikis Gerilimi",               0.1f},
  {"01-08", 0x0108, "Min Frekans Gerilimi",             0.1f},
  {"01-10", 0x010A, "Ust Sinir Frekansi",               0.01f},
  {"01-12", 0x010C, "Hizlanma Suresi",                  0.01f},
  {"01-13", 0x010D, "Yavaslama Suresi",                 0.01f},
  {"06-03", 0x0603, "Hizlanmada Asiri Akim Onleme",     1.0f},
  {"06-04", 0x0604, "Sabit Hizda Asiri Akim Onleme",    1.0f},
  /* Pr. degil, Delta'nin 2000H haberlesme blogundaki DURUM (okunur)
   * registerlari -- MOTOR_GO/STOP'un fiilen etkili olup olmadigini
   * gozle degil, VFD'nin kendi bildirdigi anlik cikis degerinden objektif
   * olarak dogrulamak icin eklendi (2026-08-12 teshis). */
  {"20-01", 0x2101, "Surucu Durum Sozcugu",              1.0f},
  {"20-03", 0x2103, "Cikis Frekansi (anlik)",            0.01f},
};
const uint8_t VFD_PARAMS_COUNT = sizeof(VFD_PARAMS) / sizeof(VFD_PARAMS[0]);

void Modbus_Init(UART_HandleTypeDef *huart)
{
  s_huart = huart;
}

/* Standart Modbus RTU CRC16 (vfd_modbus.py::modbus_crc16 ile ayni algoritma). */
static uint16_t ModbusCRC16(const uint8_t *data, uint8_t len)
{
  uint16_t crc = 0xFFFF;
  for (uint8_t i = 0; i < len; i++)
  {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++)
    {
      if (crc & 1) crc = (crc >> 1) ^ 0xA001;
      else         crc >>= 1;
    }
  }
  return crc;
}

/* Bkz. daly_bms.c::ClearUartErrors -- ayni RS485 yarim-duplex yankisi
 * riski burada da gecerli, transmit sonrasi eski hata bayraklarini
 * temizlemeden receive'e girmek cevabi kacirabilir. */
static void ClearUartErrors(void)
{
  __HAL_UART_CLEAR_OREFLAG(s_huart);
  __HAL_UART_CLEAR_FEFLAG(s_huart);
  __HAL_UART_CLEAR_NEFLAG(s_huart);
  __HAL_UART_CLEAR_PEFLAG(s_huart);
}

/* Bkz. daly_bms.c::FlushStaleRx -- ayni sebep, ayni cozum. */
static void FlushStaleRx(void)
{
  ClearUartErrors();
  while (__HAL_UART_GET_FLAG(s_huart, UART_FLAG_RXNE))
  {
    volatile uint32_t dummy = s_huart->Instance->DR;
    (void)dummy;
    ClearUartErrors();
  }
}

/* Bkz. daly_bms.c::ReceiveFrameRaw -- HAL_UART_Receive'in bu hatta
 * cevabi kacirdigi ciplak teshisle kanitlandi, dogrudan RXNE polling'e
 * gecildi. */
static uint8_t ReceiveFrameRaw(uint8_t *buf, uint8_t len, uint32_t timeout_ms)
{
  uint8_t count = 0;
  uint32_t start = HAL_GetTick();
  while (count < len && (HAL_GetTick() - start) < timeout_ms)
  {
    if (__HAL_UART_GET_FLAG(s_huart, UART_FLAG_RXNE))
    {
      buf[count++] = (uint8_t)(s_huart->Instance->DR & 0xFF);
    }
  }
  return count == len;
}

uint8_t Modbus_ReadRegister(uint16_t addr, uint16_t *outValue)
{
  uint8_t frame[8];
  frame[0] = VFD_SLAVE_ID;
  frame[1] = 0x03;
  frame[2] = (uint8_t)(addr >> 8);
  frame[3] = (uint8_t)(addr & 0xFF);
  frame[4] = 0x00;
  frame[5] = 0x01;                       /* 1 register oku */
  uint16_t crc = ModbusCRC16(frame, 6);
  frame[6] = (uint8_t)(crc & 0xFF);
  frame[7] = (uint8_t)(crc >> 8);

  ClearUartErrors();
  HAL_UART_Transmit(s_huart, frame, 8, 100);
  FlushStaleRx();

  uint8_t resp[7];
  memset(resp, 0, sizeof(resp));
  if (!ReceiveFrameRaw(resp, 7, 200)) return 0;
  if (resp[1] & 0x80) return 0;                        /* exception yaniti */

  uint16_t rcrc = ModbusCRC16(resp, 5);
  if ((uint8_t)(rcrc & 0xFF) != resp[5] || (uint8_t)(rcrc >> 8) != resp[6]) return 0;

  *outValue = ((uint16_t)resp[3] << 8) | resp[4];
  return 1;
}

uint8_t Modbus_WriteRegister(uint16_t addr, uint16_t value)
{
  uint8_t frame[8];
  frame[0] = VFD_SLAVE_ID;
  frame[1] = 0x06;
  frame[2] = (uint8_t)(addr >> 8);
  frame[3] = (uint8_t)(addr & 0xFF);
  frame[4] = (uint8_t)(value >> 8);
  frame[5] = (uint8_t)(value & 0xFF);
  uint16_t crc = ModbusCRC16(frame, 6);
  frame[6] = (uint8_t)(crc & 0xFF);
  frame[7] = (uint8_t)(crc >> 8);

  ClearUartErrors();
  HAL_UART_Transmit(s_huart, frame, 8, 100);
  FlushStaleRx();

  uint8_t resp[8];
  memset(resp, 0, sizeof(resp));
  if (!ReceiveFrameRaw(resp, 8, 200)) return 0;
  if (resp[1] & 0x80) return 0;
  return 1;
}

/* vfd_modbus.py::_handle_motor ile birebir ayni kontrol degerleri. */
uint8_t VFD_MotorDirForward(void)
{
  s_motorDirForward = 1;
  return Modbus_WriteRegister(VFD_MOTOR_CTRL_ADDR, 0x0010);
}

uint8_t VFD_MotorDirReverse(void)
{
  s_motorDirForward = 0;
  return Modbus_WriteRegister(VFD_MOTOR_CTRL_ADDR, 0x0020);
}

uint8_t VFD_MotorSetFreq(float hz)
{
  return Modbus_WriteRegister(VFD_MOTOR_FREQ_ADDR, (uint16_t)(hz * 100.0f));
}

uint8_t VFD_MotorGo(void)
{
  uint8_t ok = Modbus_WriteRegister(VFD_MOTOR_CTRL_ADDR, s_motorDirForward ? 0x0012 : 0x0022);
  if (ok) s_motorRunning = 1;
  return ok;
}

uint8_t VFD_MotorStop(void)
{
  s_motorRunning = 0;
  return Modbus_WriteRegister(VFD_MOTOR_CTRL_ADDR, 0x0001);
}

void VFD_MotorKeepAlive(void)
{
  if (!s_motorRunning) return;
  Modbus_WriteRegister(VFD_MOTOR_CTRL_ADDR, s_motorDirForward ? 0x0012 : 0x0022);
}
