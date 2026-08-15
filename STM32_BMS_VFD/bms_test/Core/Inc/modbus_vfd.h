/*
 * modbus_vfd.h
 *
 * Delta MS300 Modbus RTU (VFD) suruculugu - Pi'deki vfd_modbus.py ile
 * BIREBIR ayni protokol/adresler/CRC. Boylece Jetson tarafinda hicbir
 * degisiklik gerekmeden Pi'nin yerini alabiliyor.
 *
 * Fiziksel: STM32 USART6 (PA11=TX, PA12=RX) -> izole TTL<->RS485 modul
 *           -> VFD'nin Modbus RTU (A/B/GND) terminalleri.
 * Baud: 9600, 8N1, slave ID = 1 (vfd_modbus.py ile ayni).
 */
#ifndef MODBUS_VFD_H
#define MODBUS_VFD_H

#include "main.h"
#include <stdint.h>

#define VFD_SLAVE_ID         1
#define VFD_MOTOR_CTRL_ADDR  0x2000   /* motor calistir/durdur/yon         */
#define VFD_MOTOR_FREQ_ADDR  0x2001   /* motor frekans komutu (Hz x 100)   */

typedef struct {
  const char *code;   /* Delta parametre kodu, ornegin "01-00"  */
  uint16_t    addr;   /* Modbus register adresi                 */
  const char *name;   /* Turkce aciklama                        */
  float       scale;  /* ham deger -> gercek deger carpani       */
} VFD_Param_t;

extern const VFD_Param_t VFD_PARAMS[];
extern const uint8_t     VFD_PARAMS_COUNT;

void    Modbus_Init(UART_HandleTypeDef *huart);

/* Fonksiyon 0x03 (tek register oku) / 0x06 (tek register yaz).
 * Donus: 1 = basarili, 0 = zaman asimi / CRC hatasi / exception yaniti. */
uint8_t Modbus_ReadRegister(uint16_t addr, uint16_t *outValue);
uint8_t Modbus_WriteRegister(uint16_t addr, uint16_t value);

/* Yuksek seviye motor komutlari (vfd_modbus.py::_handle_motor esdegeri) */
uint8_t VFD_MotorDirForward(void);
uint8_t VFD_MotorDirReverse(void);
uint8_t VFD_MotorSetFreq(float hz);
uint8_t VFD_MotorGo(void);
uint8_t VFD_MotorStop(void);

/* VFD'nin kendi haberlesme-kaybi guvenlik zaman asimini (Pr.09-03, ~1-2s)
 * canli tutmak icin: motor "calisir" durumdaysa (son komut MOTOR_GO ise ve
 * MOTOR_STOP gelmediyse) RUN kontrol sozcugunu tekrar yazar; degilse hicbir
 * sey yapmaz. Ana dongude (main.c) SIK cagirilmali (~<500ms araliklarla,
 * BMS/VFD tarama dongulerinin arasina serpistirilmis) -- yoksa VFD, STM32
 * bir sure Modbus trafigi gondermedigini "baglanti koptu" sanip motoru
 * kendiliginden durduruyor (2026-08-12 canli testte gozlemlendi: ~2sn). */
void VFD_MotorKeepAlive(void);

#endif /* MODBUS_VFD_H */
