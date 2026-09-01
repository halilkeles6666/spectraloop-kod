# Sürücü / Yolcu Kontrol Paneli

Kapsül kontrol sistemi için siyah-neon mavi konseptli, ekranı Sürücü ve Yolcu olmak üzere ikiye bölen bir arayüz. Her iki tarafın ortasında 16:9 video oynatıcı, altında ise ilgili videoyu tetikleyen 5'er buton bulunur.

## Klasör yapısı

```
SurucuYolcuPaneli/
├─ kod/
│   └─ index.html      # Tüm HTML/CSS/JS
└─ videolar/            # Butonlarla eşleşen mp4 dosyaları
```

## Çalıştırma

`kod/index.html` dosyasını bir tarayıcıda açmak yeterli. `kod` ve `videolar` klasörlerinin birbirine göre konumu (`../videolar/...`) değişmemeli.

## Butonlar

**Sürücü:** Sensör Gürültü · Bms · Kabin Basıncı · Doğrulama Hatası · Levitasyon

**Yolcu:** Kabin Basıncı · Levitasyon · Sıcaklık · Acil Durum · Durma
