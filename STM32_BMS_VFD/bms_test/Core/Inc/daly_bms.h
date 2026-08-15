/*
 * daly_bms.h
 *
 * Daly Smart BMS UART/RS485 protokolu - toplam V/I/SOC (0x90),
 * hucre voltajlari (0x95) ve sicaklik (0x96) okuma.
 *
 * Cikan Daly_Data_t, Spectraloop dashboard'unun (index.html ::
 * updBms()) bekledigi alanlarla birebir eslesecek sekilde tasarlandi:
 *   v=toplam voltaj, s=SOC, i=akim, c[]=hucre voltajlari, t=sicaklik.
 */
#ifndef DALY_BMS_H
#define DALY_BMS_H

#include "main.h"
#include <stdint.h>

#define DALY_MAX_CELLS 24   /* paket basina 24S */

typedef struct {
  uint8_t  ok;                       /* 1 = 0x90 basarili (paket cevap verdi) */
  float    voltage;                  /* toplam paket voltaji (V)              */
  float    current;                  /* akim (A)                              */
  float    soc;                      /* sarj durumu (%)                       */
  uint8_t  cellCount;                /* kac hucre okunabildi (0..24)          */
  float    cells[DALY_MAX_CELLS];    /* hucre voltajlari (V)                  */
  int16_t  temperature;              /* derece C (ilk sicaklik sensoru)       */
} Daly_Data_t;

void    Daly_Init(UART_HandleTypeDef *huart);

/* addr'deki BMS'ten sirayla 0x90 + 0x95 + 0x96 okur, out'u doldurur.
 * Donus: 1 = 0x90 basarili (out->ok da 1). 0x90 basarisizsa hucre/sicaklik
 * hic denenmez, out sifirlanmis olarak doner. */
uint8_t Daly_ReadAll(uint8_t addr, Daly_Data_t *out);

#endif /* DALY_BMS_H */
