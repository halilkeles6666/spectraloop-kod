#include "jetson_link.h"
#include "modbus_vfd.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static UART_HandleTypeDef *s_huart;

/* Kesme, bayt bayt burada biriktirir; '\n' veya '\r' gorunce kucuk bir
 * DAIRESEL KUYRUGA ekler. TEK satirlik bir "double buffer" yeterli
 * sanilmisti ama YETERSIZ cikti: dashboard'daki "Baslat" butonu tek
 * tiklamada 3 komutu (MOTOR_DIR_FORWARD, MOTOR_FREQ, MOTOR_GO) neredeyse
 * ayni anda art arda gonderiyor -- ana dongu ilkini islemeden ikincisi
 * gelirse tek-satirlik tasarimda sessizce KAYBOLUYORDU (2026-08-12 canli
 * testte bulundu: sadece ilk komut isleniyordu). Kuyruk bu patlamalari
 * yutmak icin. */
#define LINE_BUF_LEN   96
#define LINE_QUEUE_LEN 6
static uint8_t  s_rxByte;
static char     s_active[LINE_BUF_LEN];
static uint8_t  s_activeLen;
static char     s_queue[LINE_QUEUE_LEN][LINE_BUF_LEN];
static volatile uint8_t s_qHead;   /* JetsonLink_Poll'un bir sonraki okuyacagi */
static volatile uint8_t s_qTail;   /* ISR'nin bir sonraki yazacagi            */
static volatile uint8_t s_qCount;

void JetsonLink_Init(UART_HandleTypeDef *huart)
{
  s_huart = huart;
  s_activeLen = 0;
  s_qHead = s_qTail = s_qCount = 0;
  HAL_UART_Receive_IT(s_huart, &s_rxByte, 1);
}

/* HAL zayif (weak) callback'ini burada override ediyoruz -- projede baska
 * hicbir UART kesme kullanmadigi icin (USART1/USART6 hala register-seviyesi
 * polling) cakisma riski yok. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance != s_huart->Instance) return;

  uint8_t b = s_rxByte;
  if (b == '\n' || b == '\r')
  {
    if (s_activeLen > 0 && s_qCount < LINE_QUEUE_LEN)
    {
      memcpy(s_queue[s_qTail], s_active, s_activeLen);
      s_queue[s_qTail][s_activeLen] = '\0';
      s_qTail = (s_qTail + 1) % LINE_QUEUE_LEN;
      s_qCount++;
    }
    /* Kuyruk doluysa (6 komut birikmis, JetsonLink_Poll uzun suredir hic
     * cagrilmamis demektir) bu satir sessizce atlanir -- olagandisi bir
     * durum, normal calismada olusmamali. */
    s_activeLen = 0;
  }
  else if (s_activeLen < LINE_BUF_LEN - 1)
  {
    s_active[s_activeLen++] = (char)b;
  }
  else
  {
    /* Asiri uzun/bozuk satir: at, yeniden basla. */
    s_activeLen = 0;
  }

  HAL_UART_Receive_IT(s_huart, &s_rxByte, 1);
}

static void SendLine(const char *s)
{
  HAL_UART_Transmit(s_huart, (uint8_t*)s, strlen(s), 200);
}

static const VFD_Param_t *FindParam(const char *code)
{
  for (uint8_t i = 0; i < VFD_PARAMS_COUNT; i++)
    if (strcmp(VFD_PARAMS[i].code, code) == 0) return &VFD_PARAMS[i];
  return NULL;
}

/* Kod VFD_PARAMS tablosunda YOKSA, Delta'nin resmi Pr. adresleme formulunu
 * ("GG-II" -> adres = grup*0x100 + indeks, VFD_PARAMS'taki tum mevcut
 * girdilerle dogrulandi: 01-13 -> 0x010D, 06-04 -> 0x0604 vb.) kullanarak
 * ham adresi hesaplar. Boylece tabloya ONCEDEN eklenmemis HERHANGI bir
 * Delta parametresi de (Pr.00-00 .. Pr.99-99) dashboard'daki genel
 * oku/yaz kutusundan erisilebilir olur -- her parametreyi tek tek
 * tabloya eklemeye gerek kalmaz. */
static uint8_t ParseGenericAddr(const char *code, uint16_t *addrOut)
{
  int group, index;
  if (sscanf(code, "%d-%d", &group, &index) != 2) return 0;
  if (group < 0 || group > 99 || index < 0 || index > 99) return 0;
  *addrOut = (uint16_t)((group << 8) | index);
  return 1;
}

static void HandleVfdReadAll(void)
{
  static char dataBuf[560];
  static char outBuf[620];
  int dn = 0;
  uint8_t anyOk = 0;

  for (uint8_t i = 0; i < VFD_PARAMS_COUNT; i++)
  {
    uint16_t raw;
    if (Modbus_ReadRegister(VFD_PARAMS[i].addr, &raw))
    {
      anyOk = 1;
      dn += sprintf(dataBuf + dn, "%s\"%s\":%u", i ? "," : "", VFD_PARAMS[i].code, raw);
    }
    else
    {
      dn += sprintf(dataBuf + dn, "%s\"%s\":null", i ? "," : "", VFD_PARAMS[i].code);
    }
    /* Bkz. daly_bms.c::Daly_ReadAll -- ayni BMS'te bulunan sorun: art arda,
     * gecikmesiz gonderilen komutlarin biri VFD'yi/RS485 hattini toparlanma
     * suresi olmadan yakalayip sessiz kalmasina neden olabiliyor (canli
     * testte dogrulandi: aralıksız 16 okumanin yarisi null donuyordu). */
    HAL_Delay(20);
  }

  sprintf(outBuf, "{\"type\":\"vfd_data\",\"connected\":%s,\"data\":{%s}}\r\n",
          anyOk ? "true" : "false", dataBuf);
  SendLine(outBuf);
}

/* Once VFD_PARAMS tablosuna bakar (isimli/bilinen parametreler), yoksa
 * genel "GG-II" formulune duser -- bkz. ParseGenericAddr. */
static uint8_t ResolveAddr(const char *code, uint16_t *addrOut)
{
  const VFD_Param_t *p = FindParam(code);
  if (p) { *addrOut = p->addr; return 1; }
  return ParseGenericAddr(code, addrOut);
}

static void HandleVfdRead(const char *code)
{
  char outBuf[96];
  uint16_t addr, raw = 0;
  uint8_t haveAddr = ResolveAddr(code, &addr);
  uint8_t ok = haveAddr && Modbus_ReadRegister(addr, &raw);

  if (ok)
    sprintf(outBuf, "{\"type\":\"vfd_data\",\"connected\":true,\"data\":{\"%s\":%u}}\r\n", code, raw);
  else
    sprintf(outBuf, "{\"type\":\"vfd_data\",\"connected\":false,\"data\":{\"%s\":null}}\r\n", code);
  SendLine(outBuf);
}

static void HandleVfdWrite(const char *code, int value)
{
  char outBuf[80];
  uint16_t addr;
  uint8_t haveAddr = ResolveAddr(code, &addr);
  uint8_t ok = haveAddr && Modbus_WriteRegister(addr, (uint16_t)value);

  sprintf(outBuf, "{\"type\":\"vfd_write_result\",\"code\":\"%s\",\"ok\":%s}\r\n",
          code, ok ? "true" : "false");
  SendLine(outBuf);
}

static void HandleMotorAck(const char *cmd, uint8_t ok)
{
  char outBuf[64];
  sprintf(outBuf, "{\"type\":\"motor\",\"data\":{\"cmd\":\"%s\",\"ok\":%s}}\r\n",
          cmd, ok ? "true" : "false");
  SendLine(outBuf);
}

static void HandleLine(char *line)
{
  char code[16];
  int value;
  float hz;

  if (strcmp(line, "VFD_READ_ALL") == 0)
  {
    HandleVfdReadAll();
  }
  else if (sscanf(line, "VFD_WRITE:%15[^:]:%d", code, &value) == 2)
  {
    HandleVfdWrite(code, value);
  }
  else if (sscanf(line, "VFD_READ:%15s", code) == 1)
  {
    HandleVfdRead(code);
  }
  else if (strcmp(line, "MOTOR_DIR_FORWARD") == 0)
  {
    HandleMotorAck(line, VFD_MotorDirForward());
  }
  else if (strcmp(line, "MOTOR_DIR_REVERSE") == 0)
  {
    HandleMotorAck(line, VFD_MotorDirReverse());
  }
  else if (sscanf(line, "MOTOR_FREQ:%f", &hz) == 1)
  {
    HandleMotorAck(line, VFD_MotorSetFreq(hz));
  }
  else if (strcmp(line, "MOTOR_GO") == 0)
  {
    HandleMotorAck(line, VFD_MotorGo());
  }
  else if (strcmp(line, "MOTOR_STOP") == 0)
  {
    HandleMotorAck(line, VFD_MotorStop());
  }
  /* Taninmayan satirlar (ornegin baska bir amaca yonelik komutlar) sessizce
   * yoksayilir -- Jetson tarafi da ayni sekilde taninmayan JSON tiplerini
   * yoksayar. */
}

/* Kuyrukta bekleyen TUM satirlari isler (tek seferde bir tane degil) --
 * "Baslat" gibi tek tikla 3 komut birden gonderen butonlarin hepsinin ayni
 * Poll() cagrisinda islenip bir sonraki BMS/VFD tarama adimina kadar
 * beklememesi icin. */
void JetsonLink_Poll(void)
{
  char line[LINE_BUF_LEN];

  while (1)
  {
    __disable_irq();
    if (s_qCount == 0)
    {
      __enable_irq();
      break;
    }
    memcpy(line, s_queue[s_qHead], sizeof(line));
    s_qHead = (s_qHead + 1) % LINE_QUEUE_LEN;
    s_qCount--;
    __enable_irq();

    HandleLine(line);

    /* Bkz. HandleVfdReadAll/daly_bms.c::Daly_ReadAll -- AYNI sorun burada da
     * cikti: "Baslat" gibi tek tiklamada MOTOR_DIR_FORWARD+MOTOR_FREQ+MOTOR_GO
     * uc komutu birden gonderen butonlar, kuyruktan gecikmesiz art arda
     * boşaltılınca VFD'nin toparlanma suresi olmadan ikinci/ucuncu Modbus
     * yazmasi SESSIZCE tutmuyordu (2026-08-12 canli testte bulundu: MOTOR_FREQ
     * yazmasi "ok" donuyordu ama 0x2001 geri okununca hala eski deger
     * okunuyordu). Kuyrukta baska komut varsa aralarina bu bekleme eklendi. */
    if (s_qCount > 0) HAL_Delay(20);
  }
}
