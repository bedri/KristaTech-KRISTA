```
KTIP: 0009
Başlık: Çift Yöntemli Madenci Kaydı ve Çift Kulvarlı (8 + 2 + 5 Dengeli Model D / 7 + 1 + 3 Model D Öncesi) Komite Kota Seçimi
Yazar: KristaTech Çekirdek Geliştiricileri
Durum: Aktif
Tür: Standart Takip (Konsensüs)
Oluşturulma Tarihi: 2026-08-15
```

## Özet
Bu öneri, ADAM konsensüs katmanındaki zincir üstü **Çift Yöntemli Madenci Kaydı** (`Coin-Lock` ve `PoW-Lock`) ile **Çift Kulvarlı Komite Kota Seçimi (`ElectDualTrackValidators`)** mekanizmalarını tanımlar: Model D öncesinde **11 koltuk (`7 + 1 + 3`)** olarak işler ve **Dengeli Model D (`UPGRADE_MODELD`: Mainnet blok `5.000` / Testnet `500` / Regtest `200`)** ile **15 koltuğa (`8 + 2 + 5`)** genişler. Model D altında 15 doğrulayıcı koltuğunun **1. Kulvar (Teminatlı Havuz — 10 Koltuk: 8 Aktif Masternode [`4.200 KRISTA`, `%32`] + 2 Coin-Lock [`250 KRISTA`, `%8`])** ve **2. Kulvar (Açık PoW Havuzu — 5 Koltuk, `%20`: 3 Fonlu PoW-Lock [`10 KRISTA`, `%12`] + 2 Kalıcı Ücretsiz Giriş [`0 KRISTA`, `%8`])** olarak ayrılmasıyla; düşük maliyetli PoW kayıtlarının yüksek teminatlı doğrulayıcıları dışlaması veya `15`'te `8` çoğunluk eşiğine tek başına ulaşması matematiksel olarak engellenirken, izinsiz açık madencilik katılımı korunur.

## Motivasyon
Kayıtlı her açık anahtarın teminat kademesinden bağımsız olarak eşit $1/P$ seçilme olasılığına sahip olduğu düz bir aday havuzunda, bir saldırgan düşük maliyetli `PoW-Lock` kimlikleriyle havuzu doldurarak `15` (Model D öncesinde `11`) doğrulayıcı koltuğunun çoğunluğunu ele geçirebilir; böylece çözümleri saklayarak blok üretimini durdurabilir veya Masternode teminatı kilitlemeden katılımcı ödüllerini domine edebilir.

Buna karşılık, her katılımcıdan 4.200 KRISTA veya 250 KRISTA teminat istemek, başlangıç bakiyesi olmayan yeni CPU/GPU madencilerinin ağa katılmasını imkansız hale getirir.

`KTIP-0009` bu dengeyi iki mekanizmayla kurar:
1. **Sıkılaştırılmış Çift Yöntemli Madenci Kaydı**: Kayıt işlemleri üzerinde katı teminat, girdi onay derinliği ve blok başına işlem sınırı kuralları uygular.
2. **Çift Kulvarlı ($8 + 2 + 5$ Dengeli Model D / $7 + 1 + 3$ Model D Öncesi) Komite Kota İzolasyonu**: Model D altında `8` koltuğu Aktif Masternode'lara (`4.200 KRISTA`), `2` koltuğu Coin-Lock doğrulayıcılara (`250 KRISTA`) ve `5` koltuğu Açık PoW madencilerine (`3` Fonlu `10 KRISTA` PoW-Lock + `2` Kalıcı Ücretsiz Giriş `0 KRISTA`) ayırır (Model D öncesi `7 + 1 + 3`'ten genişler) ve bu dağılımı hem derleme anında (`static_assert`) hem de çalışma anında (`assert`) zorunlu kılar.

## Teknik Özellikler

### 1. Çift Yöntemli Madenci Kaydı (`registerminer`)

#### A. Coin-Lock Kaydı (`250 KRISTA`)
* **Kilit Miktarı**: 33 baytlık sıkıştırılmış açık anahtar ile `nValue >= MINER_REGISTRATION_LOCK_AMOUNT` (`250 * COIN`) şarttır. Coin-Lock betik biçiminde (`MatchCoinLockRegistration`) 250 KRISTA'dan az bakiye kilitleyen çıktılar mempool politikası (`AcceptToMemoryPoolWorker` içinde `bad-txns-coinlock-amount-too-small`) tarafından reddedilir ve aktif havuz taramasında (`GetAdamMinerPool`) yok sayılır.
* **Anti-Sybil Girdi Olgunluğu, Hız Sınırları ve Cüzdan Koruması**: Tüm işlem girdileri (`vin`) en az **`ADAM_COIN_LOCK_MIN_INPUT_DEPTH = 100` onaya** sahip UTXO'ları harcamalıdır (mempool ve `ConnectBlock` içinde `bad-txns-coinlock-input-immature`); konsensüs (`CheckBlock`), mempool, `CreateNewBlock` ve `ExtractBlockMinerRegEntries` işlem başına en fazla **1 adet `Coin-Lock` çıktısı** (`bad-txns-too-many-coinlock`) ve blok başına en fazla **`ADAM_MAX_COIN_LOCK_TX_PER_BLOCK = 1` adet `Coin-Lock` işlemi** (`bad-blk-too-many-coinlock`) kabul eder; cüzdan/RPC/GUI katmanı ise **cüzdan başına en fazla 1 aktif `Coin-Lock` koltuğuna** izin verir (Mainnet'te son `240`, Model D sonrası Testnet/Regtest'te son `15` blokta yenilenebilir).
* **Aktivasyon**: Konsensüs ve manuel `registerminer lock` komutu Mainnet, Testnet ve Regtest üzerinde **tüm blok yüksekliklerinde** `250 KRISTA` Coin-Lock kabul ederken, arka plan `AutoRegisterMiner` bootstrap ödüllerini likit tutmak amacıyla Model D aktivasyonunu (`IsModelDActive`: Mainnet'te `nHeight >= 5.000`, Testnet'te `500`, Regtest'te `200`) bekler ve sonrasında `Coin-Lock` yolunu yalnızca cüzdanın kendi süresi dolan `Coin-Lock` koltuğunu yenilerken veya Model D altında `Alt-Kulvar 1B`'de `< 2` aktif/bekleyen `Coin-Lock` koltuğu varken (Model D öncesinde `< 1`) ve `2. Kulvar` halihazırda en az `nFundedPoWTarget = 3` aktif Fonlu `PoW-Lock` doğrulayıcısı (`nOpenPoWQuota - nZeroMax`) barındırırken otomatik seçer.
* **Kilit Süresi**: `nLockHeight - nTxHeight >= nRegPeriod` (Mainnet üzerinde `2.880` blok; Testnet/Regtest üzerinde Model D aktifleştiğinde `100` blok, Model D öncesinde `2.880` blok).

#### B. PoW-Lock Kaydı (`10 KRISTA` Fonlu / `0 KRISTA` Zero-Coin Bootstrap)
* **Çevrimdışı PoW Bulmacası (`MatchPoWLockRegistration`)**: Ata bloğa bağlı `<4-bayt LE32 nonce> <32-bayt challenge blockHash> <33-bayt sıkıştırılmış minerPubKey> OP_DROP OP_DROP OP_DROP` verisini içerir:
  - **Fonlu `PoW-Lock` (`10 KRISTA`, Blok `1`'de `UPGRADE_HARDENING_V2` ile `HashX11KVS` üzerinde birleştirilmiş)**: Son `100` blok içindeki bir ata bloğa (`pindexChallenge->nHeight >= nHeight - 100`) bağlı 7 seviyeli özyinelemeli **`HashX11KVS("KRPL" || challenge || 33-bayt sıkıştırılmış minerPubKey || "POWLKv2" || LE32(nNonce))`** (`ComputeMinerPoWRegistrationHash`) özetinin `GetMinerPoWLimit(..., true)` (Mainnet'te **15-bit** [`~14,8 sn` CPU], Testnet'te **13-bit**, Regtest'te **8-bit** öncü sıfır) hedefini karşılamasını gerektirir; hem mempool kabulünde (`bad-txns-powlock-invalid` / `bad-txns-powlock-stale-challenge`) hem de konsensüs havuz taramasında (`ExtractBlockMinerRegEntries`) doğrulanır.
  - **Zero-Coin `PoW-Lock` (`0 KRISTA`, Blok `1`'de `UPGRADE_HARDENING_V2` ve `UPGRADE_FAIRNESS` ile güçlendirilmiş)**: Zincir üstünde son **`ADAM_ZEROCOIN_CHALLENGE_WINDOW_V2 = 10` blok** (mempool kabulünde son `8` blok) içindeki bir ata bloğa bağlı 7 seviyeli özyinelemeli **`HashX11KVS("KRZC" || challenge || 33-bayt sıkıştırılmış minerPubKey || "SYBILV2" || LE32(nNonce))`** (`ComputeZeroCoinRegistrationHash`) özetinin `GetZeroCoinPoWLimit` (Fairness altında Mainnet'te Fonlu PoW-Lock ile eşit **15-bit** [`0001ffff...`], Testnet'te **12-bit**, Regtest'te **8-bit** sıfır öneki) hedefini karşılamasını gerektirir.
* **Fonlu `PoW-Lock` (`10 KRISTA`)**:
  - `nValue >= POW_LOCK_REGISTRATION_AMOUNT` (`10 * COIN`) kilitlenmelidir (mempool ve `CheckBlock` içinde `bad-txns-powlock-amount-too-small` ile zorunludur).
  - Tüm işlem girdileri (`vin`) en az **`ADAM_POW_LOCK_MIN_INPUT_DEPTH = 100` onaya** sahip UTXO'ları harcamalıdır (mempool ve `ConnectBlock` içinde `bad-txns-powlock-input-immature`); böylece çoklu kimlik üretmek için hızlı UTXO bölme saldırıları engellenir.
  - İşlem başına en fazla **1 adet `PoW-Lock` çıktısı** (`bad-txns-too-many-powlock`) ve blok başına en fazla **`ADAM_MAX_POW_LOCK_TX_PER_BLOCK = 1`** adet fonlu `PoW-Lock` işlemi (`bad-blk-too-many-powlock`, blok başına `1` olan `Zero-Coin PoW` sayacından bağımsız) kabul edilir; cüzdan/RPC/GUI katmanı da **cüzdan başına en fazla 1 aktif `PoW` koltuğu** kuralını uygular (`0 -> 10 KRISTA` terfisi her an serbest; fonlu yenileme penceresi son `240` / `15` blok).
* **Zero-Coin / Kalıcı Open-Entry `PoW-Lock` (`0 KRISTA`)**:
  - Başlangıçta yalnızca **fonlu** doğrulayıcı havuzunda (`GetFundedAdamMinerPoolCount`: Aktif Masternode'lar + `250 KRISTA` Coin-Lock + `10 KRISTA` Fonlu PoW-Lock) **`GetAdamMinersCount(h) + 2` adetten az fonlu doğrulayıcı (Model D'de `17`, Model D öncesinde `13`)** bulunduğunda sentinel `0x01:0` girdili `nValue == 0` kaydına (blok başına en fazla `1` adet, `bad-blk-too-many-zerocoin-pow`) izin verilirken; `nHeight >= consensus.nAdamOpenEntryHeight` (`2.500` Mainnet, `1` Testnet/Regtest) itibarıyla `0 KRISTA` **Open-Entry** kaydı fonlu havuz büyüklüğünden bağımsız biçimde **Model D altında `2` adet ayrılmış 2. Kulvar koltuğu (Model D öncesinde `1` koltuk)** için **kalıcı olarak açılır**.
  - `UPGRADE_HARDENING_V2` ve `UPGRADE_FAIRNESS` kapsamında aktif `Zero-Coin / Open-Entry PoW` koltukları **`ADAM_ZEROCOIN_POW_REG_PERIOD_V2 = 240` blok** (Mainnet'te `~2 saat`; Testnet/Regtest'te Model D sonrasında `50` blok) azami kiralama ömrüne (`nEffectiveExpiry = min(lockTime, nTxHeight + nZeroActivePeriod)`) sahiptir, Fairness altında **`MAX_ZEROCOIN_BOOTSTRAP_MINERS_FAIRNESS = 240`** en yeni benzersiz anahtar kapasitesine (`16` eski V2) çıkarılmıştır, Blok `1..199` Klasik PoW bootstrap madencilerini Zero-Coin kayıtlarından önce korur, halihazırda aktif olan anahtarların mükerrer mempool yenilemelerini (`bad-txns-zerocoin-key-already-active`) reddeder ve cüzdanda olgunlaşmış bakiye oluştuğunda `AutoRegisterMiner` ile otomatik olarak Fonlu `10 KRISTA` / `250 KRISTA` kilidine terfi ettirilir. Tüm kayıt kilitlerinin ayrıca katı blok yüksekliği CLTV (`< LOCKTIME_THRESHOLD = 500.000.000`, `<= nHeight + 5.760`) kullanması zorunludur.

---

### 2. Çift Kulvarlı ($8 + 2 + 5$ Dengeli Model D / $7 + 1 + 3$ Model D Öncesi) Komite Kota Seçimi (`ElectDualTrackValidators`)
`nHeight >= consensus.nAdamDualTrackHeight` (Mainnet/Testnet'te `UPGRADE_ADAM` ile eşleşen blok `200`, Regtest'te blok `1`) olduğunda, `SelectAdamNodes` (`src/adam.cpp`) aday havuzunu (seçilen Koordinatör hariç) $\text{SHA256D}(\text{Seed}_H \mathbin{\Vert} \text{PubKey}_i)$ puanına göre sıralanmış deterministik alt havuzlara ayırır — Model D öncesinde **11 koltuk (`7 + 1 + 3`)**, `UPGRADE_MODELD` (Mainnet blok `5.000` / Testnet `500` / Regtest `200`) sonrasında ise **15 koltuk (`8 + 2 + 5` Dengeli Model D)** düzenini uygular:

| Kulvar / Alt-Kulvar | Uygun Adaylar | Hedef Kota (Model D / Model D Öncesi) | Derleme / Çalışma Anı Değişmezi |
| :--- | :--- | :---: | :--- |
| **Alt-Kulvar 1A (Masternode'lar)** | Aktif Masternode'lar (`4.200 KRISTA` + son 2.880 blokta ping) | **8 Koltuk (`%32`)** / Öncesi `7` | `ADAM_MODELD_MASTERNODE_TRACK_QUOTA = 8` (Öncesi `7`) |
| **Alt-Kulvar 1B (Coin-Lock)** | Aktif `Coin-Lock` Doğrulayıcılar (`250 KRISTA` CLTV, birincil koltuklarla izole) | **2 İzole Koltuk (`%8`)** / Öncesi `1` | `ADAM_MODELD_COIN_LOCK_TRACK_QUOTA = 2` (Öncesi `1`) |
| **1. Kulvar Toplamı (Teminatlı)** | Alt-Kulvar 1A + Alt-Kulvar 1B (`< 2` Coin-Lock Aktif MN'e taşar; `< 8` MN önce Açık PoW'a taşar) | **10 Koltuk (koltukların `%66,7`'si / ödülün `%40`'ı)** / Öncesi `8` (`%72,7`) | `ADAM_MODELD_COLLATERALIZED_TRACK_QUOTA = 10` (Öncesi `8`) |
| **2. Kulvar (Açık PoW)** | `nAdamOpenEntryHeight` (`2.500` Mainnet / `1` Testnet & Regtest) itibarıyla **3 koltuk** `Kademe 2A` (`10 KRISTA` `PoW-Lock`, `%12`, boşta ise Aktif MN / Bootstrap'a devredilir) + **2 kalıcı koltuk** `Kademe 2C` (`0 KRISTA` Open-Entry, `%8`, Fairness altında azami `240`, boşta ise Fonlu PoW-Lock / Bootstrap'a devredilir); Model D öncesinde `2` Fonlu + `1` Ücretsiz Giriş; aktivasyon öncesinde katı `2A -> 2B -> 2C` | **5 Koltuk (koltukların `%33,3`'ü / ödülün `%20`'si)** / Öncesi `3` (`%27,3`) | `ADAM_MODELD_OPEN_POW_TRACK_QUOTA = 5`, `ADAM_MODELD_ZERO_COIN_OPEN_SEATS = 2` (Öncesi `3` / `1`) |

#### Derleme Anı ve Çalışma Anı Değişmezleri (Invariants)
`src/adam.h` ve `src/chainparams.cpp` içerisinde:
```cpp
// Model D Öncesi (11 koltuk: 7 + 1 + 3)
static_assert(ADAM_MASTERNODE_TRACK_QUOTA + ADAM_COIN_LOCK_TRACK_QUOTA == ADAM_COLLATERALIZED_TRACK_QUOTA,
              "Collateralized sub-track quotas must sum to ADAM_COLLATERALIZED_TRACK_QUOTA");
static_assert(ADAM_MASTERNODE_TRACK_QUOTA >= Consensus::Params::ADAM_MAX_CONSENSUS_THRESHOLD,
              "Masternode track quota must be at least ADAM_MAX_CONSENSUS_THRESHOLD so Masternodes alone can always form a quorum");
assert(ADAM_MASTERNODE_TRACK_QUOTA >= consensus.nAdamThreshold);

// Dengeli Model D (15 koltuk: 8 + 2 + 5)
static_assert(ADAM_MODELD_MASTERNODE_TRACK_QUOTA + ADAM_MODELD_COIN_LOCK_TRACK_QUOTA == ADAM_MODELD_COLLATERALIZED_TRACK_QUOTA,
              "Model D collateralized sub-track quotas must sum to ADAM_MODELD_COLLATERALIZED_TRACK_QUOTA");
static_assert(ADAM_MODELD_COLLATERALIZED_TRACK_QUOTA + ADAM_MODELD_OPEN_POW_TRACK_QUOTA == ADAM_MODELD_MINERS_COUNT,
              "Model D track quotas must sum to ADAM_MODELD_MINERS_COUNT");
static_assert(ADAM_MODELD_MASTERNODE_TRACK_QUOTA >= Consensus::Params::ADAM_MODELD_MAX_CONSENSUS_THRESHOLD,
              "Model D Masternode track quota must be at least ADAM_MODELD_MAX_CONSENSUS_THRESHOLD so Masternodes alone can form a quorum");
static_assert(ADAM_MODELD_MIN_RELAXED_THRESHOLD > ADAM_MODELD_COIN_LOCK_TRACK_QUOTA + ADAM_MODELD_OPEN_POW_TRACK_QUOTA,
              "Model D post-bootstrap relaxed threshold floor must exceed non-Masternode seats so at least 1 Masternode is always required");
assert(Consensus::Params::ADAM_MODELD_MASTERNODE_TRACK_QUOTA >= consensus.nAdamThresholdModelD);
```
Model D altında `8 >= nAdamThresholdModelD (8)` (ve Model D öncesinde `7 >= nAdamThreshold (7)`) olduğundan, Masternode olmayan `7` koltuğun tamamı (`2 Coin-Lock + 5 Açık PoW`) çözüm göndermese dahi `8` Aktif Masternode koltuğu bootstrap sonrası çoğunluk eşiğini tek başına karşılar.

#### Katı Kota İzolasyonu, Sıralı Taşma, Eşit Koltuk Ödemesi ve Orantılı Sermaye Koordinatör VRF (`4.200 : 250 : 10`)
1. **Kesin `2` Koltuklu `Coin-Lock` İzolasyonu (Model D Öncesi `1`)**: `Alt-Kulvar 1B` Model D altında en fazla **`2`** (öncesinde `1`) adet `Coin-Lock` doğrulayıcısı seçer. Seçilemeyen `Coin-Lock` anahtarları Açık PoW'un önünde `Alt-Kulvar 1A`'ya taşmaz ve 2. Kulvar `Kademe 2A` (`vFundedPoWRanked`) havuzuna girmez. Model D altında `2`'den az `Coin-Lock` adayı varsa (öncesinde `< 1`), boş koltuklar anında sıradaki Aktif Masternode'lara (`4.200 KRISTA`) devredilir.
2. **Sıralı Boş Koltuk Taşması ve Yedek Önceliği**: `15` kişilik komitede (Model D öncesi `11`) boş kalan tüm koltuklar katı sırayla doldurulur: **kalan Aktif Masternode'lar (`4.200 KRISTA`) $\rightarrow$ kalan Açık PoW (`10 KRISTA` `PoW-Lock` $\rightarrow$ `Bootstrap` $\rightarrow$ `Zero-Coin PoW`) $\rightarrow$ seçilemeyen `Coin-Lock` (`250 KRISTA`, yalnızca toplam havuz Model D'de `< 15` / öncesinde `< 11` olduğunda son çare canlılık yedeği)**. Yedek sıralaması da bire bir aynı önceliği izler.
3. **Eşit Emek → Eşit Kazanç ve Orantılı Kilitli Sermaye Koordinatör VRF (`ADAM_FAIR_COORD_V5` / `V4`)**: `UPGRADE_FAIRNESS` (Yükseklik `1`) kapsamında `ComputeModelDValidatorSeatPayments` Aktif Doğrulayıcı havuzunu kulvar ayrımı olmadan Model D altında tüm `15` koltuk arasında **eşit böler (`%60 / 15 = %4,00`; Dönem 0'da `15/15` çözümlemede `0` satoshi artık ile `0,6324 KRISTA`/koltuk)**; Model D öncesinde ise `11` koltuk arasında (`%75 / 11 ≈ %6,818`) eşit paylaştırır. `SelectAdamNodes` ise birincil Koordinatörü kulvar içi temsilci tavanı olmaksızın tüm fonlu doğrulayıcılar arasından (**Aktif MN için `4.200`, `Coin-Lock` için `250`, `Fonlu PoW-Lock` için `10` ağırlık**) seçer; **`UPGRADE_MODELD` aktifken `ADAM_FAIR_COORD_V5` ile `11 * sumNonMN > 9 * sumMN` olduğunda kesin `>= %55` Aktif MN güvenlik tabanını** uygular (Model D öncesinde `ADAM_FAIR_COORD_V4` ile `3 * sumNonMN > sumMN` iken `>= %75`; eski `65:8:2` ağırlığının yerini alır).

## Geriye Dönük Uyumluluk
* `consensus.nAdamDualTrackHeight` (Mainnet/Testnet'te blok `200`, Regtest'te blok `1`) yüksekliğinde etkinleşir.
* Mevcut `Coin-Lock` (`>= 250 KRISTA`) ve fonlu `PoW-Lock` (`>= 10 KRISTA`) betikleriyle tam uyumludur.

## Referans Uygulama
* Kota sabitleri ve derleme/çalışma anı doğrulamaları: `src/adam.h` ve `src/chainparams.cpp`.
* Çift kulvarlı seçim ve havuz sınıflandırması: `src/adam.cpp` içindeki `ElectDualTrackValidators`, `SelectAdamNodes`, `IsCoinLockValidatorKey` ve `GetAdamMinerPool`.
* Kayıt işlemlerinin mempool ve blok doğrulaması: `src/main.cpp` ve `src/rpc/mining.cpp`.
