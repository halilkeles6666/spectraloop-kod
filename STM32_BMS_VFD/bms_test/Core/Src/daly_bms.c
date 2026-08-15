#include "daly_bms.h"
#include <string.h>

static UART_HandleTypeDef *s_huart;
static uint8_t txBuf[13];
static uint8_t rxBuf[13];

void Daly_Init(UART_HandleTypeDef *huart)
{
  s_huart = huart;
}

static uint8_t CalcCk(const uint8_t *d, uint8_t len)
{
  uint16_t sum = 0;
  for (uint8_t i = 0; i < len; i++) sum += d[i];
  return (uint8_t)(sum & 0xFF);
}

static void BuildReq(uint8_t addr, uint8_t cmd)
{
  txBuf[0] = 0xA5;
  txBuf[1] = addr;
  txBuf[2] = cmd;
  txBuf[3] = 0x08;
  for (int i = 4; i < 12; i++) txBuf[i] = 0x00;
  txBuf[12] = CalcCk(txBuf, 12);
}

/* Bekleyen/eski UART hata bayraklarini (ORE/FE/NE/PE) temizler.
 * KRITIK: bunlar temizlenmeden HAL_UART_Receive cagirilirsa, TX
 * sirasinda RS485 modulunun yarim-duplex yankisindan kalma eski bir
 * hata bayragi HAL_UART_Receive'i cevap gelmeden ANINDA basarisiz
 * dondurebiliyor -- 0x95'in "hic bayt gelmiyor" gibi gorunmesinin
 * sebebi buydu (ciplak register-seviyesi teshisle bulundu). */
static void ClearUartErrors(void)
{
  __HAL_UART_CLEAR_OREFLAG(s_huart);
  __HAL_UART_CLEAR_FEFLAG(s_huart);
  __HAL_UART_CLEAR_NEFLAG(s_huart);
  __HAL_UART_CLEAR_PEFLAG(s_huart);
}

/* HAL_UART_Receive'in bu hatta (nedeni tam netlesmedi, muhtemelen HAL'in
 * ic state/timeout mekanizmasiyla ilgili) 0x95'in coklu-frame cevabini
 * SISTEMATIK olarak kacirdigi ciplak-register teshisle kanitlandi --
 * ayni anda dogrudan SR/DR yazmacindan polling ile YAKALANAN veri
 * kusursuzdu. O yuzden HAL_UART_Receive yerine dogrudan RXNE polling
 * kullaniyoruz; STM32'nin gercek darboğazi (baud hizi, islemci) 9600
 * baud'da bu tarz siki-donguyle kacirilmayacak kadar yavas. */
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

/* Istek gonderir + ilk 13 baytlik cevap frame'ini alir. */
static uint8_t SendAndRecv(uint8_t addr, uint8_t cmd)
{
  BuildReq(addr, cmd);
  memset(rxBuf, 0, sizeof(rxBuf));
  ClearUartErrors();
  HAL_UART_Transmit(s_huart, txBuf, 13, 100);
  if (!ReceiveFrameRaw(rxBuf, 13, 500)) return 0;
  if (rxBuf[0] != 0xA5) return 0;
  if (CalcCk(rxBuf, 12) != rxBuf[12]) return 0;
  return 1;
}

/* Ayni istegin bir sonraki frame'ini (yeniden istek atmadan) okur --
 * 0x95/0x96 gibi coklu-frame cevaplarda BMS art arda gonderiyor. */
static uint8_t RecvNextFrame(void)
{
  memset(rxBuf, 0, sizeof(rxBuf));
  if (!ReceiveFrameRaw(rxBuf, 13, 800)) return 0;
  if (rxBuf[0] != 0xA5) return 0;
  if (CalcCk(rxBuf, 12) != rxBuf[12]) return 0;
  return 1;
}

static void Parse90(Daly_Data_t *out)
{
  uint16_t v_raw   = (rxBuf[4]  << 8) | rxBuf[5];
  uint16_t i_raw   = (rxBuf[8]  << 8) | rxBuf[9];
  uint16_t soc_raw = (rxBuf[10] << 8) | rxBuf[11];

  out->voltage = v_raw * 0.1f;
  out->current = ((int32_t)i_raw - 30000) * 0.1f;
  out->soc     = soc_raw * 0.1f;
}

/* 0x95 hucre voltaji: her frame'de data[0]=frame no (1 tabanli),
 * data[1:2]/[3:4]/[5:6] = 3 hucre (mV, buyuk-uctan). 24 hucre icin
 * ceil(24/3)=8 frame gelir. */
static uint8_t ReadCellVoltages(uint8_t addr, Daly_Data_t *out)
{
  out->cellCount = 0;
  if (!SendAndRecv(addr, 0x95)) return 0;

  const uint8_t framesExpected = (DALY_MAX_CELLS + 2) / 3;  /* ceil(24/3)=8 */
  for (uint8_t f = 0; f < framesExpected; f++)
  {
    if (f > 0 && !RecvNextFrame()) break;

    uint8_t frameNo = rxBuf[4];
    if (frameNo == 0) break;

    for (uint8_t i = 0; i < 3; i++)
    {
      uint8_t cellIdx = (frameNo - 1) * 3 + i;
      if (cellIdx >= DALY_MAX_CELLS) continue;

      uint16_t mv = (rxBuf[5 + i * 2] << 8) | rxBuf[6 + i * 2];
      if (mv == 0 || mv == 0xFFFF) continue;      /* bos/kullanilmayan slot */

      out->cells[cellIdx] = mv * 0.001f;
      if (cellIdx + 1 > out->cellCount) out->cellCount = cellIdx + 1;
    }
  }
  return out->cellCount > 0;
}

/* 0x96 sicaklik: data[0]=frame no, data[1..]=sensor sicakliklari (1 bayt,
 * -40 ofsetli). Arayuz tek deger istedigi icin sadece ilk sensoru aliyoruz. */
static uint8_t ReadTemperature(uint8_t addr, Daly_Data_t *out)
{
  if (!SendAndRecv(addr, 0x96)) return 0;
  out->temperature = (int16_t)rxBuf[5] - 40;
  return 1;
}

uint8_t Daly_ReadAll(uint8_t addr, Daly_Data_t *out)
{
  memset(out, 0, sizeof(*out));

  out->ok = SendAndRecv(addr, 0x90);
  if (!out->ok) return 0;
  Parse90(out);

  /* BMS, bir istege cevap verdikten hemen sonra ikinci bir istegi
   * (ozellikle 8-frame'lik agir 0x95'i) gecikmesiz alirsa cevap vermiyor --
   * komutlar arasinda kisa bir "toparlanma" suresine ihtiyaci var. Bu
   * gecikme olmadan 0x95 sistematik olarak sessiz kaliyordu; canli testte
   * dogrulandi (24 hucrenin tamami dogru degerlerle geldi). */
  HAL_Delay(30);
  ReadCellVoltages(addr, out);
  HAL_Delay(30);
  ReadTemperature(addr, out);
  return 1;
}
