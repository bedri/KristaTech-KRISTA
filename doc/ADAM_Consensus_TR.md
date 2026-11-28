# ADAM (A Decentralized Approach Model) İşbirlikçi Konsensüsü

## 1. Giriş ve Arka Plan

**ADAM (A Decentralized Approach Model)** konsensüs mekanizması; blok zincirini madencilik merkezileşmesine, bencil madenciliğe (selfish mining), blok grinding işlemlerine ve blok liderlerine yönelik hedefli Denial-of-Service (DoS) saldırılarına karşı güçlendirmek için tasarlanmış işbirlikçi bir hibrit konsensüs modelidir. 

Geleneksel Proof-of-Work (PoW) ve Proof-of-Stake (PoS) protokolleri belirgin güvenlik açıklarına sahiptir:
* PoW modelinde, devasa işlem (hashing) gücüne sahip madenciler blok üretimine hakim olur ve bu durum konsensüsü merkezileştiren havuzların oluşmasına yol açar.
* PoS modelinde, en zengin stake eden cüzdanların blok üretme olasılığı orantısız şekilde daha yüksektir.
* Her iki modelde de, bir sonraki blok üreticisinin kimliği önceden biliniyorsa (önceden seçilmişse), DoS saldırılarının veya rüşvet girişimlerinin hedefi haline gelirler. Eğer blok içerikleri bir sonraki bloğun rastgeleliğini değiştirecek şekilde manipüle edilebiliyorsa, madenciler gelecekteki lider seçimlerini saptırmak için *grinding saldırıları* gerçekleştirebilirler.

ADAM, blok üretim sürecini bir **İşbirlikçi Hibrit Turuna** bölerek bu sorunları çözer:
1. **Seçim Aşaması (Election Phase)**: Her blok slotu için, sözde rastgele bir seed deterministik olarak **$N$ adet Madenci (Miners)** ve **1 adet Koordinatör (Coordinator)** havuzunu seçer.
2. **Çözme Aşaması (Solving Phase)**: Seçilen Madenciler, mevcut bloğun hedefine bağlı olarak hafif, paralel hale getirilmiş PoW bulmacalarını (kısmi çözümler) derler ve çözer.
3. **Birleştirme Aşaması (Aggregation Phase)**: Seçilen Koordinatör kısmi çözümleri toplar, doğrular, bunları bir blok şablonu (block template) halinde paketler, kendi gizli anahtarını (private key) kullanarak nihai blok başlığını (block header) imzalar ve bunu ağa yayınlar.

---

## 2. Konsensüs Mimarisi ve Parametreleri

ADAM konsensüsü, blok yüksekliğine bağlı olarak koşullu bir şekilde etkinleştirilir. Temel parametreler `src/consensus/params.h` dosyasında tanımlanmıştır ve ağ bazında `src/chainparams.cpp` dosyasında yapılandırılmıştır.

Aktif Masternode sayısının düşük olduğu durumlarda ağın sorunsuz bir şekilde bootstrap (ilk kurulum) yapabilmesini sağlamak için ADAM, konsensüs yükseltme yükseklikleriyle deterministik olarak denetlenen iki blok sürümü moduna sahiptir.

### A. Fallback Mode (Geri Çekilme Modu - Blok Versiyon 11)
Fallback Mode, ağın bootstrap aşaması (`UPGRADE_ADAM` $\le H <$ `UPGRADE_POMBL`) için tasarlanmıştır. Aktif Masternode kaydı gerektirmeden çalışır ve tamamen kendi kendine yeten (self-contained) bir yapıdadır.
* **Madenci Sayısı ($N$)**: `vAdamMiners.size()` değeri `threshold` ile `GetAdamMinersCount(nHeight) + 3` arasında olmak üzere $K-1$ boyutundaki anahtar havuzuna göre dinamik olarak belirlenir (Mainnet/Testnet'te Model D öncesinde $11 \le K \le 14$, Regtest `200..299` gibi `UPGRADE_POMBL` öncesi Model D aktifliğinde ise en fazla $18$; anahtarlar deterministiktir ve seçilen Koordinatör `vAdamMiners` dizisinin son elemanı olarak eklenir).
* **Konsensüs Eşiği ($T$)**: `GetAdamThreshold(nHeight, nActiveMasternodes)` tarafından yönetilir (`nAdamThresholdBootstrapLimit` altında aktif Masternode sayısı `< 8` iken `5`, Model D + `UPGRADE_HARDENING_V4` ile `nActiveMasternodes >= 8` olduğu anda bootstrap döneminde dahi doğrudan **`nAdamThresholdModelD = 8`** [`> 7` MN-dışı koltuk]; bootstrap sonrasında Mainnet/Regtest üzerinde Model D ile **`8`** [Model D öncesinde `nAdamThreshold = 7`], Testnet üzerinde **`3`**).
* **Kendi Kendine Yeten Başlık Düzeni**: `vAdamMiners`, tüm $K$ açık anahtarlarını depolar (ilk $K-1$ seçilen madenciler, son anahtar koordinatördür). `vAdamSolutions` ise $K-1$ kısmi çözümü depolar.

### B. Standart Mod (Blok Versiyon 12)
Standart Mod, 3-permütasyonlu bulmaca özetleme zinciri ile tam işbirlikçi Çoklu Kanıt Algoritması (PoMBL) konsensüs durumunu temsil eder.
* **Madenci Sayısı ($N$)**: `GetAdamMinersCount(nHeight)` aracılığıyla yapılandırılır (`src/consensus/params.h` dosyasında tanımlanmış ve `src/chainparams.cpp` içinde Mainnet `4.800..4.999` / Testnet `400..499` için Model D öncesi `nAdamMinersCount = 11`, Mainnet `5.000` / Testnet `500` / Regtest `200` `UPGRADE_MODELD` aktivasyonuyla birlikte ise **`ADAM_MODELD_MINERS_COUNT = 15`** olarak belirlenmiştir).
* **Konsensüs Eşiği ($T$)**: `GetAdamThreshold(nHeight, nActiveMasternodes)` aracılığıyla yönetilir (`nAdamThresholdBootstrapLimit` altında aktif Masternode sayısı `< 8` iken `5`, Model D + `UPGRADE_HARDENING_V4` ile `nActiveMasternodes >= 8` olduğunda bootstrap aşamasında dahi doğrudan **`nAdamThresholdModelD = 8`** [`> 7` MN-dışı koltuk]; bootstrap sonrasında Mainnet/Regtest üzerinde Model D ile **`8`** [Model D öncesinde `nAdamThreshold = 7`], Testnet için **`3`**).
* **Seçilen Koordinatör**: `UPGRADE_FAIRNESS` (Blok `1`, `SelectAdamNodes`) kapsamında birincil Koordinatör, tüm fonlu doğrulayıcılar arasından kulvar içi temsilci tavanı olmaksızın orantılı kilitli sermaye ağırlıklı VRF (**Aktif MN başına `4.200`, `Coin-Lock` başına `250`, `Fonlu PoW-Lock` başına `10`**) ile seçilir: `UPGRADE_MODELD` altında `11 * sumNonMN > 9 * sumMN` olduğunda **`ADAM_FAIR_COORD_V5` ile kesin `>= %55` Aktif MN güvenlik tabanı** uygulanır (Model D öncesinde ise `3 * sumNonMN > sumMN` iken **`ADAM_FAIR_COORD_V4` ile kesin `>= %75` Aktif MN tabanı** korunur; eski `65 : 8 : 2` V2 tavanlarının yerini almıştır; fonlu doğrulayıcı yoksa Bootstrap $\rightarrow$ Zero-Coin VRF yedeği) ve `15` (Model D öncesinde `11`) kişilik `vAdamMiners` listesinden ayrıdır.

### C. Deterministik Blok Yüksekliği Aktivasyonu (`Consensus::UPGRADE_POMBL`)
Fallback Mode (Versiyon 11) ile Standart Mod (Versiyon 12) arasındaki geçiş, herhangi bir idari anahtara bağımlı olmaksızın doğrudan `Consensus::UPGRADE_POMBL` (`src/chainparams.cpp` içindeki `vUpgrades[Consensus::UPGRADE_POMBL].nActivationHeight`) yüksekliği ile deterministik olarak denetlenir:
* **Aktivasyon Yükseklikleri**: Mainnet üzerinde Blok **`200`**, Testnet üzerinde Blok **`200`** ve Regtest üzerinde Blok **`300`**.
* **Davranış**:
  - `UPGRADE_ADAM <= nHeight < UPGRADE_POMBL` aralığında: Bloklar Fallback Mode kuralları altında **Versiyon 11** olarak üretilir ve doğrulanır.
  - `nHeight >= UPGRADE_POMBL` olduğunda: `ComputeBlockVersion` **Versiyon 12** bloklar üretir; hem `ContextualCheckBlockHeader` hem de `CheckBlock` fonksiyonları `nVersion < 12` olan tüm blokları `bad-version` hatası ile kesin olarak reddeder.

### Ağ Yapılandırmaları ve Aktivasyon Yükseklikleri
İş birlikçi konsensüs çerçevesi ve deterministik Masternode katmanının geçiş yükseklikleri (`src/chainparams.cpp`):
1. **`Consensus::UPGRADE_DIP3`**: `evodb` tabanlı KTDIP3 deterministik Masternode'ları ve Hot/Cold saf operatör BLS ayrımı + ödeme yönlendirmesini etkinleştirir.
2. **`Consensus::UPGRADE_ADAM`**: ADAM Fallback Modunu (Blok Sürümü 11) ve Çift Kulvarlı doğrulayıcı kotalarını etkinleştirir.
3. **`Consensus::UPGRADE_POMBL`**: ADAM Standart Modunu / Çoklu Kanıt Algoritmasını (Multi-Proof-Algorithm - Blok Sürümü 12) etkinleştirir.
4. **`Consensus::UPGRADE_MODELD` ve `UPGRADE_STAKE_MIN_DEPTH_V2`**: Model D ödül dağılımlarını / PoBLS konsensüsünü zorunlu kılar ve Mainnet asgari PoS olgunlaşma derinliğini (`100 -> 600` blok) artırır.
5. **`Consensus::UPGRADE_HARDENING_V4` (`"Hardening_v4"`, Mainnet / Testnet / Regtest üzerinde Yükseklik `1`; `v1.5.9.0`, `PROTOCOL_VERSION = 70983`, `MIN_PEER_PROTO_VERSION = 70983`)**: VRF anahtar olgunluğunu (`ADAM_VRF_KEY_MATURITY = 100`), `nProUpRegCooldownBlocks = 100`, zorunlu `<blsPubKey(48B)> <blsPoP(96B)> OP_2DROP` madenci kayıt sabitlemesini (`"POWLKv4"`/`"SYBILV4"`), `>= 8` MN olduğunda Model D bootstrap eşiği `8`'i, LLMQ `floor(2N/3) + 1` BFT süper çoğunluğunu (`4-of-5..11-of-15`) ve `45s` çifte-imza önlemeyi, PoW `CoinStake` yasağını (`bad-cs-in-pow`), `M-4` PoSe adaletini, R35-1 `5s` birincil çözüm ihmal vetosunu ve kesin `PoM` çalışma süresini (`std::max(nRegisteredHeight, nPoSeRevivedHeight)`) etkinleştirir.

| Ağ | `UPGRADE_DIP3` / `HARDENING_V4` | `UPGRADE_ADAM` / `nAdamCoordWeightV2Height` | `UPGRADE_ADAM_V2` / `nAdamOpenEntryHeight` | `UPGRADE_POMBL` (Std V12) | `UPGRADE_MODELD` / `STAKE_DEPTH_V2` | Hedef Blok Süresi |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Mainnet** | Blok 1 / Blok 1 | Blok 200 (`DualTrack = 200`) / Blok 1 | Blok 5.000 / Blok 2.500 | Blok 4.800 | Blok 5.000 / Blok 5.001 | 30 saniye |
| **Testnet** | Blok 1 / Blok 1 | Blok 200 (`DualTrack = 200`) / Blok 1 | Blok 500 / Blok 1 | Blok 400 | Blok 500 / Blok 5.001 | 30 saniye |
| **Regtest** | Kapalı (`NO_ACTIVATION`) / Blok 1 | Blok 200 (`DualTrack = 1`) / Blok 1 | Blok 200 / Blok 1 | Blok 300 | Blok 200 / Kapalı | 10 saniye |

### D. Bulmaca Zorluğu Bit Kaydırma (Bit-Shift) Parametreleri
Farklı blok yüksekliklerinde dinamik ve yapılandırılabilir zorluk ölçeklendirmesini korurken, ağ üzerinde bulmaca çözümlerinin "yanlış pozitif seline" (false-positive flood) yol açmasını önlemek için ADAM, `Consensus::Params` (`src/adam.cpp` ve `src/rpc/mining.cpp`) içinde tanımlanan zorluk bit kaydırma çarpanlarını uygular:
* **V1 Zorluk Kaydırması (`nAdamDifficultyShiftV1`)**: Fallback Modunda (`nVersion == 11` veya `Consensus::UPGRADE_MODELD` öncesinde) hedef zorluğu gevşeten bit kaydırma çarpanı. Değer: `10` (Mainnet, Testnet ve Regtest üzerinde).
* **V2 Zorluk Kaydırması (`nAdamDifficultyShiftV2`)**: Standart Modda (`nVersion >= 12`) hem `Consensus::UPGRADE_ADAM_V2` hem de `Consensus::UPGRADE_MODELD` aktif olduğunda hedef zorluğu gevşeten bit kaydırma çarpanı. Değer: `6` (Mainnet, Testnet ve Regtest üzerinde).
* **Etkin Geçiş Yüksekliği**: `nVersion == 11` veya `!NetworkUpgradeActive(nHeight, Consensus::UPGRADE_MODELD)` iken `fFallbackMode` aktif kaldığı için, üretim ortamında `activeShift` değeri Mainnet üzerinde **Blok 5.000**, Testnet üzerinde ise **Blok 500** itibarıyla `10` (`V1`) değerinden `6` (`V2`) değerine geçer (Regtest ortamı ise otomatik testler için `VerifyAdamSolution` içinde hedef sınırını atlar ve `scaledTarget = ~UINT256_ZERO` değerini kullanır).

Mainnet ve Testnet üzerinde bulmaca doğrulaması sırasında `scaledTarget` değeri, konsensüs hedefinin (`Target`) `activeShift` kadar kaydırılıp `powLimit` ile sınırlandırılmasıyla elde edilir:

$$\text{scaledTarget} = \min\left(\text{Target} \ll \text{activeShift}, \text{powLimit}\right)$$

* `nVersion == 11`, `!NetworkUpgradeActive(nHeight, Consensus::UPGRADE_MODELD)` veya `!NetworkUpgradeActive(nHeight, Consensus::UPGRADE_ADAM_V2)` ise $\text{activeShift} = \text{nAdamDifficultyShiftV1}$ (`10`).
* `nVersion >= 12` ve hem `Consensus::UPGRADE_MODELD` hem de `Consensus::UPGRADE_ADAM_V2` aktif ise $\text{activeShift} = \text{nAdamDifficultyShiftV2}$ (`6`).

### E. Başlangıç Zorluk Limiti (powLimit)
1–199 arasındaki blokların çok hızlı kazılmasını (bu durum çatallanmalara ve kuorum kilitlenmelerine yol açıyordu) önlemek için, başlangıç zorluk hedefi `powLimit` şu şekilde ayarlanmıştır:

$$
\text{powLimit} = ({\sim}\text{UINT256}\underline{\hspace{0.4em}}\text{ZERO}) \gg 20
$$

Mainnet ve Testnet üzerinde bu değer tam olarak `1/2^20`'dir (genesis bloğunun `0x1e0ffff0` olan `nBits` değerine eşdeğerdir). Bu, blokların genesis'ten itibaren doğal olarak yaklaşık 30 saniye aralıklarla yerleşmesini sağlayarak düğümlerin (nodes) kararlı P2P bağlantıları kurmasına ve birleşik bir zincir ucunu (chain tip) sürdürmesine olanak tanır.

---

## 3. Doğrulanabilir Rastgele Fonksiyon (VRF) ve Rolling Seed Yapısı

Madencilerin blok $H$ hash'ini manipüle etmek için işlemleri veya nonce'ları değiştirmesini ve böylece blok $H+1$ için yapılacak lider seçimini saptırmasını (yani **grinding saldırılarını**) önlemek amacıyla ADAM, bir **Doğrulanabilir Rastgele Fonksiyon (VRF) Rolling Seed** modeli uygular.

### Matematiksel Formülasyon
ADAM ağ yükseltmesinin (`Consensus::UPGRADE_ADAM`) aktif olduğu herhangi bir $H$ blok yüksekliği için:

$$\text{Seed}_H = \text{Hash}\left(\text{Seed}_{H-1} \mathbin{\Vert} \text{VRFProof}_{H-1}\right)$$

Burada:
* $H$ yüksekliğindeki seçim seed'i ($\text{Seed}$), blok $H+1$ için liderleri belirlemek üzere kullanılır.
* $H-1$ yüksekliğindeki VRF kanıtı ($\text{VRFProof}$), $H-1$ bloğunun seçilen **Koordinatörü** tarafından $H-2$ yüksekliğindeki önceki seed üzerinde üretilen deterministik imzadır: kayıtlı **KTDIP3** Masternode'lar (`UPGRADE_DIP3 = 1` Mainnet ve Testnet) doğrudan zincir üstü kayıtlı `blsPubKeyOperator` anahtarıyla imzalarken (`SignBLSOperatorOnly` / `VerifyAdamOrDip3Signature`, boş `ecdsaSig`), DIP3 öncesi ve MN dışı doğrulayıcılar hibrit BLS12-381 + ECDSA fallback mekanizmasını (`SignBLSWithECDSAFallback` / `VerifyBLSWithECDSAFallback`) kullanır. `UPGRADE_HARDENING_V4` (Yükseklik `1`) altında tüm Coin-Lock, Fonlu PoW-Lock ve Zero-Coin madenci kayıtları çıktı betiklerinin başına `<blsPubKey(48B)> <blsPoP(96B)> OP_2DROP` eklemek ZORUNDADIR (`bad-txns-minerreg-missing-bls`, `bad-txns-minerreg-bls-pop`, `bad-txns-minerreg-dup-bls`), `VerifyAdamOrDip3Signature` imzalamayı yalnızca o kayda sabitlenen BLS anahtarıyla sınırlar ve **`ADAM_VRF_KEY_MATURITY = 100` blok** (`src/adam.h`) ile `consensus.nProUpRegCooldownBlocks = 100` (`nOperatorKeyHeight` üzerinden `ProUpRegTx` operatör anahtar rotasyonlarında `bad-proupreg-cooldown`) yeni kayıtlı veya döndürülmüş anahtarların olgunlaşmadan Koordinatör VRF / PoBLS yedek önceliğine girmesini önler.
* Hibrit şemada Koordinatör BLS açık anahtarını ECDSA özel anahtarıyla yetkilendirirken (ve V4 altında `48` baytlık BLS anahtarı + `96` baytlık PoP doğrudan kayıt çıktısına ve `blockHash` + aktif-zincir yüklemi izleyen çatal-farkındalıklı `PinnedBLSEntry` yapısına sabitlenir), KTDIP3 `blsPubKeyOperator` anahtarını doğrudan `ProRegTx` / `evodb` içinde bağlar. Her iki yol da verilen seed için tekil ve deterministik bir BLS imzası üretir; bu da rolling seed yapısını Koordinatör imzalayıp yayınlayana kadar tamamen değiştirilemez ve tahmin edilemez kılar.

### Düğüm Seçimi (SSLE)
Madencilerin ve koordinatörün seçimi `src/adam.cpp` içindeki `SelectAdamNodes()` tarafından gerçekleştirilir:
1. Aktif düğüm havuzu (kayıtlı Masternode listesi ve Coin-Lock veya PoW-Lock aracılığıyla aktif kayıtlı madenciler) derlenir.
2. Seçim havuzu ağa bağlıdır:
   * **Mainnet & Testnet**: Havuz, aktif Masternode'lardan ve aktif kayıtlı madencilerden dinamik olarak oluşturulur. Bununla birlikte, aktif masternode'lar veya kayıtlar tam olarak kurulmadan önce blok üretiminin kararlı kalmasını sağlamak amacıyla, blok yüksekliği bootstrap limitinin altında olduğu sürece (`UPGRADE_FAIRNESS` ile Mainnet üzerinde `nAdamBootstrapLimit` = **2.500** [eski `44.850`], Testnet üzerinde `100.000` blok), ağ, blok 1 ila 199 arasındaki blok üreticilerini (coinbase çıktıları) otomatik olarak tarar ve açık anahtarlarını madenci havuzuna ekler. Bu, erken aşamalarda zincirin durmasını (stall) önler.
   * **Regtest**: Havuz, 15 koltuklu Model D komitesi, Koordinatör ve yedek yuvalar için otomatik testleri kolaylaştırmak amacıyla harici kayıtları otomatik olarak atlar ve 20 adet deterministik bootstrap açık anahtarı (`0..19`) içerir:

     $$
     \text{Pool}_{\text{bootstrap}} = \lbrace \text{DeterministicPubKey}_0, \dots, \text{DeterministicPubKey}_{19} \rbrace
     $$

3. **Rolling seed'e dayanarak seçim havuzundaki her bir düğüm için benzersiz bir hash sırası (hash rank) hesaplanır**:

   $$\text{Rank}_i = \text{Hash}\left(\text{Seed}_H \mathbin{\Vert} \text{PubKey}_i\right)$$

4. **Havuz, $\text{Rank}_i$ değerlerine göre artan düzende sıralanır**.
5. **Çift Kulvarlı Komite Seçimi (Dengeli Model D `8 + 2 + 5` vs Model D Öncesi `7 + 1 + 3`; `nAdamDualTrackHeight` = Mainnet/Testnet üzerinde 200, Regtest üzerinde 1; mevcut bir zincirde etkinleştirilirken daima mevcut yüksekliğin ilerisinde bir blok yüksekliğine $\ge$ `UPGRADE_ADAM` ayarlanmalıdır)**:
   `IsAdamDualTrackActive(H)` aktif olduğunda (`H >= consensus.nAdamDualTrackHeight`), $M = \text{GetAdamMinersCount}(H)$ doğrulayıcı koltuğu (Dengeli Model D ile `15`, öncesinde `11`) katı kota izolasyonu ve sıralı taşma mekanizmasına sahip `ElectDualTrackValidators()` aracılığıyla iki kulvara ayrılır:
   * **Kulvar 1 — Teminatlı Kulvar (Model D'de `ADAM_MODELD_COLLATERALIZED_TRACK_QUOTA = 10` koltuk; öncesinde `ADAM_COLLATERALIZED_TRACK_QUOTA = 8`)**:
     - **Alt-Kulvar 1A (Aktif Masternode'lar — Model D'de `8` koltuk [`%32` doğrulayıcı payı]; öncesinde `7` koltuk)**: **Aktif Masternode'lara (`4.200 KRISTA` teminatlı)** ayrılmıştır. Derleme anında `static_assert(ADAM_MODELD_MASTERNODE_TRACK_QUOTA >= ADAM_MODELD_MAX_CONSENSUS_THRESHOLD)` (`8 >= 8`) ve Model D öncesi `static_assert(ADAM_MASTERNODE_TRACK_QUOTA >= 7)` ile kilitlenmiştir; böylece Aktif Masternode'lar bootstrap sonrasında da (`GetAdamThreshold = 8` Model D / `7` öncesinde) tek başına kuorum oluşturabilir.
     - **Alt-Kulvar 1B (Coin-Lock — Model D'de `2` izole koltuk [`%8`]; öncesinde `1` koltuk)**: Kesin olarak Model D'de **`2` adet `250 KRISTA` Coin-Lock** (`ADAM_MODELD_COIN_LOCK_TRACK_QUOTA = 2`; öncesinde `1` adet `ADAM_COIN_LOCK_TRACK_QUOTA = 1`) doğrulayıcısına ayrılmıştır. Havuzdaki Coin-Lock adayı kotadan azsa boş koltuklar anında kalan Aktif Masternode'lara (`4.200 KRISTA`) devredilir. `AutoRegisterMiner` da Fonlu PoW-Lock'lar dolduktan sonra Model D altında `< 2` Coin-Lock koltuğu (öncesinde `< 1`) koşulunu denetler.
   * **Kulvar 2 — Açık PoW Kulvarı (Model D'de `ADAM_MODELD_OPEN_POW_TRACK_QUOTA = 5` koltuk [toplam `%20`]; öncesinde `ADAM_OPEN_POW_TRACK_QUOTA = 3`)**: Açık PoW madencilerine (`10 KRISTA` CLTV kilitli `PoW-Lock`, `Blok 1..199` bootstrap anahtarları ve `Zero-Coin PoW`) ayrılmıştır. `UPGRADE_HARDENING_V2`, `UPGRADE_FAIRNESS` ve `UPGRADE_HARDENING_V4` (`nActivationHeight = 1`) kapsamında `ElectDualTrackValidators`, Kulvar 2 içinde katı ekonomik öncelik uygular (`Kademe 2A: Fonlu 10 KRISTA PoW-Lock -> Kademe 2B: Blok 1..199 Bootstrap -> Kademe 2C: 0 KRISTA Zero-Coin PoW`; `nAdamOpenEntryHeight` öncesinde `< GetAdamMinersCount(h) + 2` fonlu anahtar sınırı [Model D'de `< 17`, öncesinde `< 13`], `UPGRADE_FAIRNESS` altında `MAX_ZEROCOIN_BOOTSTRAP_MINERS_FAIRNESS = 240` aktif anahtar kapasitesi, V4 `"POWLKv4"`/`"SYBILV4"` [V4 öncesi `"POWLKv2"`/`"SYBILV2"`] altında `Hash(challenge || blsPubKey)` bağlayan Mainnet `15`-bit `HashX11KVS` PoW, `<blsPubKey(48B)> <blsPoP(96B)> OP_2DROP` sabitlemesi, LevelDB `"adamregcache_v3"`, `10` blokluk challenge penceresi ve Mainnet'te `240` / Model D sonrası Testnet & Regtest'te `50` blokluk kiralama ömrü), ve **`nAdamOpenEntryHeight`** (`2.500` Mainnet, `1` Testnet/Regtest) itibarıyla Model D altında **5 koltuğun 2'sini (`%8`)** (`ADAM_MODELD_ZERO_COIN_OPEN_SEATS = 2`, öncesinde `1`) `0 KRISTA` `Zero-Coin PoW` için ayırırken kalan **3 koltuğu (`%12`)** (öncesinde `2`) `10 KRISTA` `Fonlu PoW-Lock` / `Bootstrap` adaylarına tahsis eder. Seçilemeyen `Coin-Lock` anahtarları `Kademe 2A` dışındadır.
   * **Sıralı Boş Koltuk Taşması ve Yedek Önceliği**: `15` kişilik komitede (öncesinde `11`) boş kalan koltuklar katı sırayla doldurulur: **Aktif Masternode (`4.200 KRISTA`) $\to$ Açık PoW (`10 KRISTA` `PoW-Lock` $\to$ `Bootstrap` $\to$ `Zero-Coin PoW`) $\to$ seçilemeyen `Coin-Lock` (`250 KRISTA`, son çare canlılık yedeği)**. Yedek sıralaması da aynı `Aktif MN -> Açık PoW -> Coin-Lock` önceliğini izler.
   * Bu yapı, Model D'de `15` (öncesinde `11`) doğrulayıcı koltuğunun her koşulda doldurulmasını garanti ederken `4.200 KRISTA` Aktif Masternode'ları korur (`8` doğrulayıcı koltuğu + Model D'de `>= %55` Koordinatör VRF tabanı), `250 KRISTA` Coin-Lock doğrulayıcılarına `2` koltuklu alt-kulvar sunar ve Açık PoW madencilerine `5` koltuk (`3` Fonlu + `2` Ücretsiz Giriş) garanti eder.
   * **Bekleme Pencereleri ($3.000\text{ ms}$ / $1.000\text{ ms}$), Yedek Koltuk Devri ($t \ge 60\text{ sn}$) ve Eşit Emeğe Eşit Kazanç (`UPGRADE_FAIRNESS`)**:
     - `UPGRADE_FAIRNESS` kapsamında (Regtest hariç), `CreateNewBlock` eşik sağlandıktan sonra geride kalan komite imzalarını toplamak için **`ADAM_SOLUTION_GRACE_WINDOW_MS = 3.000 ms`** (Model D'de `15/15`, öncesinde `11/11` çözüme kadar) ve LLMQ korum imzalarını toplamak için **`LLMQ_SIG_GRACE_WINDOW_MS = 1.000 ms`** bekleme penceresi uygular.
     - Bir bloğun üretimi $\ge 60\text{ sn}$ gecikirse (`ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS = 60`), sessiz koltuklar yalnızca **aynı kulvardaki** (`MN -> MN`, `Coin-Lock -> Coin-Lock`, `Açık PoW -> Açık PoW`; yedek `poolIdx` yuvaları Model D'de `m >= 15`, öncesinde `m = 11..16`) yedek doğrulayıcılara devredilebilir.
     - **`UPGRADE_FAIRNESS` ve `UPGRADE_HARDENING_V4` (Blok 1)** altında: Blok `200` (`IsAdamValidatorSplitActive`) itibarıyla Model D öncesi (`200..4.999` Mainnet) **`%25` Üretici (PoS'ta `%20` Staker + `%5` Koordinatör) / `%75` Aktif Doğrulayıcılar** (`11` koltukta `~%6,818` / Dönem 0'da `1,07795454 KRISTA` koltuk başı), Dengeli Model D (`5.000+` Mainnet / `500+` Testnet / `200+` Regtest) ise **`%5` Pasif MN (`0,7905 KRISTA`) / `%10` İmzacı Odaklı LLMQ (1-blok gecikmeli, V4 `M-4` PoSe adaleti: imzalama `-5` iyileşme sağlarken LLMQ imzalamama artık `+5` ceza vermez, `1,5810 KRISTA`) / `%25` Üretici (`3,9525 KRISTA`: PoW'da `%25 + nFees` Koordinatör; PoS'ta `%22` Staker [`3,4782 KRISTA` + `%100` `nFees`] + `%3` İmzacı Koordinatör [`0,4743 KRISTA`]) / `%60` Aktif Doğrulayıcılar (`9,4860 KRISTA`, `15` koltukta tam `%4,00` / `0,6324 KRISTA` koltuk başı, `15/15` çözüldüğünde `0` kalan)** uygular. **Eşit Emeğe Eşit Kazanç** ilkesiyle, geçerli 33 baytlık sıkıştırılmış pubkey ile çözüm sunan (`!vAdamSolutions[i].empty()`) her koltuk kulvar ayrımı olmaksızın tam eşit `nPartSplitTotal / GetAdamMinersCount(nHeight)` payı alır (kalan satoshiler yalnızca tüm `15/15` [veya öncesinde `11/11`] koltuklar çözüldüğünde verilir). Çözülmeyen koltuk ve imzasız LLMQ payları (`nUnclaimedPartSplit`) **%50 Otomatik Protokol Yakımı + %50 Geliştirici Hazinesi** olarak bölüştürülürken Koordinatör blok kurtarmada tam üretici payını (`%25` PoW, Model D `%3` / Öncesi `%5` PoS) korur (`M-4` kapsamında yedek ikameyle düşürülen sessiz birincil ADAM komite üyeleri `+5` ve kaçırılan birincil koordinatörler `+5` PoSe cezası alır); ayrıca LLMQ (`qblockprop`, yalnızca düğümün mevcut ucu `msg.block.hashPrevBlock` ile eşleştiğinde) $t \ge 60\text{ sn}$ anındaki birincil koltuk ikame vetosunun (`vAdamMiners[i] != vExpectedMiners[i]`) yanı sıra V4 (R35-1) kapsamında yerelde **`>= 5 sn`** süredir önbellekte bulunan (`mapAdamSolutionFirstSeen`) birincil doğrulayıcı çözümünü atlayan (`vAdamMiners[i] == vExpectedMiners[i] && vAdamSolutions[i].empty()`) önerileri de veto ederek `QUORUMSIGSHARE` imzalamayı reddeder.
     - Blok üretimi $\ge 120\text{ sn}$ tıkanırsa `GetAdamEffectiveThreshold(nHeight, nTimeElapsed, nActiveMasternodes)` eşiği `60 sn` aralıklarla gevşetir: Dengeli Model D (`UPGRADE_MODELD`) + `UPGRADE_HARDENING_V4` altında `GetAdamThreshold(nHeight, nActiveMasternodes)` aktif MN sayısı `>= 8` olduğu anda bootstrap döneminde (`nHeight < nAdamThresholdBootstrapLimit`) dahi **`8`** (`> 2 Coin-Lock + 5 Açık PoW = 7` MN-dışı koltuk ve `<= 8` MN koltuğu) zorunlu kılar ve gevşeme tabanını **`ADAM_MODELD_MIN_RELAXED_THRESHOLD = 8`** olarak korur (`>= 8` MN varken daima en az `1` Aktif Masternode zorunludur; yalnızca `< 8` MN varken temel `5` eşiği `ADAM_MIN_RELAXED_THRESHOLD_BOOTSTRAP = 3`'e gevşer); Model D öncesinde ise `ADAM_MIN_RELAXED_THRESHOLD = 5` (`> 4` MN dışı koltuk; bootstrap `< 7` MN iken `3`) kullanılır. `ActiveProposalGuard` aynı ebeveyn blok üzerinde eşzamanlı PoW/PoS üretimini engeller.
6. **Orantılı Kilitli Sermaye Ağırlıklı VRF Koordinatör Seçimi (`SelectAdamNodes` — `UPGRADE_FAIRNESS` & `UPGRADE_HARDENING_V4`)**:
   * **Fonlu Havuz Aktif (`totalFundedWeight > 0`)**: `UPGRADE_FAIRNESS` altında `SelectAdamNodes`, kulvar içi temsilci tavanı olmaksızın orantılı kilitli sermaye ağırlıkları (**Aktif MN için `4.200`, `Coin-Lock` için `250`, `Fonlu PoW-Lock` için `10`**) uygular ve MN ağırlıklarını 256-bit `arith_uint256` ile ölçekler:
     - **VRF Anahtar Olgunluğu (`UPGRADE_HARDENING_V4`, `ADAM_VRF_KEY_MATURITY = 100` blok)**: Yeni kayıtlı veya operatör anahtarı döndürülmüş doğrulayıcılar `maturedCandidates` (`totalMaturedWeight > 0`) havuzuna girebilmek için en az **`100` blok** kesintisiz kayıtlı kalmalıdır. `SelectAdamNodes`, birincil Koordinatörü yalnızca hiç olgunlaşmış aday yoksa `weightedCandidates` havuzundan çeker.
     - **Dengeli Model D (`ADAM_FAIR_COORD_V5`, `IsModelDActive(height)` aktifken)**: `11 * sumNonMN > 9 * sumMN` olduğunda **kesin `>= %55` Aktif MN güvenlik tabanı** (`Hash(Seed_H, "ADAM_FAIR_COORD_V5") mod drawTotalWeight`).
     - **Model D Öncesi (`ADAM_FAIR_COORD_V4`, Mainnet `< 5.000`)**: `3 * sumNonMN > sumMN` olduğunda **kesin `>= %75` Aktif MN güvenlik tabanı** (`Hash(Seed_H, "ADAM_FAIR_COORD_V4") mod drawTotalWeight`).
     Her iki dönem de eski `65:8:2` (`ADAM_QUAD_COORD_V3`) kotasını kaldırırken fonlu doğrulayıcı varken `0-KRISTA` Open-Entry madencilerine `0` Koordinatör ağırlığı verir.
   * **Bootstrap / Zero-Coin Yedek (`totalFundedWeight == 0`)**: Yalnızca fonlu doğrulayıcı yoksa `Block 1..199` Bootstrap (veya o da boşsa `Zero-Coin PoW`) arasından seçilir.

---

## 4. Blok Başlığı Uzantıları ve Serileştirme (Serialization)

ADAM ağ yükseltmesi (`Consensus::UPGRADE_ADAM`) aktif olduğunda, bloklar **Versiyon 11** (Fallback Mode) veya **Versiyon 12** (Standart Mod) blok yapıları kullanılarak serileştirilir. `src/primitives/block.h` dosyasındaki `CBlockHeader` sınıfı dört yeni alanla genişletilmiştir:

```cpp
class CBlockHeader {
public:
    // Legacy fields...
    int32_t nVersion;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    uint32_t nTime;
    uint32_t nBits;
    uint32_t nNonce;

    // ADAM Extended fields (Version >= 11)
    std::vector<CPubKey> vAdamMiners;                  // Public keys of elected miners (and coordinator in v11)
    std::vector<std::vector<unsigned char>> vAdamSolutions; // Serialized partial solutions (nonce + miner signature)
    std::vector<unsigned char> vAdamVRFProof;          // Coordinator's VRF signature on previous seed
    std::vector<unsigned char> vAdamCoordinatorSig;    // Coordinator's signature on final block header hash
};
```

### 4.1 Hashing ve Serileştirme Sıralaması
Nihai blok başlığı hash'ini imzalayan ve `GetHash()` fonksiyonunun dışında tutulan `vAdamCoordinatorSig` alanının aksine, `vAdamVRFProof` blok hash'i hesaplanmadan *önce* üretilir. `GetHash()` sırasında blok başlığı serileştirmesine dahil edilir, bu da VRF kanıtının başlığa kriptografik olarak kilitlenmesini sağlar.

### 4.2 Durumsuz Blok Hashing Zinciri (Stateless)
Blok hash'ini (`CBlockHeader::GetHash()`) hesaplamak için ADAM, serileştirilmiş başlığı (versiyon 12 ise `vAdamCoordinatorSig` ve LLMQ `vQuorumSig` hariç tutularak), `bool fFallbackMode = (nVersion == 11) || (vAdamMiners.size() == vAdamSolutions.size() + 1);` koşuluyla belirlenen $M$ adet ardışık hashing turundan oluşan bir zincir boyunca hash'ler:
* Fallback modunda (`nVersion == 11` veya sonuna Koordinatör eklenmiş başlıklarda `vAdamMiners.size() == vAdamSolutions.size() + 1`): $M = \text{vAdamMiners.size()} - 1$.
* Standart modda (`nVersion == 12` ve `vAdamMiners.size() == vAdamSolutions.size()`): $M = \text{vAdamMiners.size()}$ (böylece **15** Model D koltuğunun ve **11** Model D öncesi koltuğun tamamı 3-permütasyonlu modda özetlenir).

Her bir $i \in \lbrace 0, \dots, M-1 \rbrace$ turu için:
1. Önceki blok hash'inden ve tur endeksinden benzersiz bir tur hash'i türetilir:

   $$\text{roundHash}_i = \text{Hash}\left(\text{hashPrevBlock} \mathbin{\Vert} i\right)$$

2. İlk bayt $v_i = \text{roundHash}_i[0]$ elde edilir.
3. Tek bir ortak asal (coprime) çarpanı $m_i$ hesaplanır:

   $$m_i = v_i \mid 1$$

   Eğer $m_i < 3$ ise, $m_i = 3$ olarak ayarlanır. Bu, çarpanların $2^{256}$ değerine göre aralarında asal olmasını sağlayarak %100 entropiyi korur.
4. Tur hashing işlemi gerçekleştirilir:
   - **Fallback Modu (Versiyon 11)** (burada `UPGRADE_FAIRNESS` altında $\text{algo}_i = \text{kUniformAlgos}[\text{Hash}(\text{hashPrevBlock} \mathbin{\Vert} \text{vAdamMiners}[i]) \pmod{17}]$):
     - **Tur 0**: $H_0 = \text{CalculateAdamPuzzleHash}(\text{algo}_0, \text{SerializedHeader}) \times m_0 \pmod{2^{256}}$ hesaplanır ve $H_{\text{prev}} = H_0$ atanır.
     - **Tur $i > 0$**: $H_i = \text{CalculateAdamPuzzleHash}(\text{algo}_i, H_{\text{prev}}) \times m_i \pmod{2^{256}}$ hesaplanır ve $H_{\text{prev}} = H_i$ olarak güncellenir.
   - **Standart Mod (Versiyon 12)**:
     - **Tur 0**:
       * `GetAdam3PermutationAlgos(hashPrevBlock, vAdamMiners[0])` ile 3-permütasyon algoritmaları $\text{algo1}$, $\text{algo2}$, $\text{algo3}$ türetilir.
       * Hesaplama yapılır:

         $$H_0^{(3)} = \text{CalculateAdamPuzzleHash}(\text{algo3}, \text{SerializedHeader})$$

         $$H_0^{(2)} = \text{CalculateAdamPuzzleHash}\left(\text{algo2}, \left( H_0^{(3)} \times 1 \right) \pmod{2^{256}}\right)$$

         $$H_0 = \text{CalculateAdamPuzzleHash}\left(\text{algo1}, \left( H_0^{(2)} \times 1 \right) \pmod{2^{256}}\right)$$

       * Çarpan uygulanır:

         $$H_{\text{prev}} = (H_0 \times m_0) \pmod{2^{256}}$$

     - **Tur $i > 0$**:
       * `GetAdam3PermutationAlgos(hashPrevBlock, vAdamMiners[i])` ile 3-permütasyon algoritmaları $\text{algo1}$, $\text{algo2}$, $\text{algo3}$ türetilir.
       * Hesaplama yapılır:

         $$H_i^{(3)} = \text{CalculateAdamPuzzleHash}(\text{algo3}, H_{\text{prev}})$$

         $$H_i^{(2)} = \text{CalculateAdamPuzzleHash}\left(\text{algo2}, \left( H_i^{(3)} \times (i + 1) \right) \pmod{2^{256}}\right)$$

         $$H_i = \text{CalculateAdamPuzzleHash}\left(\text{algo1}, \left( H_i^{(2)} \times (i + 1) \right) \pmod{2^{256}}\right)$$

       * Çarpan uygulanır:

         $$H_{\text{prev}} = (H_i \times m_i) \pmod{2^{256}}$$

$M - 1$ turunun sonundaki nihai hash $H_{\text{prev}}$, blok hash'i olarak döndürülür.

---

## 5. Konsensüs Doğrulama Kuralları

Bir blok alındığında, ADAM ağ yükseltmesi (`Consensus::UPGRADE_ADAM`) aktifse, `src/main.cpp` içindeki `CheckBlock()` ve `CheckFormsOfWork()` fonksiyonları aşağıdaki doğrulamaları zorunlu kılar:

1. **Versiyon Zorunluluğu ve Sürüm Düşürme (Downgrade) Koruması**:
   - `Consensus::UPGRADE_ADAM` aktif olduğunda (`nHeight >= 200`), blok versiyonu en az `11` olmalıdır.
   - `Consensus::UPGRADE_POMBL` aktif olduğunda (`vUpgrades[Consensus::UPGRADE_POMBL].nActivationHeight`: Mainnet üzerinde `4.800`, Testnet üzerinde `400`, Regtest üzerinde `300`), hem `ContextualCheckBlockHeader` hem de `CheckBlock` fonksiyonları `block.nVersion >= 12` olmasını zorunlu kılar ve sürüm düşürme saldırılarını önlemek amacıyla `block.nVersion < 12` olan tüm blokları `bad-version` DoS hatası ile reddeder.

2. **Madenciler ve Çözüm Boyutları**:
   - **Versiyon 11 (Fallback Mode)**:
     - `vAdamMiners` boyutu `threshold` ile `GetAdamMinersCount(nHeight) + 3` arasında (Mainnet/Testnet'te Model D öncesi `11..14`, Regtest `200..299` gibi `UPGRADE_POMBL` öncesi Model D aktifliğinde en fazla `18`) olmalıdır.
     - `vAdamSolutions` boyutu `vAdamMiners.size() - 1` (veya yalnızca ilk `vAdamMiners.size() - 1` komite koltuğu değerlendirilmek üzere `vAdamMiners.size()`) olmalıdır.
     - Doğrulanacak Koordinatör, `vAdamMiners` dizisinin son elemanıdır (`vAdamMiners.back()`).
   - **Versiyon 12 (Standart Mod)**:
     - `vAdamMiners` boyutu tam olarak `GetAdamMinersCount(nHeight)` (**Model D altında `15`**, öncesinde `11`) değerine eşit olmalıdır.
     - `vAdamSolutions` boyutu tam olarak `vAdamMiners.size()` (**Model D altında `15`**, öncesinde `11`) değerine eşit olmalıdır.
     - Koordinatör, `SelectAdamNodes` ($t \ge 45\text{ sn}$ durumunda `GetAdamFallbackCoordinatorForSlot`) algoritmasından türetilir.

3. **VRF Kanıtı Doğrulaması**: `vAdamVRFProof`, önceki rolling seed $\text{Seed}_{H-1}$'e göre doğrulanmalıdır ve `VerifyAdamOrDip3Signature` / `VerifyBLSWithECDSAFallback` kullanılarak beklenen Koordinatörün açık anahtarıyla (veya `UPGRADE_DIP3` altında kayıtlı **KTDIP3** `blsPubKeyOperator` anahtarıyla, yahut `UPGRADE_HARDENING_V4` altında kayda sabitlenen BLS anahtarıyla) eşleşen geçerli bir imza olmalıdır.

4. **Seçim Yolu Doğrulaması (Election Path Validation)**: Mainnet ve Testnet üzerinde hem Versiyon 11 (`vAdamMiners[0..M-1]`) hem de Versiyon 12 (`vAdamMiners[0..14]` Model D'de, `vAdamMiners[0..10]` öncesinde) bloklarında komite açık anahtarları `SelectAdamNodes` çıktısıyla (veya $t \ge 60\text{ sn}$ sonrasında `ValidateAdamCommitteeForTime` ile doğrulanan meşru aynı kulvar yedek devriyle) eşleşmelidir; yalnızca Regtest ortamı kendi kendine yeten birim testleri için Versiyon 11 seçim kontrolünü atlar.

5. **Kısmi Çözümler Doğrulaması (Partial Solutions Validation)**:
   - Geçerli kısmi çözümlerin sayısı `GetAdamEffectiveThreshold(nHeight, nBlockTimeDiff, nActiveMasternodes)` fonksiyonunun belirlediği etkin çoğunluk eşiğini karşılamalıdır (temel eşik olan `GetAdamThreshold(nHeight, nActiveMasternodes)`, `nAdamThresholdBootstrapLimit` [Mainnet'te 50.000, Testnet'te 100.000, Regtest'te 1.000] altında aktif MN sayısı `< 8` iken `5`, Model D + `UPGRADE_HARDENING_V4` ile `nActiveMasternodes >= 8` olduğu anda bootstrap döneminde dahi doğrudan **`nAdamThresholdModelD = 8`** olup bootstrap sonrasında Mainnet/Regtest üzerinde **`8`** [öncesinde `nAdamThreshold = 7`], Testnet üzerinde **`3`**'tür ve $t \ge 120\text{ sn}$ sonrasında Bölüm 3'te açıklandığı şekilde gevşetilir).
   - Her çözüm bir `nonce` ve bir `signature` (imza) olarak ayrıştırılır.
   - Bulmaca hash'i (puzzle hash), madenciye atanan algoritma(lar) kullanılarak hesaplanır:
     - **Versiyon 11 (Fallback Mode)** içinde:

       $$\text{PuzzleHash} = \text{CalculateAdamPuzzleHash}\left(\text{algoIndex}, \text{Seed}_H \mathbin{\Vert} \text{MinerPubKey}_i \mathbin{\Vert} \text{Nonce}_i\right)$$

       Burada `UPGRADE_FAIRNESS` altında $\text{algoIndex} = \text{kUniformAlgos}[\text{Hash}(\text{Seed}_H \mathbin{\Vert} \text{MinerPubKey}_i) \pmod{17}]$ olup desteklenen **17 tek-geçişli algoritmadan** biri kullanılır (asimetrik `X11KVS` hariçtir; Fairness öncesi `% 18` idi).
     - **Versiyon 12 (Standart Mod)** içinde: Önceki bloğun hash'ine ve madencinin 33 baytlık sıkıştırılmış açık anahtarına dayanarak **`17` tek-geçişli algoritma arasından (`4.080` permütasyon, `X11KVS` hariç)** deterministik olarak 3 farklı hashing algoritmasını ($\text{algo1}$, $\text{algo2}$ ve $\text{algo3}$) seçen bir 3-permütasyon seçici şeması $\text{GetAdam3PermutationAlgos}(\text{hashPrevBlock}, \text{MinerPubKey}_i)$ kullanılır. Çözücü bu üç algoritmayı birleştirir:

       $$\text{PuzzleHash} = \text{algo1}\left( (\text{minerIdx} + 1) \times \text{algo2}\left( (\text{minerIdx} + 1) \times \text{algo3}(\text{Challenge}) \right) \right) \pmod{2^{256}}$$

       Burada $\text{Challenge} = \text{Seed}_H \mathbin{\Vert} \text{MinerPubKey}_i \mathbin{\Vert} \text{Nonce}_i$ şeklindedir.
     - `PuzzleHash`, `nBits` ile tanımlanan hedef zorluğu karşılamalıdır (Bölüm 2.D'de açıklandığı gibi $\text{scaledTarget} = \text{Target} \ll \text{activeShift}$ olarak gevşetilir).
   - İmza, `PuzzleHash`'i imzalayan `MinerPubKey_i`'ye (ve `UPGRADE_HARDENING_V4` altında `VerifyAdamOrDip3Signature` üzerinden kayda sabitlenen BLS anahtarına) göre doğrulanmalıdır.

6. **Koordinatör İmzası Doğrulaması ve PoW CoinStake Yasağı**: `vAdamCoordinatorSig`, `VerifyAdamOrDip3Signature` / `VerifyBLSWithECDSAFallback` kullanılarak nihai blok başlığı hash'ini (imzanın kendisi hariç) imzalayan beklenen Koordinatörün açık anahtarına (veya `UPGRADE_DIP3` altında kayıtlı **KTDIP3** `blsPubKeyOperator` anahtarına, yahut V4 kayda sabitli BLS anahtarına) göre doğrulanmalıdır. Ek olarak `UPGRADE_HARDENING_V4` altında bir PoW bloğuna (`!block.IsProofOfStake()`) dahil edilen her `IsCoinStake()` işlemi **`bad-cs-in-pow`** ile kesinlikle reddedilir ve `fAllowCoinStake` parametresi `CheckTxInputs` kontrolünü sıkı şekilde sınırlar.

---

## 6. Cüzdan ve RPC API Entegrasyonu

Geliştiricileri ve madencilik havuzu operatörlerini desteklemek amacıyla RPC API, işbirlikçi konsensüs alanlarını dışarıya sunar (expose):

### `getblocktemplate` İsteği ve Yanıtı
Harici madenci yazılımları (`cpuminer-opt -a adam`, `ccminer -a adam`), cüzdanda kayıtlı belirli bir doğrulayıcı veya Masternode operatör koltuğunu hedeflemek üzere `"mineraddress"` içeren bir JSON şablon isteği (`{"mineraddress": "<KRISTA_adresi>"}`) gönderebilir.
`nVersion >= 11` olduğunda `getblocktemplate`, çağrıyı yapan düğümün/adresin mevcut turdaki rolüne göre iki farklı yanıt modu sunar:
* **Harici Komite Bulmaca İş Yanıtı** (yerel cüzdandaki bir doğrulayıcı anahtarı veya belirtilen `"mineraddress"` mevcut turda komiteye seçildiğinde ve bu uç için bulmacasını henüz çözmediğinde döner):
  * `capabilities`, `version` (`11` Fallback Modu veya `12` Standart PoMBL Modu), `previousblockhash`, `transactions` (`[]`), `bits`, `height`, `curtime`
  * `target`: Hex-kodlu 256-bit gevşetilmiş bulmaca hedefi (`scaledTarget = Target << activeShift`).
  * `puzzleheader`: 80-bayt hex-kodlu bulmaca girdi ön eki (`32-bayt adamSeed || 0x21 || 33-bayt sıkıştırılmış myMinerKey || 10-bayt sıfır dolgusu || 4-bayt nonce yuvası`).
  * `powalgo`: Atanan bulmaca algoritması tanımı — Versiyon 11'de `"<algo>"` (örn. `"hamsi"`), Versiyon 12'de ise `"<algo1>+<algo2>+<algo3>+<minerIdx>"` (örn. `"whirlpool+fugue+haval+0"`).
* **Bekleme Yanıtı** (yerel/belirtilen doğrulayıcı anahtarı mevcut turda seçilmediğinde, mevcut uç için bulmacasını zaten çözdüğünde veya aktif havuz boş olduğunda döner — klasik BIP22 şablonuna düşmeden gereksiz CPU/GPU tüketimini önler):
  * `capabilities`, `version`, `previousblockhash`, `transactions` (`[]`), `height`, `curtime` ve `"status": "waiting_for_election"`.
* **Koordinatör Tam Blok Şablonu ve `getblock` JSON Alanları Ayrımı**:
  * `getblocktemplate` tam bir Koordinatör blok şablonu oluşturduğunda (`src/rpc/mining.cpp`), küçük harfli `adamminers` (hex-kodlu sıkıştırılmış açık anahtar dizisi), `adamsolutions`, `adamvrfproof` ve `adamcoordinatorsig` alanlarını döndürür.
  * `getblock` onaylanmış veya aday bir bloğu `blockToJSON` (`src/rpc/blockchain.cpp`) üzerinden serileştirdiğinde ise camelCase `vAdamMiners` (`EncodeDestination(miner.GetID())` ile Base58Check KRISTA adresleri dizisi), `vAdamSolutions`, `adamSeatAlgos` (her komite koltuğuna atanan V11 tekli veya V12 3-permütasyonlu algoritma dizgisi), `adamCoordinator` (çözümlenen Koordinatör KRISTA adresi), `vAdamVRFProof`, `vAdamCoordinatorSig`, `vQuorumSig` ve `adamQuorumSigners` (`signedCount`, `quorumSize`, `threshold`, `verified` ve `signers` dizisi) alanlarını döndürür.

### `generate` RPC ve Qt Cüzdan Madenci Merkezi (Miner Hub)
`generate` / `setgenerate` RPC komutları ve **KristaTech-Qt Madenci Merkezi** (`MINER HUB` sekmesi, `MinerHubWidget`), ADAM blokları için işbirlikçi bulmaca çözümünü ve blok birleştirme sürecini yönetir. Geleneksel yüksek zorluklu hash yarışması yapmak yerine düğüm:
1. Dönen VRF tohumuna dayanarak yerel cüzdan/Masternode anahtarlarından hangilerinin Komite Doğrulayıcısı veya Koordinatör olarak seçildiğini belirler.
2. Seçilen tüm yerel doğrulayıcı koltukları için hafif kısmi PoW bulmacalarını çözer ve `ADAMSOL` mesajlarıyla ağa yayar (ayrıca eş `VERACK` bağlantısında `CollectCachedAdamSolutionsForTip` ile güncel uca ait en fazla `MAX_ADAMSOL_SYNC_PER_PEER = 32` önbelleklenmiş `ADAMSOL` mesajını iletir ve uç geciktiğinde her `60s`'de bir yeniden yayınlar; `AutoRegisterMiner` ise yalnızca `!amnodeman.GetActiveMasternodesCopy().empty()` kontrolüyle atlanır).
3. Koordinatör olarak seçildiyse çözümleri toplar, PoW Blok Üreticisi coinbase payını (`vout[0]`) doğrudan seçilen Koordinatörün kendi adresine (`4.200 KRISTA` KTDIP3 teminatlı bir Masternode ise `scriptPayout` ve varsa `scriptOperatorPayout` payına) yönlendirir, VRF kanıtını ve nihai blok özetini deterministik olarak imzalar (Model D aktif olduğunda aktif MN sayısına göre ölçeklenen LLMQ `vQuorumSig` — `UPGRADE_HARDENING_V4` altında `floor(2N/3)+1`: **5'te 4, 9'da 7, 11'de 8 veya 15'te 11**; V4 öncesi 5'te 3..15'te 8 — sertifikası alır) ve tamamlanmış bloğu ağa sunar. Bağımsız **MINER HUB** üst çubuk sekmesi **Tam Ekran (`F11` / `Esc`)** izleme, tek tıkla CPU madenciliği, arayüzü dondurmayan tek tıkla `PoW-Lock` / `Coin-Lock` kaydı (`0 KRISTA` Open-Entry madenciliği ve olgunlaşmış bakiye oluştuğunda fonlu kilide otomatik terfi dahil), `< 240` blok (`<= 24` blok kiralama) kalan/dolan kilitler için satır içi `Unlock & Renew` uyarısı, harici RPC madenci (`cpuminer`/`ccminer`) algılama, canlı komite rolü ve `ADAMSOL` rozetleri, sıralanabilir sütunlar, özel etiketler ve maskeli `cpuminer` komutu kopyalama özelliklerini sunar.

---

## 7. Proof-of-Stake (PoS) Entegrasyonu ve İşbirlikçi PoS

ADAM, Proof-of-Stake (PoS) mekanizmasının yerine geçmez, aksine onunla entegre olarak PoS ile PoW'un (ADAM işbirlikçi madenciliği) birlikte çalıştığı bir **Hibrit İşbirlikçi PoS (Hybrid Cooperative PoS)** konsensüs mekanizması oluşturur.

Geleneksel PoS modelinde, blok üretimi yalnızca stake ağırlığına (cüzdanda tutulan coin miktarı) göre belirlenir. ADAM'da bu süreç, blok grinding'i, bencil staking'i ve hedefli lider DoS saldırılarını önlemek için işbirlikçi madenci-koordinatör doğrulama döngüsüyle birleştirilir.

### Staking ve İşbirlikçi Yaşam Döngüsü (Blok yüksekliği $\ge$ 200)

Ağ yükseltmesi `Consensus::UPGRADE_POS` etkinleştirildiğinde (Mainnet üzerinde 200. blok yüksekliğinde), blok üretimi saf İşbirlikçi PoW'dan, PoW işbirlikçi madenciliği ile PoS staking'in paralel olarak çalıştığı Hibrit İşbirlikçi PoS modeline geçer:

> [!IMPORTANT]
> **Tek İmzalı PoS Bloklarına İzin Verilmez:** Proof-of-Work (ADAM işbirlikçi madenciliği) asla sonlanmaz veya PoS lehine devre dışı bırakılmaz. KristaTech bünyesindeki her PoS bloğu hibrittir; seçilen ADAM madencileri tarafından çözülen PoW bulmacalarının doğrulanmasını gerektirir ve hem stake edenin anahtarı (`vchBlockSig`) hem de seçilen ADAM koordinatörünün anahtarı (`vAdamCoordinatorSig`) ile çift imzalanmış (double-signed) olması gerekir. Tek imzalı bloklar, konsensüs doğrulama kuralları tarafından kesin bir şekilde reddedilir.

1. **Staking Yetkisi (Kernel Kontrolü)**:
   Cüzdanın staking iş parçacığı (staking thread - `ThreadStakeMinter`), kernel hash kontrolünü (coin ağırlığı ile orantılı olarak) doğrulayarak herhangi bir UTXO'nun bir blokta stake etmeye uygun olup olmadığını periyodik olarak değerlendirir.
   
2. **İşbirlikçi Bulmaca Toplama ve Dönüşümlü Slot Açılış Önceliği**:
   Her 15 saniyelik `TimeProtocolV2` slot açılışında (`src/miner.cpp` içindeki `ADAM_SLOT_PRIORITY_WINDOW_MS = 1500` ms), PoW ve PoS önceliği ebeveyn bloğun türüne göre (`pindexPrev->IsProofOfStake()`) dönüşümlü olarak verilir: önceki blok PoW ise PoS paydaşları beklemesiz (`0 ms`) başlarken PoW koordinatörleri (`BitcoinMiner` ve `TryAssembleCoordinatorBlockAsync`) `1500 ms` bekler; önceki blok PoS ise PoW koordinatörleri beklemesiz (`0 ms`) başlarken PoS paydaşları (`ThreadStakeMinter`) `1500 ms` bekler (öncelikli taraf bu pencerede blok üretmezse diğer taraf aynı 15 saniyelik slot içinde kesintisiz devreye girer). Staking iş parçacığı sırası geldiğinde geçerli bir çekirdek bulduğunda aday blok şablonunu oluşturur (`UPGRADE_POMBL` altında `Versiyon 11`, `UPGRADE_POMBL` ve üzerinde `Versiyon 12`).
   Paydaş (staker), `SelectAdamNodes` ($t \ge 60\text{ sn}$ durumunda `ResolveAdamCommitteeForTime`) aracılığıyla seçilen madencileri alır ve `mapAdamSolutionsCache` önbelleğinden kısmi PoW bulmaca çözümlerini toplar; etkin çoğunluk eşiği (`GetAdamEffectiveThreshold`) sağlanana kadar şablonun kesinleştirilmesi ertelenir.

3. **Merkeziyetsiz Koordinatör Doğrulaması ve Ortak İmzalama (`POSCOORDREQ` / `POSCOORDSIG`)**:
   - **Yerel Koordinatör**: Aktif Koordinatör anahtarı ($t < 45\text{ sn}$ için birincil `expectedCoordinator`, $t \ge 45\text{ sn}$ için `GetAdamFallbackCoordinatorForSlot`) paydaşın kendi düğümündeyse, düğüm `AcquireCoordinatorProposalSlot` yuvasını ayırır ve `vAdamVRFProof` ile `vAdamCoordinatorSig` alanlarını doğrudan imzalar.
   - **Uzak Koordinatör (P2P Ortak İmzalama)**: Aktif Koordinatör ayrı bir Masternode veya doğrulayıcı düğümdeyse, paydaş `ComputePoSCoordRequestHash` özetini coinstake girdisinin ECDSA anahtarıyla imzalar (`SignPoSCoordRequest`) ve P2P ağı üzerinden çok sekmeli (multi-hop) yayılan bir `POSCOORDREQ` mesajı gönderir. Eş düğümler iletim öncesinde `CheckBlockTime`, `CheckBlock(..., fCheckSig=false)`, `ContextualCheckBlock`, `CheckProofOfStake` ve `ConnectBlock(..., fJustCheck=true)` kontrollerini yürütür; aktif Sıcak Koordinatör ise ek olarak `VerifyPoSCoordRequestAuth` ve `TestBlockValidity` kontrollerini doğrular, aynı `(parentHash, coordinatorKey)` için çift PoW+PoS üretimini engelleyen `AcquireCoordinatorProposalSlot` yuvasını kilitler, `vAdamVRFProof` + `vAdamCoordinatorSig` imzalarını üretir ve P2P üzerinden `POSCOORDSIG` yanıtını yayınlar. Model D aktif olduğunda paydaş ardından aktif BFT LLMQ `vQuorumSig` sertifikasını (`RequestBlockQuorumSig`, `UPGRADE_HARDENING_V4` altında **`4-of-5`..`11-of-15`**; V4 öncesi `3-of-5`..`8-of-15`) talep eder.

4. **Stake Eden Blok İmzası (Çift Kilitleme)**:
   Tamamlanan blok, iki ayrı imza türü kullanılarak güvence altına alınır:
   - **ADAM İmzası (`vAdamCoordinatorSig`)**: İşbirlikçi PoW turunun ve VRF tohum geçişinin geçerliliğini onaylamak üzere aktif Koordinatör tarafından (yerel olarak veya `POSCOORDSIG` aracılığıyla) oluşturulur.
   - **PoS Blok İmzası (`vchBlockSig`)**: Stake edenin cüzdanı tarafından `SignBlock` kullanılarak oluşturulur (stake edilen UTXO'nun gizli anahtarı kullanılarak nihai blok hash'i imzalanır).

### Ağ Üzerinde Çift Doğrulama (Dual Validation)

Bir eş düğüm (peer) İşbirlikçi bir PoS bloğu aldığında, `CheckBlock` içindeki doğrulama kuralları her iki konsensüs kontrolünün de geçmesini zorunlu kılar:
1. **Proof-of-Stake Doğrulaması**: Düğüm, `coinstake` işlemini doğrular, kernel hash hedef zorluğunu kontrol eder ve stake edenin blok imzasını (`vchBlockSig`) doğrular.
2. **ADAM İşbirlikçi PoW Doğrulaması**: Düğüm, seçilen madencilerin kısmi PoW bulmaca çözümlerini ($T$ kuorum eşiğini karşılayan `vAdamSolutions`), Koordinatörün VRF kanıtını (`vAdamVRFProof`), Koordinatörün imzasını (`vAdamCoordinatorSig`) ve Model D aktif olduğunda Versiyon 12 bloklarındaki LLMQ kuorum imzasını (`vQuorumSig`) doğrular.

---

## 8. LLMQ (Long-Living Masternode Quorums) ve DKG (Distributed Key Generation)

Standart Mod altında Versiyon 12 blok şablonlarını doğrulamak ve imzalamak için ağ, **Dağıtık Anahtar Üretimi (DKG - Distributed Key Generation)** oturumlarını yürüten **Uzun Ömürlü Masternode Kuorumlarına (LLMQ - Long-Living Masternode Quorums)** dayanır.

> [!NOTE]
> **Eşik Değerleri Arasındaki Fark:**
> **ADAM İşbirlikçi Madencilik Eşiği** (madenci bulmaca çözümleri `vAdamSolutions` için Dengeli Model D'de uygulanan 15'te 8, öncesinde 11'de 7 kuralı) ile **LLMQ İmza Doğrulama Eşiği** (blok imzası `vQuorumSig` için uygulanan dinamik BFT süper çoğunluğu) karıştırılmamalıdır:
> * **ADAM Eşiği**: Dengeli Model D kapsamında 15 seçilmiş madenciden en az **8**'inin (`nAdamThresholdModelD = 8` Mainnet/Regtest üzerinde, `3` Testnet üzerinde — Model D + `UPGRADE_HARDENING_V4` ile `nActiveMasternodes >= 8` olduğunda bootstrap döneminde dahi doğrudan uygulanır, `< 8` MN iken `5`; Model D öncesinde `11`'de `7`) PoW bulmaca çözümünü (`vAdamSolutions`) çözmüş ve göndermiş olmasını şart koşar. Bu kural ADAM işbirlikçi bulmaca doğrulama katmanına aittir.
> * **LLMQ Eşiği**: Blok hash'inin geçerli sayılabilmesi için aktif LLMQ üyelerinden en az `llmq::GetQuorumThreshold(N, nHeight)` tanesinin — `UPGRADE_HARDENING_V4` altında **`floor(2N/3) + 1`** (`4-of-5`, `7-of-9`, `8-of-11` veya `11-of-15`, `GetTargetQuorumSize` ile aktif Masternode sayısına göre dinamik ölçekli; V4 öncesi `floor(N/2)+1`: `3-of-5`, `5-of-9`, `6-of-11`, `8-of-15`) — BLS12-381 anahtarlarıyla (`UPGRADE_DIP3` altında `blsPubKeyOperator` veya `DeriveBLSFromCKey`, `CQuorumSignature` üzerinden `vQuorumSig` içine toplanmış olarak) bloğu imzalamış olmasını şart koşar. Bu kural merkeziyetsiz blok doğrulama katmanına aittir.

### A. Kuorum Topolojisi ve Parametreleri
* **Kuorum Boyutu ve Eşiği**: `UPGRADE_HARDENING_V1` (`src/llmq.cpp` içindeki `GetTargetQuorumSize`) ve `UPGRADE_HARDENING_V4` (`llmq::GetQuorumThreshold(N, nHeight)`) kapsamında her aktif LLMQ doğrulanmış aktif Masternode sayısına göre dinamik ölçeklenir ve **`floor(2N/3) + 1`** BFT süper çoğunluğunu uygular: `<25` MN için **`5`** (**`4-of-5`**), `25–49` için **`9`** (**`7-of-9`**), `50–99` için **`11`** (**`8-of-11`**) ve `>=100` MN için **`15`** (**`11-of-15`**) (V4 öncesi `floor(N/2)+1`: `3-of-5..8-of-15`).
* **DKG Aralıkları ve Blok Bazlı Rotasyon**:
  - **Başlangıç Epoch Modu**: Mainnet'te her **100 blokta** bir ve Testnet/Regtest'te her **10 blokta** bir yeni bir DKG oturumu gerçekleştirilir (`GetActiveQuorum`).
  - **Blok Bazlı Kuorum Rotasyonu**: Kuorum rotasyonu aktifleştiğinde (Mainnet'te **1.400**, Testnet'te **1.000** ve Regtest'te **800** blok yüksekliğinde), `GetActiveQuorum` her blok yüksekliğinde `((nHeight / dkgInterval) * dkgInterval) - 1` yüksekliğindeki epoch tohumunu kullanarak `RunDKG(nHeight)` çalıştırır.
* **Kuorum İmzası Doğrulama Eşiği (`CQuorumSignature::Verify`)**:
  - **Tüm Ağlar (Mainnet, Testnet, Regtest)**: Model D (`UPGRADE_MODELD`) aktif olduğunda, `CQuorumSignature::Verify` fonksiyonu `llmq::GetQuorumThreshold(quorum.members.size(), nSigHeight)` ile **`floor(2N/3) + 1`** BFT süper çoğunluğunu (`UPGRADE_HARDENING_V4` altında **`4-of-5`**, **`7-of-9`**, **`8-of-11`** veya **`11-of-15`**; V4 öncesi `floor(N/2)+1`: `3-of-5`, `5-of-9`, `6-of-11`, `8-of-15`) zorunlu kılar.
  - **Model D Aktivasyonundan Önce**: Mainnet (4.800–4.999 blokları) ve Testnet (400–499 blokları) üzerinde başlangıç masternode birikimine olanak tanımak amacıyla kuorum imzası doğrulaması `CheckBlock()` içinde (`fFallbackMode`) atlanır. Regtest üzerinde Model D yüksekliği 200'de (`UPGRADE_POMBL` yüksekliği 300'den önce) aktifleştiği için BFT çoğunluk eşiği 300. bloktan itibaren her zaman aktiftir.

### B. Aktif Masternode Filtreleme (%100 Zincir Üstü Deterministik Havuz)
- Kuorumlar ve ADAM doğrulayıcı seçimleri, yerel P2P bellek listesine (`mnodeman`) bağımlı olmaksızın %100 zincir üstü doğrulanmış Masternode durumundan — öncelikli olarak **`CDeterministicMNManager` (`evodb`, `UPGRADE_DIP3`)**, yanında eski `mapMasternodeCollateralOutpoints` / `mapOnChainCollateralDelegations` ve `pcoinsTip` üzerindeki 4.200 KRISTA teminat doğrulaması — deterministik olarak seçilir.
- `nTargetSize` (ör. `5`) altında zincir üstü aktif masternode kayıtlıysa, tüm doğrulanmış gerçek Aktif Masternode'lar (`1..4`) kurulda kesin olarak korunur ve yalnızca eksik kalan koltuklar `src/llmq.cpp` içindeki kota-izoleli LLMQ yedek adaylarından (`GetAdamMinerPool(nHeight - 1)`: en fazla 1 Coin-Lock + en fazla 3 Fonlu PoW-Lock, ardından taşma -> Bootstrap -> Zero-Coin -> Coin-Lock yedeği — 15 koltuklu `8+2+5` Model D ADAM komitesinden ayrıdır) deterministik olarak doldurulur.
- Mainnet, Testnet veya Regtest üzerinde kod içinde sabitlenmiş (hardcoded) herhangi bir anahtar kimliği (key ID) filtrelemesi veya yerel cüzdan kısıtlaması uygulanmaz, bu da tamamen merkeziyetsiz ve güven gerektirmeyen (trustless) bir konsensüs ortamı sağlar.

### C. Deterministik Epoch-Tohum Hash Sıralaması ve Blok Bazlı Canlılık Denetimi
Tüm düğümlerin kuorum üyeliği üzerinde %100 mutabakata varmasını sağlarken kuorum üyelerini hedefli DoS/DDoS saldırılarına karşı korumak amacıyla (`src/llmq.cpp` içindeki `ElectQuorumMembers`):
- Zincir üstü aktif her Masternode (`evodb` KTDIP3 yasaksız geçerli listesi veya `IsMasternodeActiveAtHeight(..., 2880)` ve `IsValidMasternodeCollateralAndAuth` ile doğrulanır), dönen ADAM tohumu ve kendi teminat UTXO çıktısı kullanılarak deterministik olarak puanlanır:

  $$\text{Score}_i = \text{SHA256D}\left(\text{Seed}_{\text{seedHeight}} \mathbin{\Vert} \text{collateralTxid}_i \mathbin{\Vert} \text{collateralVout}_i\right)$$

- Adaylar $\text{Score}_i$ değerine göre artan sırada dizilir ve **ilk $N$** aday (`nQuorumSize` $\in \{5, 9, 11, 15\}$) aktif kuorumu oluşturur. Zincir üstü aktif Masternode sayısı 5'in altındaysa, `ElectQuorumMembers` gerçek Aktif Masternode'ları (`1..4`) korur ve yalnızca eksik koltukları `GetAdamMinerPool(nHeight - 1, pView)` içindeki kota-izoleli LLMQ yedek adaylarından (en fazla 1 Coin-Lock + en fazla 3 Fonlu PoW-Lock, ardından taşma -> Bootstrap -> Zero-Coin -> Coin-Lock yedeği — 15 koltuklu `8+2+5` Model D ADAM komitesinden ayrıdır) deterministik olarak doldurur.
- **Epoch Tohumu ve Blok Bazlı Canlılık**: Rotasyon aktivasyonu öncesinde `GetActiveQuorum`, `nDkgHeight = max(vUpgrades[UPGRADE_POMBL].nActivationHeight, (nHeight / dkgInterval) * dkgInterval)` yüksekliğinde `seedHeight = nDkgHeight - 1` kullanarak `RunDKG(nDkgHeight)` çalıştırır. Epoch-tohum rotasyonu aktifleştiğinde ise `GetActiveQuorum`, `seedHeight = ((nHeight / dkgInterval) * dkgInterval) - 1` (`dkgInterval` Mainnet üzerinde `100`, Testnet/Regtest üzerinde `10`) yüksekliğindeki deterministik epoch tohumunu kullanarak zincir üstü Masternode canlılığını **her blok yüksekliğinde** (`RunDKG(nHeight)`) yeniden denetler; böylece dönem boyunca kararlı DKG kimliği korunurken teminatı harcanan veya 2.880 blokluk heartbeat süresi dolan düğümler anında rotasyon dışına çıkarılır:
  - **Regtest**: Blok yüksekliği $\ge 800$
  - **Testnet**: Blok yüksekliği $\ge 1.000$
  - **Mainnet**: Blok yüksekliği $\ge 1.400$

---

## 9. Çevrimdışı Koordinatör Geri Çekilme Rotasyonu (`GetAdamFallbackCoordinatorForSlot`)

Birincil seçilen Koordinatörün çevrimdışı olması durumunda **hem** Fallback Modunda (Blok Versiyonu 11) **hem de** Standart Modda (Blok Versiyonu 12; Model D öncesi `4.800..4.999` blokları ve Model D $\ge 5.000$ blokları dahil) zincirin kilitlenmesini önlemek amacıyla:

### A. Birincil Koordinatör Münhasırlık Penceresi ($t < 45\text{ sn}$)
Ebeveyn bloğun zaman damgasından itibaren geçen ilk **45 saniye** boyunca (`nBlockTimeDiff < ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS = 45`):
* **Versiyon 12 (`nHeight >= UPGRADE_POMBL`)**: Hem `CheckBlock` (`src/main.cpp`) hem de blok üretimi (`src/miner.cpp`), `vAdamVRFProof` ve `vAdamCoordinatorSig` alanlarını **yalnızca** `SelectAdamNodes` tarafından seçilen birincil `expectedCoordinator` anahtarının imzalamasını zorunlu kılar. $t < 45\text{ sn}$ içinde yapılan erken yedek rotasyon denemeleri Koordinatör imzası doğrulamasından geçemez ve reddedilir (`bad-adam-coord-sig`).
* **Versiyon 11 (`UPGRADE_ADAM <= nHeight < UPGRADE_POMBL`)**: Madenciler ve paydaşlar `vAdamMiners.back()` anahtarını döndürmeden önce 45 saniyelik birincil pencereyi bekler; `CheckBlock` ise `vAdamMiners.back()` konumundaki kendi kendine yeten Koordinatör anahtarını doğrular.

### B. PoBLS XOR Sıralamalı Yedek Rotasyon ($t \ge 45\text{ sn}$)
`nBlockTimeDiff >= 45` saniye olduğunda, koordinatörlük yetkisi geçerli bulmaca çözümü sunmuş komite doğrulayıcıları arasında **30 saniyelik dilimlerle** döndürülür (`src/adam.cpp` içindeki `GetAdamFallbackCoordinatorForSlot`):
1. Bulmacasını çözmüş komite doğrulayıcıları `UPGRADE_HARDENING_V4` altında önce VRF olgunluğuna göre (`RankValidatorsByPoBLSDistance` içindeki `maturedKeys` ile `ADAM_VRF_KEY_MATURITY = 100` blok) ve ardından PoBLS XOR mesafelerine göre $D_i = |T_i \oplus T_{\text{target}}|$ artan sırada deterministik olarak sıralanır (bilet verisi yoksa komite sırası kullanılır).
2. `nRawSlot = (nBlockTimeDiff - 45) / 30` dilimi (`nSlotOffset = (nBlockTimeDiff - 45) % 30`) için aktif yedek Koordinatör (`activeFallback`) sıralı doğrulayıcı listesi üzerinden modüler olarak seçilir; `CheckBlock` ise 30 saniyelik slot geçişlerindeki küçük P2P yayılım gecikmelerini tolere etmek amacıyla `expectedCoordinator`, `activeFallback` veya 5 saniyelik sınır tolerans penceresi içinde (`nRawSlot > 0 && nSlotOffset < 5`) bir önceki dilimin `prevFallback` anahtarını geçerli kabul eder.
3. Versiyon 12 bloklarında `vAdamMiners` her zaman tam `GetAdamMinersCount(nHeight)` komite doğrulayıcısı düzenini korur (**Model D altında `15` koltuk / `vAdamSolutions.size() == 15`**; öncesinde `11`); yedek Koordinatör `vAdamMiners` dizisinin boyutunu değiştirmeden doğrudan `vAdamVRFProof` ve `vAdamCoordinatorSig` alanlarını imzalar.

---

## 10. Konsensüs Kesinliği, Çatal Çözümü (Fork Resolution) ve Performans İyileştirmeleri (v1.4.5.9 – v1.5.9.0)

### A. Deterministik Eşit İş (Equal-Work) Çatal Çözücü (`CBlockIndexWorkComparator`)
30 saniyelik işbirlikçi PoW/PoS hibrit zincirinde, ağ bölünmeleri veya eşzamanlı geri çekilme rotasyonları aynı yükseklikte ve eşit kümülatif zincir işine (`nChainWork`) sahip rakip bloklar üretebilir. Önceden `src/chain.h` içindeki `CBlockIndexWorkComparator`, blok özetlerini karşılaştırmadan önce yerel geliş sırasına (`nSequenceId`) bakıyordu; bu da bölünmüş alt ağlardaki düğümlerin bir sonraki blok kazılana kadar farklı zincir uçlarında kalmasına yol açabiliyordu.
* **Kural**: `pa->nChainWork == pb->nChainWork` olduğunda, karşılaştırıcı `nSequenceId` kontrolünden önce sayısal olarak daha küçük blok özetine (`pa->GetBlockHash() < pb->GetBlockHash()`) sahip blok indeksini deterministik olarak tercih eder.
* **Etki**: Ağ bölünmesi iyileştiğinde ve rakip bloklar yayıldığında, tüm düğümler bir sonraki bloğu beklemeden anında (`< 1s` içinde) aynı kanonik zincir ucunda birleşir.

### B. LLMQ Çifte İmza Önleme (Anti-Equivocation) ve Çok Sekmeli (Multi-Hop) Gossip İletimi
* **Ebeveyn Blok Bazlı Çifte İmza Koruması (`llmq::AllowQuorumSignature` / `mapSignedProposalByPrev`)**: `src/llmq.cpp` içinde her LLMQ kurul üyesi `mapSignedProposalByPrev[prevHash]` kaydını tutar. Bir kurul üyesi belirli bir `prevHash` üzerinde geçerli bir `PROPOSEBLOCK` imzaladığında, rakip koordinatörlerin alternatif blok önerilerini **`QUORUM_EQUIVOCATION_GUARD_SECS = 45`** saniye boyunca (V4 öncesi `30s`'den yükseltilmiştir) imzalamayı reddeder ve aynı koordinatörün aynı ebeveyn üzerinde rakip blokla çifte-imza girişimini (`mapCoordBlockHash` — **`12s` rakip-blok yeniden denemesi olmaksızın**; yalnızca birebir aynı `blockHash` yeniden yayınına izin verilir) engeller. Farklı bir yedek koordinatör için `45s` geçtikten sonra dahi önceki öneri `mapBlockIndex`'e girmiş veya eşik imza payına (`GetQuorumThreshold`) ulaşmışsa geçiş reddedilir ve elenen blok özetleri bir daha önerilemeyecek şekilde `rejectedBlocks` içinde izlenir. Bu sayede aynı dilimde iki rakip bloğun ikisinin birden BFT korum sertifikası (`vQuorumSig`: `UPGRADE_HARDENING_V4` altında **`4-of-5`..`11-of-15`** [`floor(2N/3)+1`]; V4 öncesi `3-of-5..8-of-15`) alması önlenir.
* **Çok Sekmeli Gossip İletimi (`setSeenProposedBlocks`, `mapQuorumBlockSigs`)**: `PROPOSEBLOCK` (`qblockprop`) ve `QUORUMSIGSHARE` (`qsigshare`) P2P mesajları tekilleştirilerek tüm bağlı eşlere iletilir; böylece doğrudan (tek sekmede) bağlı olmayan korum üyeleri ve blok önericileri de öneri ve imza paylarını eksiksiz alır.
* **`ElectQuorumMembers` İçinde %100 Zincir Üstü (On-Chain) Yedek Havuz**: Zincir üstü aktif Masternode sayısı (`evodb` KTDIP3 yasaksız MN'ler veya 2.880 blokluk heartbeat penceresi) `5` değerinin altındaysa, `ElectQuorumMembers` fonksiyonu gerçek Aktif Masternode'ları (`1..4`) korur ve yalnızca `5`'e kadar eksik kalan koltukları bellek içi P2P `mnodeman` durumuna başvurmadan doğrudan zincir üstü `GetAdamMinerPool(nHeight - 1, pView)` kota-izoleli aday havuzundan deterministik olarak tamamlar.

### C. Doğrulanmış Zincir Üstü Masternode Heartbeat Ping Desteği (`ExtractOnChainMasternodePing` & `SendOnChainPing`)
* **Sıkıştırılmış (33-bayt) ve Sıkıştırılmamış (65-bayt) Açık Anahtar Desteği**: `src/adam.cpp` içindeki `ExtractOnChainMasternodePing`, serileştirilmiş `CDataStream` `OP_RETURN` kanıtıyla (`collateralOutpoint` + `Hash(collateralOutpoint || tx.vin[0].prevout)` üzerindeki `vchPingSig` imzası + isteğe bağlı `bool hasDelegation` + `COnChainMasternodeDelegation`) birlikte hem 36 baytlık (`0x21 <33-bayt sıkıştırılmış pubkey> OP_CHECKSIGVERIFY OP_1`) hem de 68 baytlık (`0x41 <65-bayt sıkıştırılmamış pubkey> OP_CHECKSIGVERIFY OP_1`) MESCAL `check-signature-verification` heartbeat betiklerini doğrular.
* **İletim Ücreti Muafiyeti (`0.0001 KRISTA`)**: Kimliği doğrulanmış zincir üstü Masternode ping işlemleri `GetMinRelayFee` (`src/main.cpp`) içindeki `1000x` `OP_RETURN` ücret çarpanından muaftır ve sabit `0.0001 KRISTA` işlem ücretiyle yayınlanır.

### D. Kilitsiz (Lock-Free) Zincir Ucu Erişimi (`CChain::AtomicTip()`)
* `CChain` (`src/chain.h` / `src/chain.cpp`), `CChain::SetTip()` içinde güncellenen atomik bir `std::atomic<CBlockIndex*> pAtomicTip` işaretçisi tutar.
* `GetChainTip()` (`src/main.cpp`), `cs_main` kilidini almadan doğrudan `chainActive.AtomicTip()` değerini okur; böylece P2P mesaj işleme, RPC sorguları ve staking/madencilik iş parçacıkları arasındaki kilit çekişmesi (lock contention) ortadan kaldırılır.

### E. Merkeziyetsiz PoS Ortak İmzalama, Kriptografik Gelecek Kuyruğu ve Tek Dağıtıcılı Çözücü (`Rev 40–42`)
* **Merkeziyetsiz `POSCOORDREQ` / `POSCOORDSIG` P2P Ortak İmzalama (`K4` / `R42-F2`)**: Aktif Koordinatör uzak bir düğümdeyken PoS paydaşlarının P2P üzerinden `vAdamVRFProof` ve `vAdamCoordinatorSig` imzalarını güven gerektirmeden almasını sağlar (`VerifyPoSCoordRequestAuth`, `TestBlockValidity`, `AcquireCoordinatorProposalSlot` çift PoW+PoS üretim koruması ve `POSCOORDSIG` alındığında `mapSeenPoSCoordReqs` üzerindeki `targetCoordinator`, `draftHeader` ve ebeveyn `adamSeed` ile önbellekleme/aktarım öncesi tam `VerifyAdamVRFProof` + `VerifyAdamCoordinatorSig` doğrulaması).
* **Sınırlandırılmış `mapFutureBlocks` Kriptografik Ön-Doğrulaması (`K1` / `R42-F1`)**: `IsFutureBlockPreVerified(header, pindexPrev)`, `mapBlockIndex` içinde bilinen bir ebeveyn (`header.GetBlockTime() > pindexPrev->MinPastBlockTime()`) zorunlu kılar; `nVersion >= 11` için `VerifySelfContainedBLSSignedData` (`vrfBlsPk == coordBlsPk`) / DIP3 operatör BLS (`VerifyAdamOrDip3Signature`) ve `GetAdamMinerPool` doğrulayıcı sahipliğini (`VerifyAdamVRFProof` + `VerifyAdamCoordinatorSig`), `nVersion >= 12` ve `IsModelDActive` durumunda ise aktif LLMQ kurulu üzerinde dinamik BFT süper çoğunluğu (`UPGRADE_HARDENING_V4` altında **`4-of-5`..`11-of-15`** [`floor(2N/3)+1`]; V4 öncesi `3-of-5`..`8-of-15`) `CQuorumSignature::Verify` imzasını doğrular; ayrıca kuyruktaki tam `CBlock` kayıtlarının `HEADERS` mesajlarıyla ezilmesini önler ve `AddInventoryKnown` çağrısını `ProcessFutureBlocks` aşamasına erteler.
* **Tek Dağıtıcılı Strided Çözücü, Deterministik `DeriveBLSFromCKey` ve Yaşam Döngüsü Korumalı Koordinatör Birleştirme (`O2`–`O5` / `R42-F3`)**: Çok çekirdekli `BitcoinMiner` bulmaca aramasını mükerrer nonce taraması olmadan koordine eder, `AutoRegisterMiner` ve `registerminer` yollarını ortak `CMinerRegistrationSlotGuard` ve deterministik `DeriveBLSFromCKey` ile birleştirir, `TryAssembleCoordinatorBlockAsync` iş parçacığını `GenerateBitcoins` geçişlerinde ve `g_connman.reset()` sonrası kapanışta güvenli biçimde sonlandırır ve `ccminer` tarafına Versiyon 12 3-permütasyonlu bileşik bulmaca desteğini (`<a1>+<a2>+<a3>+<minerIdx>`) ekler.

### F. `UPGRADE_HARDENING_V4` ve `ADAMSOL` P2P Yakınsaması (`v1.5.9.0`, `PROTOCOL_VERSION = 70983`)
* **`UPGRADE_HARDENING_V4` (`"Hardening_v4"`, Mainnet / Testnet / Regtest üzerinde Yükseklik `1`; `MIN_PEER_PROTO_VERSION = 70983`)**:
  - **LLMQ 2/3 BFT Süper Çoğunluğu (`H-7`) ve Çifte İmza Önleme**: `llmq::GetQuorumThreshold(N, h)`, `floor(2N/3) + 1` (`4-of-5`, `7-of-9`, `8-of-11`, `11-of-15`) uygular; `QUORUM_EQUIVOCATION_GUARD_SECS = 45` aynı koordinatörün rakip blok eşdeğerliğini reddeder ve önceki öneri `mapBlockIndex`'e girdiğinde veya eşik paya ulaştığında geçişi engeller (`rejectedBlocks`).
  - **VRF Anahtar Olgunluğu ve `ProUpRegTx` Bekleme Süresi**: `ADAM_VRF_KEY_MATURITY = 100` blok (`SelectAdamNodes` içinde `maturedCandidates` ve `RankValidatorsByPoBLSDistance` içinde `maturedKeys` önceliği); `ProUpRegTx` operatör BLS rotasyonları arasında (`nOperatorKeyHeight`) `consensus.nProUpRegCooldownBlocks = 100` blok bekleme (`bad-proupreg-cooldown`).
  - **Zorunlu BLS+PoP Madenci Kayıt Sabitlemesi**: Coin-Lock, Fonlu PoW-Lock ve Zero-Coin çıktıları `<blsPubKey(48B)> <blsPoP(96B)> OP_2DROP` ön ekini taşımak ZORUNDADIR (`bad-txns-minerreg-missing-bls`, `bad-txns-minerreg-bls-pop`, `bad-txns-minerreg-dup-bls`); PoW ön-görüntüleri `"POWLKv4"` / `"SYBILV4"` son ekleriyle `Hash(challenge || blsPubKey)` bağlar; LevelDB `"adamregcache_v3"` içinde ve çatal-farkındalıklı `PinnedBLSEntry` (`blockHash` + aktif-zincir yüklemi) ile tutulur; `VerifyAdamOrDip3Signature` imzalamayı yalnızca sabitlenen BLS anahtarıyla sınırlar.
  - **Model D Bootstrap Eşiği `8` ve PoW CoinStake Yasağı**: `GetAdamThreshold(nHeight, nActiveMasternodes)`, `nActiveMasternodes >= 8` olduğu anda bootstrap döneminde dahi **`8`** (`> 7` MN-dışı koltuk) zorunlu kılar (`< 8` MN iken `5`); PoW bloklarındaki `IsCoinStake()` işlemleri reddedilir (`bad-cs-in-pow`) ve `fAllowCoinStake` `CheckTxInputs` kontrolünü sıkı şekilde sınırlar.
  - **PoSe Adaleti (`M-4`), R35-1 `5s` İhmal Vetosu, `PoM` Çalışma Süresi ve `burncache_v3`**: LLMQ imzalamama artık `+5` PoSe cezası üretmez (`-5` iyileşme korunur), yedek ikameyle düşürülen sessiz birincil ADAM komite üyeleri ise `+5` PoSe cezası alır (kaçırılan birincil koordinatör `+5` ile birlikte); `qblockprop`, yerelde `>= 5 sn` önbellekte bulunan (`mapAdamSolutionFirstSeen`) birincil doğrulayıcı çözümünü atlayan önerileri veto eder; KTDIP3 `PoM` çalışma süresi kesin olarak `std::max(nRegisteredHeight, nPoSeRevivedHeight)` ile başlar; `InitializeBurnCache()`, V3 zincirlerinde temiz tarama yapar (`burncache_v3`) ve `mapBurnTransferUndo` kayıtlarını yükseklik sırasıyla tahliye eder.
* **`ADAMSOL` `VERACK` Eşitlemesi, Tıkanma Yeniden Yayını ve `AutoRegisterMiner` Düzeltmesi (`63475e416`)**: `VERACK` üzerinde eşlere güncel uca ait en fazla `MAX_ADAMSOL_SYNC_PER_PEER = 32` önbelleklenmiş `ADAMSOL` mesajı (`CollectCachedAdamSolutionsForTip`) gönderilir ve uç geciktiğinde her `60s`'de bir (`ADAMSOL_REBROADCAST_INTERVAL_SECS = 60`) yeniden yayınlanır; `BitcoinMiner` içinde `AutoRegisterMiner` yalnızca `!amnodeman.GetActiveMasternodesCopy().empty()` kontrolüyle sınırlandırılır.

