# Drone Fleet OS — Proje Özeti

**FleetOS**, İşletim Sistemleri dersi kapsamında geliştirilen bir mini OS simülatörüdür. Tema olarak Drone Filo Kontrolcüsü seçilmiştir.

## Bileşenler

- **Zamanlayıcı**: RR ve MLFQ
- **Bellek Yönetimi**: Sayfalama (256B sayfa, 16 çerçeve)
- **Eşzamanlılık**: Üretici-Tüketici (sınırlı tampon)
- **Dosya Sistemi**: FAT tabanlı (64 blok × 64B)
- **Kilitlenme Tespiti**: Kaynak tahsis grafiği
- **Hata Senaryosu**: Drone çökmesi
- **Görselleştirme**: Python Flask + HTML/JS dashboard

## Hızlı Başlangıç

```bash
make
./drone_fleet_os --mode mlfq
```

Dashboard için:
```bash
python3 web/server.py
```

## Branch Stratejisi

- `main`: Stabil kod
- `dev`: Entegrasyon branch'i
- `feature/*`: Modül geliştirme branch'leri
