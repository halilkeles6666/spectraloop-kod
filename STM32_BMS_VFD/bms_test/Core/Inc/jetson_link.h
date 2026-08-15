/*
 * jetson_link.h
 *
 * Jetson <-> STM32 komut hatti (USART2, ayni USB-CDC/ST-Link VCP baglantisi
 * uzerinden BMS JSON'unu da tasiyan hat). uart_ws_bridge.py::send_to_pi()
 * ile birebir ayni satir-tabanli (\n sonlu) metin protokolunu dinler:
 *
 *   VFD_READ_ALL              -> {"type":"vfd_data","connected":bool,"data":{...}}
 *   VFD_WRITE:<code>:<value>  -> {"type":"vfd_write_result","code":"...","ok":bool}
 *   MOTOR_DIR_FORWARD/REVERSE -> VFD yon kaydi yazilir
 *   MOTOR_FREQ:<hz>           -> VFD frekans kaydi yazilir
 *   MOTOR_GO / MOTOR_STOP     -> VFD calistir/durdur
 *
 * Alis, USART2 RX kesmesiyle (bayt-bayt, arka planda) yapilir -- STM32'nin
 * ana dongude BMS/VFD taramasiyla mesgulken bile Jetson'dan gelen komutu
 * (ozellikle MOTOR_STOP gibi guvenlik-kritik olani) KACIRMAMASI icin. Komut
 * islenmesi (Modbus yazma/okuma) ise JetsonLink_Poll() ile ana dongude,
 * uygun araliklarla cagrilarak yapilir -- bkz. main.c::ScanBus/ScanVFD
 * icindeki JetsonLink_Poll() cagrilari (BMS/VFD taramasi tek seferde
 * saniyeler surebildigi icin, MOTOR_STOP'un o tarama bitene kadar
 * beklememesi burada onemli).
 */
#ifndef JETSON_LINK_H
#define JETSON_LINK_H

#include "main.h"

void JetsonLink_Init(UART_HandleTypeDef *huart);

/* Kesmenin doldurdugu hazir bir satir varsa isler (ayristirir, ilgili
 * Modbus/VFD fonksiyonunu cagirir, JSON cevabini geri yollar). Hazir satir
 * yoksa aninda doner -- bloklamaz. Ana dongudeki uzun tarama dongulerinin
 * (BMS adres taramasi, VFD parametre taramasi) ARASINDA sik sik cagirilmali. */
void JetsonLink_Poll(void);

#endif /* JETSON_LINK_H */
