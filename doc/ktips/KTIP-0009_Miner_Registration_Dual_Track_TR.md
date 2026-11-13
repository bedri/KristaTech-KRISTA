```
KTIP: 0009
Başlık: Çift Yöntemli Madenci Kaydı ve Çift Kulvarlı (7 + 1 + 3) Komite Kota Seçimi
Yazar: KristaTech Çekirdek Geliştiricileri
Durum: Aktif
Tür: Standart Takip (Konsensüs)
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
* **Kilit Miktarı**: 33 baytlık sıkıştırılmış açık anahtar ile `nValue >= MINER_REGISTRATION_LOCK_AMOUNT` (`250 * COIN`) şarttır. Coin-Lock betik biçiminde (`MatchCoinLockRegistration`) 250 KRISTA'dan az bakiye kilitleyen çıktılar mempool politikası (`AcceptToMemoryPoolWorker` içinde `bad-txns-coinlock-amount-too-small`) tarafından reddedilir ve aktif havuz taramasında (`GetAdamMinerPool`) yok sayılır.
* **Anti-Sybil Girdi Olgunluğu, Hız Sınırları ve Cüzdan Koruması**: Tüm işlem girdileri (`vin`) en az **`ADAM_COIN_LOCK_MIN_INPUT_DEPTH = 100` onaya** sahip UTXO'ları harcamalıdır (mempool ve `ConnectBlock` içinde `bad-txns-coinlock-input-immature`); konsensüs (`CheckBlock`), mempool, `CreateNewBlock` ve `ExtractBlockMinerRegEntries` işlem başına en fazla **1 adet `Coin-Lock` çıktısı** (`bad-txns-too-many-coinlock`) ve blok başına en fazla **`ADAM_MAX_COIN_LOCK_TX_PER_BLOCK = 1` adet `Coin-Lock` işlemi** (`bad-blk-too-many-coinlock`) kabul eder; cüzdan/RPC/GUI katmanı ise **cüzdan başına en fazla 1 aktif `Coin-Lock` koltuğuna** izin verir (Mainnet'te son `240`, Model D sonrası Testnet/Regtest'te son `15` blokta yenilenebilir).
* **Aktivasyon**: Konsensüs ve manuel `registerminer lock` komutu Mainnet, Testnet ve Regtest üzerinde **tüm blok yüksekliklerinde** `250 KRISTA` Coin-Lock kabul ederken, arka plan `AutoRegisterMiner` bootstrap ödüllerini likit tutmak amacıyla Mainnet üzerinde `nHeight >= 2200` yüksekliğini bekler.
* **Kilit Süresi**: `nLockHeight - nTxHeight >= nRegPeriod` (Mainnet üzerinde `2.880` blok; Testnet/Regtest üzerinde Model D aktifleştiğinde `100` blok, Model D öncesinde `2.880` blok).

#### B. PoW-Lock Kaydı (`10 KRISTA` Fonlu / `0 KRISTA` Zero-Coin Bootstrap)
* **Çevrimdışı PoW Bulmacası (`MatchPoWLockRegistration`)**: Ata bloğa bağlı `<4-bayt LE32 nonce> <32-bayt challenge blockHash> <33-bayt sıkıştırılmış minerPubKey> OP_DROP OP_DROP OP_DROP` verisini içerir:
  - **Fonlu `PoW-Lock` (`10 KRISTA`, Blok `1`'de `UPGRADE_HARDENING_V2` ile `HashX11KVS` üzerinde birleştirilmiş)**: Son `100` blok içindeki bir ata bloğa (`pindexChallenge->nHeight >= nHeight - 100`) bağlı 7 seviyeli özyinelemeli **`HashX11KVS("KRPL" || challenge || 33-bayt sıkıştırılmış minerPubKey || "POWLKv2" || LE32(nNonce))`** (`ComputeMinerPoWRegistrationHash`) özetinin `GetMinerPoWLimit(..., true)` (Mainnet'te **15-bit** [`~14,8 sn` CPU], Testnet'te **13-bit**, Regtest'te **8-bit** öncü sıfır) hedefini karşılamasını gerektirir; hem mempool kabulünde (`bad-txns-powlock-invalid` / `bad-txns-powlock-stale-challenge`) hem de konsensüs havuz taramasında (`ExtractBlockMinerRegEntries`) doğrulanır.
  - **Zero-Coin `PoW-Lock` (`0 KRISTA`, Blok `1`'de `UPGRADE_HARDENING_V2` ile güçlendirilmiş)**: Zincir üstünde son **`ADAM_ZEROCOIN_CHALLENGE_WINDOW_V2 = 10` blok** (mempool kabulünde son `8` blok) içindeki bir ata bloğa bağlı 7 seviyeli özyinelemeli **`HashX11KVS("KRZC" || challenge || 33-bayt sıkıştırılmış minerPubKey || "SYBILV2" || LE32(nNonce))`** (`ComputeZeroCoinRegistrationHash`) özetinin `GetZeroCoinPoWLimit` (Mainnet'te **14-bit** [`~7,4 sn` CPU], Testnet'te **12-bit**, Regtest'te **8-bit** öncü sıfır) hedefini karşılamasını gerektirir.
* **Fonlu `PoW-Lock` (`10 KRISTA`)**:
  - `nValue >= POW_LOCK_REGISTRATION_AMOUNT` (`10 * COIN`) kilitlenmelidir (mempool ve `CheckBlock` içinde `bad-txns-powlock-amount-too-small` ile zorunludur).
  - Tüm işlem girdileri (`vin`) en az **`ADAM_POW_LOCK_MIN_INPUT_DEPTH = 100` onaya** sahip UTXO'ları harcamalıdır (mempool ve `ConnectBlock` içinde `bad-txns-powlock-input-immature`); böylece çoklu kimlik üretmek için hızlı UTXO bölme saldırıları engellenir.
  - İşlem başına en fazla **1 adet `PoW-Lock` çıktısı** (`bad-txns-too-many-powlock`) ve blok başına en fazla **`ADAM_MAX_POW_LOCK_TX_PER_BLOCK = 1`** adet fonlu `PoW-Lock` işlemi (`bad-blk-too-many-powlock`, blok başına `1` olan `Zero-Coin PoW` sayacından bağımsız) kabul edilir; cüzdan/RPC/GUI katmanı da **cüzdan başına en fazla 1 aktif `PoW` koltuğu** kuralını uygular (`0 -> 10 KRISTA` terfisi her an serbest; fonlu yenileme penceresi son `240` / `15` blok).
* **Zero-Coin Bootstrap `PoW-Lock` (`0 KRISTA`)**:
  - Yalnızca **fonlu** doğrulayıcı havuzunda (`GetFundedAdamMinerPoolCount`: Aktif Masternode'lar + `250 KRISTA` Coin-Lock + `10 KRISTA` Fonlu PoW-Lock) **`consensus.nAdamMinersCount + 2` (`13`) adetten az fonlu doğrulayıcı** bulunduğunda sentinel `0x01:0` girdili `nValue == 0` kaydına (blok başına en fazla `1` adet, `bad-blk-too-many-zerocoin-pow`) izin verilir.
  - `UPGRADE_HARDENING_V2` kapsamında aktif `Zero-Coin PoW` koltukları **`ADAM_ZEROCOIN_POW_REG_PERIOD_V2 = 240` blok** (Mainnet'te `~2 saat`; Testnet/Regtest'te Model D sonrasında `50` blok) azami kiralama ömrüne (`nEffectiveExpiry = min(lockTime, nTxHeight + nZeroActivePeriod)`) sahiptir, en yeni **`MAX_ZEROCOIN_BOOTSTRAP_MINERS_V2 = 16`** benzersiz anahtar ile sınırlandırılır, Blok `1..199` Klasik PoW bootstrap madencilerini Zero-Coin kayıtlarından önce korur, halihazırda aktif olan anahtarların mükerrer mempool yenilemelerini (`bad-txns-zerocoin-key-already-active`) reddeder ve cüzdanda olgunlaşmış bakiye oluştuğunda `AutoRegisterMiner` ile otomatik olarak Fonlu `10 KRISTA` / `250 KRISTA` kilidine terfi ettirilir. Tüm kayıt kilitlerinin ayrıca katı blok yüksekliği CLTV (`< LOCKTIME_THRESHOLD = 500.000.000`, `<= nHeight + 5.760`) kullanması zorunludur.

---

### 2. Çift Kulvarlı ($7 + 1 + 3$) Komite Kota Seçimi (`ElectDualTrackValidators`)
`nHeight >= consensus.nAdamDualTrackHeight` (Mainnet/Testnet'te `UPGRADE_ADAM` ile eşleşen blok `200`, Regtest'te blok `1`) olduğunda, `SelectAdamNodes` (`src/adam.cpp`) aday havuzunu (seçilen Koordinatör hariç) $\text{SHA256D}(\text{Seed}_H \mathbin{\Vert} \text{PubKey}_i)$ puanına göre sıralanmış üç deterministik alt havuza ayırır:

| Kulvar / Alt-Kulvar | Uygun Adaylar | Hedef Kota | Derleme / Çalışma Anı Değişmezi |
| :--- | :--- | :---: | :--- |
| **Alt-Kulvar 1A (Masternode'lar)** | Aktif Masternode'lar (`4.200 KRISTA` + son 2.880 blokta ping) | **7 Koltuk** | `ADAM_MASTERNODE_TRACK_QUOTA = 7` |
| **Alt-Kulvar 1B (Coin-Lock)** | Aktif `Coin-Lock` Doğrulayıcılar (`250 KRISTA` CLTV, kesin 1 birincil koltukla izole) | **1 İzole Koltuk** | `ADAM_COIN_LOCK_TRACK_QUOTA = 1` |
| **1. Kulvar Toplamı (Teminatlı)** | Alt-Kulvar 1A + Alt-Kulvar 1B (`< 1` Coin-Lock Aktif MN'e taşar; `< 7` MN önce Açık PoW'a taşar) | **8 Koltuk (%72,7)** | `ADAM_COLLATERALIZED_TRACK_QUOTA = 8` |
| **2. Kulvar (Açık PoW)** | Katı öncelik (`UPGRADE_HARDENING_V2`): `Kademe 2A` (`10 KRISTA` `PoW-Lock` VRF yarışı) $\rightarrow$ `Kademe 2B` Bootstrap (`Blok 1..199`) $\rightarrow$ `Kademe 2C` `Zero-Coin PoW` (`0 KRISTA`, azami `16`) | **3 Koltuk (%27,3)** | `ADAM_OPEN_POW_TRACK_QUOTA = 3` |

#### Derleme Anı ve Çalışma Anı Değişmezleri (Invariants)
`src/adam.h` ve `src/chainparams.cpp` içerisinde:
```cpp
static_assert(ADAM_MASTERNODE_TRACK_QUOTA + ADAM_COIN_LOCK_TRACK_QUOTA == ADAM_COLLATERALIZED_TRACK_QUOTA,
              "Track 1 sub-quotas must sum to ADAM_COLLATERALIZED_TRACK_QUOTA");
static_assert(ADAM_MASTERNODE_TRACK_QUOTA >= Consensus::Params::ADAM_MAX_CONSENSUS_THRESHOLD,
              "Masternode track quota must be at least ADAM_MAX_CONSENSUS_THRESHOLD so Masternodes alone can always form a quorum");
assert(ADAM_MASTERNODE_TRACK_QUOTA >= consensus.nAdamThreshold);
```
`7 >= nAdamThreshold (7)` olduğundan, Masternode olmayan 4 koltuğun tamamı (`1 Coin-Lock + 3 Açık PoW`) çözüm göndermese dahi 7 Aktif Masternode koltuğu bootstrap sonrası çoğunluk eşiğini tek başına karşılar.

#### Katı Kota İzolasyonu, Sıralı Taşma (Spillover), Ödeme Koruması ve Kota-Tavanlı Koordinatör VRF
1. **Kesin `1` Koltuklu `Coin-Lock` İzolasyonu**: `Alt-Kulvar 1B` en fazla **1** adet `Coin-Lock` doğrulayıcısı (`%8,00` katılımcı ödül payı) seçer. Seçilemeyen `Coin-Lock` anahtarları Açık PoW'un önünde `Alt-Kulvar 1A`'ya taşmaz ve 2. Kulvar `Kademe 2A` (`vTier2AFunded`) havuzuna girmez. Hiç `Coin-Lock` adayı yoksa (`< 1`), boş koltuk anında sıradaki Aktif Masternode'a (`4.200 KRISTA`) devredilir.
2. **Sıralı Boş Koltuk Taşması ve Yedek Önceliği**: 11 kişilik komitede boş kalan tüm koltuklar (örneğin `< 7` Aktif Masternode veya `< 3` Açık PoW madencisi bulunduğunda) katı sırayla doldurulur: **kalan Aktif Masternode'lar (`4.200 KRISTA`) $\rightarrow$ kalan Açık PoW (`10 KRISTA` `PoW-Lock` $\rightarrow$ `Bootstrap` $\rightarrow$ `Zero-Coin PoW`) $\rightarrow$ seçilemeyen `Coin-Lock` (`250 KRISTA`, yalnızca toplam havuz `< 11` olduğunda son çare canlılık yedeği)**. Yedek (standby) sıralaması da bire bir aynı `Aktif Masternode -> Açık PoW -> Coin-Lock` önceliğini izler.
3. **Kulvar Korumalı Koltuk Ödemeleri ve Kota-Tavanlı Koordinatör VRF**: Blok `200` itibarıyla (`IsAdamValidatorSplitActive`) `ComputeModelDValidatorSeatPayments` fonksiyonunda en fazla `1` `Coin-Lock` doğrulayıcısı `%8,00` alırken, en fazla `3` adet Masternode ve `Coin-Lock` dışı Açık PoW doğrulayıcısı (`Fonlu PoW-Lock 10 -> Bootstrap -> Zero-Coin PoW`) `%4,00`'er pay alır; boş bir Masternode koltuğunu dolduran taşma doğrulayıcıları (Açık PoW veya son çare `Coin-Lock`) ise `nMNSeatBase` (`~%2,857`) alır. `SelectAdamNodes` ise birincil Koordinatörü kota-tavanlı teminat ağırlıklı VRF ile **tüm Aktif Masternode'lar (`4.200`'er ağırlık), VRF lideri en fazla `1` `Coin-Lock` temsilcisi (`250` ağırlık) ve VRF lideri en fazla `3` `Fonlu PoW-Lock` temsilcisi (`10`'ar ağırlık, Açık PoW kulvar tavanı `30`)** arasından seçer.

## Geriye Dönük Uyumluluk
* `consensus.nAdamDualTrackHeight` (Mainnet/Testnet'te blok `200`, Regtest'te blok `1`) yüksekliğinde etkinleşir.
* Mevcut `Coin-Lock` (`>= 250 KRISTA`) ve fonlu `PoW-Lock` (`>= 10 KRISTA`) betikleriyle tam uyumludur.

## Referans Uygulama
* Kota sabitleri ve derleme/çalışma anı doğrulamaları: `src/adam.h` ve `src/chainparams.cpp`.
* Çift kulvarlı seçim ve havuz sınıflandırması: `src/adam.cpp` içindeki `ElectDualTrackValidators`, `SelectAdamNodes`, `IsCoinLockValidatorKey` ve `GetAdamMinerPool`.
* Kayıt işlemlerinin mempool ve blok doğrulaması: `src/main.cpp` ve `src/rpc/mining.cpp`.
