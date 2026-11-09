```
KTIP: 0009
Başlık: Çift Yöntemli Madenci Kaydı ve Çift Kulvarlı (7 + 1 + 3) Komite Kota Seçimi
Yazar: KristaTech Çekirdek Geliştiricileri
Durum: Active
Tür: Standard Track (Consensus)
Oluşturulma Tarihi: 2026-08-15
```

## Özet
Bu öneri, ADAM konsensüs katmanındaki zincir üstü **Çift Yöntemli Madenci Kaydı** (`Coin-Lock` ve `PoW-Lock`) ile **Çift Kulvarlı ($7 + 1 + 3$) Komite Kota Seçimi (`ElectDualTrackValidators`)** mekanizmalarını tanımlar. 11 doğrulayıcı koltuğunun **1. Kulvar (Teminatlı Havuz — 8 Koltuk: 7 Aktif Masternode [`4.200 KRISTA`] + 1 Coin-Lock [`250 KRISTA`])** ve **2. Kulvar (Açık PoW Havuzu — 3 Koltuk)** olarak ayrılmasıyla; düşük maliyetli PoW kayıtlarının yüksek teminatlı doğrulayıcıları dışlaması veya çoğunluk eşiğine tek başına ulaşması matematiksel olarak engellenirken, izinsiz açık madencilik katılımı korunur.

## Motivasyon
Kayıtlı her açık anahtarın teminat kademesinden bağımsız olarak eşit $1/P$ seçilme olasılığına sahip olduğu düz bir aday havuzunda, bir saldırgan düşük maliyetli `PoW-Lock` kimlikleriyle havuzu doldurarak 11 doğrulayıcı koltuğunun çoğunluğunu ele geçirebilir; böylece çözümleri saklayarak blok üretimini durdurabilir veya Masternode teminatı kilitlemeden katılımcı ödüllerini domine edebilir.

Buna karşılık, her katılımcıdan 4.200 KRISTA veya 250 KRISTA teminat istemek, başlangıç bakiyesi olmayan yeni CPU/GPU madencilerinin ağa katılmasını imkansız hale getirir.

`KTIP-0009` bu dengeyi iki mekanizmayla kurar:
1. **Sıkılaştırılmış Çift Yöntemli Madenci Kaydı**: Kayıt işlemleri üzerinde katı teminat, girdi onay derinliği ve blok başına işlem sınırı kuralları uygular.
2. **Çift Kulvarlı ($7 + 1 + 3$) Komite Kota İzolasyonu**: 7 koltuğu Aktif Masternode'lara (`4.200 KRISTA`), 1 koltuğu Coin-Lock doğrulayıcılara (`250 KRISTA`) ve 3 koltuğu Açık PoW madencilerine (`10 KRISTA` / `0 KRISTA`) ayırır ve bu dağılımı hem derleme anında (`static_assert`) hem de çalışma anında (`assert`) zorunlu kılar.

## Teknik Özellikler

### 1. Çift Yöntemli Madenci Kaydı (`registerminer`)

#### A. Coin-Lock Kaydı (`250 KRISTA`)
* **Kilit Miktarı**: `nValue >= MINER_REGISTRATION_LOCK_AMOUNT` (`250 * COIN`) şarttır. Coin-Lock betik biçiminde 250 KRISTA'dan az bakiye kilitleyen çıktılar hem mempool politikası (`AcceptToMemoryPoolWorker`) hem de blok doğrulaması (`CheckBlock` içinde `bad-miner-reg-amount`) tarafından reddedilir.
* **Aktivasyon**: Mainnet üzerinde Model D aktifleştiğinde (`nHeight >= 2200`), Testnet/Regtest üzerinde ise tüm yüksekliklerde açıktır.
* **Kilit Süresi**: `nLockHeight - nTxHeight >= nRegPeriod` (Mainnet üzerinde `2.880` blok; Testnet/Regtest üzerinde Model D sonrasında `100` blok).

#### B. PoW-Lock Kaydı (`10 KRISTA` Fonlu / `0 KRISTA` Zero-Coin Bootstrap)
* **Çevrimdışı PoW Bulmacası**: Son `nRegPeriod` blok içindeki yakın bir blok özetine bağlı 32 baytlık `[24-bayt blockHash öneki || 8-bayt nonce]` verisinin, `SHA256D(blockHash || minerPubKey || nonce)` özeti en az 16 öncü sıfır bitine sahip olacak şekilde çözülmesini gerektirir.
* **Fonlu `PoW-Lock` (`10 KRISTA`)**:
  - `nValue >= POW_LOCK_REGISTRATION_AMOUNT` (`10 * COIN`) kilitlenmelidir.
  - Tüm işlem girdileri (`vin`) en az **`ADAM_POW_LOCK_MIN_INPUT_DEPTH = 100` onaya** sahip UTXO'ları harcamalıdır (`bad-pow-lock-input-depth`); böylece çoklu kimlik üretmek için hızlı UTXO bölme saldırıları engellenir.
  - Blok başına en fazla **`ADAM_MAX_POW_LOCK_TX_PER_BLOCK = 3`** adet fonlu `PoW-Lock` işlemine izin verilir (`bad-pow-lock-block-quota`).
* **Zero-Coin Bootstrap `PoW-Lock` (`0 KRISTA`)**:
  - Yalnızca aktif doğrulayıcı havuzunda (`GetAdamMinerPool`) **13'ten az aktif anahtar** (`ADAM_ZERO_COIN_POW_MAX_POOL = 13`) bulunduğunda bootstrap canlılığını garanti etmek için `nValue == 0` kaydına izin verilir.

---

### 2. Çift Kulvarlı ($7 + 1 + 3$) Komite Kota Seçimi (`ElectDualTrackValidators`)
`nHeight >= consensus.nAdamDualTrackHeight` (`UPGRADE_ADAM` = Mainnet/Testnet'te blok 200, Regtest'te blok 1) olduğunda, `SelectAdamNodes` (`src/adam.cpp`) aday havuzunu (seçilen Koordinatör hariç) $\text{SHA256D}(\text{Seed}_H \mathbin{\Vert} \text{PubKey}_i)$ puanına göre sıralanmış üç deterministik alt havuza ayırır:

| Kulvar / Alt-Kulvar | Uygun Adaylar | Hedef Kota | Derleme / Çalışma Anı Değişmezi |
| :--- | :--- | :---: | :--- |
| **Alt-Kulvar 1A (Masternode'lar)** | Aktif Masternode'lar (`4.200 KRISTA` + son 2.880 blokta ping) | **7 Koltuk** | `ADAM_MASTERNODE_TRACK_QUOTA = 7` |
| **Alt-Kulvar 1B (Coin-Lock)** | Aktif `Coin-Lock` Doğrulayıcılar (`250 KRISTA` CLTV) | **1 Koltuk** | `ADAM_COIN_LOCK_TRACK_QUOTA = 1` |
| **1. Kulvar Toplamı (Teminatlı)** | Alt-Kulvar 1A + Alt-Kulvar 1B (Kulvar-1 içi doğrudan taşma ile) | **8 Koltuk (%72,7)** | `ADAM_COLLATERALIZED_TRACK_QUOTA = 8` |
| **2. Kulvar (Açık PoW)** | `PoW-Lock` (`10 KRISTA`), `Zero-Coin PoW` (`0 KRISTA`), Bootstrap | **3 Koltuk (%27,3)** | `ADAM_OPEN_POW_TRACK_QUOTA = 3` |

#### Derleme Anı ve Çalışma Anı Değişmezleri (Invariants)
`src/adam.h` ve `src/chainparams.cpp` içerisinde:
```cpp
static_assert(ADAM_MASTERNODE_TRACK_QUOTA + ADAM_COIN_LOCK_TRACK_QUOTA == ADAM_COLLATERALIZED_TRACK_QUOTA,
              "Track 1 sub-quotas must sum to ADAM_COLLATERALIZED_TRACK_QUOTA");
static_assert(ADAM_MASTERNODE_TRACK_QUOTA >= 7,
              "Masternode track quota must be >= post-bootstrap quorum threshold (7)");
assert(ADAM_MASTERNODE_TRACK_QUOTA >= consensus.nAdamThreshold);
```
`7 >= nAdamThreshold (7)` olduğundan, Masternode olmayan 4 koltuğun tamamı (`1 Coin-Lock + 3 Açık PoW`) çözüm göndermese dahi 7 Aktif Masternode koltuğu bootstrap sonrası çoğunluk eşiğini tek başına karşılar.

#### İki Aşamalı Çift Yönlü Taşma (Spillover)
1. **Kulvar-1 İçi Taşma**: Alt-Kulvar 1A'da boş kalan koltuklar (`< 7` Aktif Masternode) anında Alt-Kulvar 1B'deki (`Coin-Lock`) sıradaki adaylarla doldurulur; benzer şekilde Alt-Kulvar 1B'de aday yoksa (`< 1` Coin-Lock) koltuk sıradaki Aktif Masternode'a devredilir.
2. **Kulvarlar Arası Taşma**: 1. Kulvarın tamamında 8'den az aday varsa boş kalan koltuklar 2. Kulvardaki (`Açık PoW`) sıradaki adaylara taşar; tersine 2. Kulvarda 3'ten az aday varsa boş koltuklar 1. Kulvardaki sıradaki adaylara (önce Masternode, sonra Coin-Lock) taşar.

## Geriye Dönük Uyumluluk
* `consensus.nAdamDualTrackHeight` (`UPGRADE_ADAM`) yüksekliğinde etkinleşir.
* Mevcut `Coin-Lock` (`>= 250 KRISTA`) ve fonlu `PoW-Lock` (`>= 10 KRISTA`) betikleriyle tam uyumludur.

## Referans Uygulama
* Kota sabitleri ve derleme/çalışma anı doğrulamaları: `src/adam.h` ve `src/chainparams.cpp`.
* Çift kulvarlı seçim ve havuz sınıflandırması: `src/adam.cpp` içindeki `ElectDualTrackValidators`, `SelectAdamNodes`, `IsCoinLockValidatorKey` ve `GetAdamMinerPool`.
* Kayıt işlemlerinin mempool ve blok doğrulaması: `src/main.cpp` ve `src/rpc/mining.cpp`.
