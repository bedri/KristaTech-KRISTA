# MPA (Multi-Proof Algorithm) Konsensüs Özellikleri

## 1. Giriş ve Arka Plan

**Multi-Proof-Algorithm (MPA)**, ADAM işbirlikçi konsensüs çerçevesinin üzerine uygulanan hibrit bir konsensüs mekanizmasıdır. Düğüm (node) operatörlerini teşvik etmek, likiditeyi kilitlemek ve güvenli blok üretimini sağlamak için Proof-of-Stake (PoS) ile özel kriptografik kanıt mekanizmalarını birleştirir.

MPA, bir arada var olan dört staking kanıt türü sunar:
1. **Proof of Stake (PoS - Baseline)**: Standart coin yaşı tabanlı blok üretimi.
2. **Proof of Lock (PoL)**: CLTV veya CSV zaman kilitleri (timelocks) içeren çıktı betikleri (output scripts) kullanılarak yapılan staking olup, daha uzun kilit süreleri için ekstra madencilik ağırlığı (mining weight) kazandırır.
3. **Proof of Burn (PoB)**: Coin'lerin belirlenmiş harcanamaz bir adrese yakılması (burning) olup, zamanla doğrusal olarak azalan bir madencilik ağırlığı çarpanı sağlar.
4. **Proof of Masternode (PoM)**: Aktif Masternode teminatı ve bunun sürekli çalışma süresine (uptime) bağlı olarak madencilik ağırlığı tahsis edilmesi.

Bu ağırlık metrikleri, blok doğrulama sırasında zorluk hedeflerini (difficulty targets) doğrudan ölçeklendirerek, kaynak taahhüdünün doğrudan konsensüs gücüne dönüştüğü çok katmanlı bir konsensüs topolojisi oluşturur.

---

## 2. Konsensüs Mimarisi ve Parametreleri

MPA, `Consensus::UPGRADE_POMBL` ağ yükseltmesi (`vUpgrades[Consensus::UPGRADE_POMBL].nActivationHeight`) ile etkinleştirilir. Temel yapılandırması `src/consensus/params.h` içinde yer alır ve ağ başına `src/chainparams.cpp` içinde tanımlanır.

### Temel Parametreler
* **`vUpgrades[Consensus::UPGRADE_POMBL].nActivationHeight`**: MPA konsensüs kurallarının etkinleştiği blok yüksekliği (`PoMBL`, **Proof of Masternode, Burn and Lock** anlamına gelir). Bu yüksekliğin altında ağ, eski kurallar veya Sürüm 11 ADAM kuralları altında çalışır.
* **`nTargetSpacing`**: Blok üretimi için hedef aralık (Mainnet ve Testnet üzerinde `30` saniye, Regtest üzerinde `10` saniye).
* **`mBurnAddresses`**: Kayıtlı harcanamaz yakma adreslerini (burn addresses) ve bunların aktif başlangıç yüksekliklerini içeren bir harita (map).

### Ağ Etkinleştirme Yükseklikleri
| Ağ | `UPGRADE_POMBL` Etkinleştirme Yüksekliği | Zorunlu Blok Sürümü |
| :--- | :--- | :--- |
| **Mainnet** | 4800 | Sürüm 12 |
| **Testnet** | 400 | Sürüm 12 |
| **Regtest** | 300 | Sürüm 12 |

---

## 3. Madencilik Çekirdek Ağırlığı Hesaplamaları

Hibrit MPA sistemi altında bir sonraki bloğu kazma olasılığı, çekirdek değerlendirme (kernel evaluation) denklemi tarafından yönetilir (`src/kernel.cpp` içindeki `CStakeKernel::CheckKernelHash`):

$$\text{KernelHash} < \text{Target} \times \lfloor \text{Weight}_{\text{Total}} / 100 \rfloor$$

Verilen bir işlem çıktısı (UTXO) veya Masternode teminatı için madencilik $\text{Weight}_{\text{Total}}$ değeri, `src/kernel.cpp` içerisindeki `CalculateMPAWeight()` fonksiyonunda **1. PoM $\rightarrow$ 2. PoL $\rightarrow$ 3. PoB $\rightarrow$ 4. PoS** öncelik sırasıyla hesaplanır ve `UPGRADE_HARDENING_V3` (`Yükseklik 1`) kapsamında dal-izoleli çatal işleme ile `SetNewStakeModifier(prevoutId)` üzerinden deterministik olarak hesaplanan 256-bitlik bir `v2StakeModifier` (`CBlockIndex::GetStakeModifierV2`) ile tohumlanır:

### 3.1. Proof of Stake (PoS - Baseline)

$$W_{\text{PoS}} = \text{Amount}$$

### 3.2. Proof of Lock (PoL) — Canlı Kendi Kendini Yenileme (`UPGRADE_FAIRNESS`)
Blok yüksekliği tabanlı mutlak (`OP_CHECKLOCKTIMEVERIFY`) veya göreceli (`OP_CHECKSEQUENCEVERIFY`) zaman kilitleri (timelocks) kullanılarak $L_{\text{MAX}}$ değerine kadar kalan/göreceli $L$ blok süresi boyunca kilitlenen işlem çıktılarına uygulanır:

$$W_{\text{PoL}} = \text{Amount} \times \left(1 + \gamma \cdot \min\left(\frac{L}{L_{\text{MAX}}}, 1.0\right)\right)$$

* $\gamma$ kilit çarpanı parametresidir (varsayılan: `2.0`, 3 kata kadar ağırlık bonusu verir).
* $L_{\text{MAX}}$ değerlendirilen maksimum kilit süresidir (varsayılan: `50.000` blok).
* **Kilit Koruyan Canlı Yenileme (`UPGRADE_FAIRNESS`, Blok 1)**: Standart blok yüksekliği CLTV `TX_CLTV_TIMELOCK` çıktıları (`<lockTime> OP_CHECKLOCKTIMEVERIFY OP_DROP OP_DUP OP_HASH160 <20B pubkeyHash> OP_EQUALVERIFY OP_CHECKSIG`, `0 < lockTime < 500.000.000`) `CWallet::AvailableCoins(STAKEABLE_COINS)` tarafından seçilebilir ve kilit süresinin dolmasını beklemeden canlı staking yapabilir (`IsValidPoLSelfRenewal`): coinstake işlemi tam `1` girdi (`vin.size() == 1`) harcar, `vout[1].scriptPubKey == prevout.scriptPubKey` koşulunu (kilit yüksekliğini ve sahip anahtar hash'ini, bölünme olmadan) korur ve `vout[1].nValue >= prevout.nValue` tutarını (ana para + yeni staking ödülü kilit bitimine dek kilitli kalır) garanti eder. Süresi dolmamış `TX_CLTV_TIMELOCK` UTXO'ları standart transferlerde harcanamaz kalır.

### 3.3. Proof of Burn (PoB) — `COutPoint` Başına Soy Hattı (`UPGRADE_HARDENING_V3` & `UPGRADE_HARDENING_V4`)
Standart (coinbase / coinstake olmayan: `!tx.IsCoinBase() && !tx.IsCoinStake()`) işlemlerle kayıtlı harcanamaz bir yakma adresine (Mainnet üzerinde `KTburnCYcDjF1GsXa5k3tc3eZrVVbqefyWr` / `KTburnUHdmKMVx4z8BcnZYZCWYKxRgfZp95`, Testnet üzerinde `ktBurn42LtQP2pJ2fS5X2kpRx4Sd86kNgx4` / `ktBurnKmNRzVXVVVDXxFhhKytkH5x2yqsCG`) gönüllü olarak gönderilen coin'ler, yakma bloğundan bu yana geçen süre olan $T$ (blok cinsinden) boyunca doğrusal olarak sıfıra düşen kişisel bir PoB staking ağırlık çarpanı alır. `CoinBase` veya `CoinStake` çıktılarındaki otomatik protokol yakımları (sahipsiz Model D koltuk payları) ise yalnızca `nMoneySupply` arzından kalıcı olarak düşülür ve kişisel PoB ağırlığı **oluşturmaz**.

**`UPGRADE_HARDENING_V3` (tüm ağlarda `Yükseklik 1`)** ve **`UPGRADE_HARDENING_V4` (`Yükseklik 1`)** kapsamında:
* **`COutPoint` Başına Bağlama (`mapOutpointBurns`, LevelDB `"burncache_v3"` içinde kalıcı):** Adres üzerindeki tüm UTXO'ları kredilendirmek (`mapAddressBurns`) yerine, gönüllü PoB yalnızca yakma işleminde belirlenen harcanabilir UTXO `COutPoint` hedefine (`OP_RETURN` içinde `"KTPOB"` hedef etiketi veya varsayılan olarak işlemin ilk harcanabilir çıktısı) bağlanır. `UPGRADE_HARDENING_V4` altında `InitializeBurnCache()`, aktif V3 zincirlerinde eski V2 adres-yakım önbellek kayıtlarına güvenmek yerine temiz bir V3 yeniden taraması (`burncache_v3`) gerçekleştirir.
* **Coinstake `vout[1]` Soy Aktarımı ve Yükseklik Sıralı Tahliye:** PoB bağlı bir UTXO başarıyla bir blok stake ettiğinde, `TransferOutpointBurnOnCoinstake` kalan azalmış yakım soy hattını atomik olarak Coinstake `vout[1]` çıktısına aktarır ve harcanan girdi outpoint'ini kaldırır (`mapBurnTransferUndo` girişleri `nHeight` bilgisini izler ve `UPGRADE_HARDENING_V4` altında `2.048` kayıt sınırında en eski girişleri kesin yükseklik sırasıyla tahliye eder); `DisconnectBlock` aktarımı `UndoOutpointBurnTransferForBlock` ile atomik olarak geri alır ve `CPivStake::CreateTxOuts` yakım ağırlığının çoğaltılmasını veya parçalanmasını önlemek için PoB bağlı outpoint'lerde stake bölmeyi devre dışı bırakır.

$$W_{\text{PoB}} = \text{Amount} + \min\left(\text{BurnWeight}_{\text{aktif}}, \text{Amount} \times \beta\right)$$

burada $\text{BurnWeight}_{\text{aktif}} = \text{BurnAmount} \times \beta \times \left(1 - \frac{T}{T_{\text{MAX}}}\right)$.

* $\beta$ yakma teşvik çarpanıdır (varsayılan: `5.0`).
* $T_{\text{MAX}}$ (`nBurnDecayBlocks`) azalma (decay) eşiğidir (Mainnet'te `500.000` blok, Testnet/Regtest'te `10.000` blok). $T \ge T_{\text{MAX}}$ olduğunda yakma bonusu 0'a düşer (geriye temel staking UTXO miktarı kalır).

### 3.4. Proof of Masternode (PoM) — `UPGRADE_FAIRNESS` Altında Azaltılmış Katsayı (`1,25x`, $\alpha = 0,25$)
Teminatı $C$ (`4.200 KRISTA`) ve sürekli aktif kalma süresi $t_{\text{active}}$ (`UPGRADE_HARDENING_V4` altında zincir üstü `evodb` KTDIP3 üzerinden kesin olarak **`std::max(nRegisteredHeight, nPoSeRevivedHeight)`** başlangıcıyla — kayıt öncesi `nCoinHeight` artışı olmaksızın ve PoSe yasaklı bir MN `ProUpServTx` ile canlandırıldığında çalışma süresini sıfırlayarak — veya eski `IsMasternodeActiveAtHeight(..., 2880)` ile deterministik hesaplanan; V4 öncesi `std::min(nRegisteredHeight, nCoinHeight)` alıyordu) olan aktif Masternode'lar:

$$W_{\text{PoM}} = C \times \left(1 + \alpha \cdot \min\left(\frac{t_{\text{active}}}{T_{\text{MAX}}}, 1.0\right)\right)$$

* **`UPGRADE_FAIRNESS` (Mainnet, Testnet ve Regtest üzerinde Blok 1) kapsamında**: $\alpha = 0,25$ (**en fazla `1,25x`**, yani `C + floor(C * capped / (4 * T_MAX))`). Dengeli Model D altında Masternode'lar halihazırda `%5` Pasif MN, `%10` İmzacı Odaklı LLMQ ve `15` koltuktan en fazla `8` Eşit Doğrulayıcı (`%60/15 = %4,00`/koltuk, en fazla `%32`; Model D öncesi `11` koltuktan en fazla `7` koltuk, `%75/11 ≈ %6,818`) payı aldığı için $\alpha$ katsayısının `1,0`'dan `0,25`'e indirilmesi, normal staker'ların önüne geçmeden (`PoS 1,0x < PoM 1,25x << PoL 3,0x < PoB 6,0x`) ölçülü bir uzun süreli kesintisiz çalışma (uptime) sadakat bonusu korur.
* $T_{\text{MAX}}$ (`nMasternodeUptimeLimit`) Mainnet'te **`100.000` blok**, Testnet ve Regtest'te **`10.000` bloktur**.
* *(Tarihsel Fairness öncesi parametre yalnızca `UPGRADE_FAIRNESS` öncesi yükseklikler için geçerlidir: $\alpha = 1,0$, en fazla `2,0x`.)*

---

## 4. Uzun Ömürlü Masternode Kurulları (Long-Living Masternode Quorums - LLMQs)

Güvenli lider seçimi ve imza birleştirmeyi desteklemek için LLMQ'lar, `UPGRADE_HARDENING_V3` altında **`BLSDomain::LLMQ_BLOCK` (`"BLS_SIG_BLS12381G2_XMD:SHA-256_SSWU_RO_NUL_KRISTA_LLMQ_BLOCK_V1_"`)** (ve hazine ödemeleri için `BLSDomain::LLMQ_TREASURY`, hata durumunda kapalı [fail-closed] `Aggregate` kontrolleriyle) üzerinden alan-ayrıştırılmış **BLS12-381** (`blst`) birleşik eşik imzalarını (`CQuorumSignature::AggregateBLSSignatures`) kullanır. Birleşik **KTDIP3 (`UPGRADE_DIP3`)** kapsamında kurul üyeleri doğrudan zincir üstü kayıtlı operatör BLS anahtarıyla (`evodb` içindeki `blsPubKeyOperator`; V3 altında `PinBLSPubKey` olmaksızın boş `ecdsaSig` zorunludur) `blockHash` özetini imzalar; DIP3 öncesi ve MN dışı yedek üyeler ise `CBLSSignedData` (`secp256k1` strict DER + çatal-farkındalıklı `PinBLSPubKey`) ile yetkilendirilmiş deterministik `DeriveBLSFromCKey` anahtarlarını kullanır. Doğrulama aşamasında (`CQuorumSignature::Verify`), üye kimlikleri doğrulandıktan sonra imzalayan üyelerin BLS12-381 açık anahtarları (`CBLSPubKey::Aggregate`) ve imzaları (`CBLSSignature::Aggregate`) tek bir birleşik eşleşme denklemiyle doğrulanır.

### Kurul Seçimi ve Boyutu
* **Dinamik Kurul Boyutu ve V4 BFT Eşiği (`GetTargetQuorumSize`, `llmq::GetQuorumThreshold`)**: `UPGRADE_HARDENING_V1` ve `UPGRADE_HARDENING_V4` kapsamında komite boyutu $N$ ve gerekli BFT eşiği (V4 altında `floor(2N/3) + 1`; V4 öncesi `floor(N/2) + 1`), zincir üstü doğrulanmış aktif Masternode sayısına göre dinamik ölçeklenir: `<25` MN için **`5`** (**`4-of-5`**; V4 öncesi `3-of-5`), `25–49` için **`9`** (**`7-of-9`**; V4 öncesi `5-of-9`), `50–99` için **`11`** (**`8-of-11`**; V4 öncesi `6-of-11`) ve `>=100` MN için **`15`** (**`11-of-15`**; V4 öncesi `8-of-15`); ayrıca **`QUORUM_EQUIVOCATION_GUARD_SECS = 45`** (`AllowQuorumSignature` aynı koordinatörün rakip blok çifte-imza girişimini `12s` yeniden deneme olmaksızın reddeder ve önceki öneri `mapBlockIndex`'e girmiş veya eşik paya ulaşmışsa geçişi durdurup elenen özetleri `rejectedBlocks` içinde izler) ile korunur.
* **DKG Aralığı, Ön-Tohum Anlık Görüntüsü ve Blok Bazlı Canlılık Rotasyonu**: `GetActiveQuorum`, DKG oturumlarını Mainnet'te 1.400. bloğa kadar her **100 blokta** bir ve Testnet'te 1.000. bloğa / Regtest'te 800. bloğa kadar her **10 blokta** bir çalıştırır; bu yüksekliklerden itibaren (Mainnet üzerindeki tüm `UPGRADE_POMBL` `>= 4.800` blokları dahil) `RunDKG(nHeight)` her blokta `((nHeight / dkgInterval) * dkgInterval) - 1` yüksekliğindeki deterministik dönem tohumunu kullanarak aktif kurul üyeliğini değerlendirir. `UPGRADE_HARDENING_V3` (`C-2`) altında `pindexPrev` üzerindeki DIP3 adayları **`seedHeight - 1`** yüksekliğindeki ön-tohum `evodb` anlık görüntüsüyle kesiştirilir ve `proTxHash` üzerinden deterministik olarak puanlanır; `UPGRADE_DIP3` + `UPGRADE_HARDENING_V3` aktifleştiğinde eski yalnızca-ping (ping-only) MN'ler devre dışı bırakılır.
* **Aktif Masternode Filtreleme**: Herhangi bir ağda (Mainnet, Testnet veya Regtest) kodlanmış (hardcoded) anahtar kimliği filtrelemesi veya yerel cüzdan kısıtlaması uygulanmaz. Seçim, ağdaki tüm aktif ve geçerli Masternode'lar (`evodb` KTDIP3 veya eski zincir üstü pingler) arasından dinamik olarak yapılır.
* **Belirleyici Geri Çekilme (Deterministic Fallback)**: 5'ten az aktif Masternode mevcutsa, `ElectQuorumMembers` gerçek Aktif Masternode'ları (1..4) kurulda korur ve yalnızca 5'e kadar eksik kalan koltukları `GetAdamMinerPool(nHeight - 1)` kota-izoleli aday havuzundan (en fazla 1 Coin-Lock + en fazla 3 Fonlu PoW-Lock, ardından taşma -> Bootstrap -> Zero-Coin -> Coin-Lock yedeği) deterministik olarak doldurur. Regtest'te ise zincir üstünde Masternode veya Coin-Lock bulunmadığında otomatik testleri kolaylaştırmak amacıyla **20 deterministik anahtar** (`GetAdamMinerPool` / `GetMasternodePrivKey`) kullanılır.

### İmza ve Eşik Doğrulaması
* Kurul üyeleri kayıtlı `blsPubKeyOperator` (KTDIP3) veya `secp256k1` ile yetkilendirilmiş `DeriveBLSFromCKey` anahtarlarıyla blok özetini imzalar ve `CQuorumSignature::AggregateBLSSignatures` ile imzalar birleştirilir.
* Sürüm `>= 12` olduğunda (`Consensus::UPGRADE_POMBL` yükseltmesiyle deterministik olarak — Mainnet'te 4.800, Testnet'te 400 ve Regtest'te 300 — Standart Mod aktifleştiğinde) kurul imzası `vQuorumSig` blok başlıklarına doldurulur. Model D (`UPGRADE_MODELD`), Mainnet'te 5.000, Testnet'te 500 ve Regtest'te 200 yüksekliğinde etkinleşen ayrı bir yükseltmedir.
* **Kurul Doğrulama Eşiği (`src/llmq.cpp` içindeki `CQuorumSignature::Verify`)**:
  - **Model D Aktivasyonundan Önce (Mainnet / Testnet üzerinde `!NetworkUpgradeActive(nHeight, UPGRADE_MODELD)`)**: Doğrulama eşiği **0 imzadır** (aktif LLMQ kurulları zorunlu hale gelmeden önce bootstrap sürecini kolaylaştırmak için doğrulama atlanır - bypassed).
  - **Model D Aktif Olduktan Sonra (Mainnet, Testnet ve Regtest)**: Hem blok üretimi (`miner.cpp`) hem de konsensüs doğrulaması (`llmq.cpp`), tüm ağlarda `llmq::GetQuorumThreshold(quorum.members.size(), nSigHeight)` ile — `UPGRADE_HARDENING_V4` altında katı **`floor(2N/3) + 1` BFT süper çoğunluğu** (**`4-of-5`**, **`7-of-9`**, **`8-of-11`** veya **`11-of-15`** imza; V4 öncesi `floor(N/2)+1`: `3-of-5`, `5-of-9`, `6-of-11`, `8-of-15`) — değerini zorunlu kılar.

> [!NOTE]
> **Eşik Değerleri Arasındaki Fark:**
> **ADAM İşbirlikçi Madencilik Eşiği** (madenci bulmaca çözümleri `vAdamSolutions` için Dengeli Model D'de 15'te 8, Model D öncesinde 11'de 7 kuralı) ile **LLMQ İmza Doğrulama Eşiği** (V4 altında blok imzası `vQuorumSig` için `floor(2N/3) + 1` BFT süper çoğunluğu) karıştırılmamalıdır:
> * **ADAM Komitesi ve Eşiği**: `GetAdamMinersCount`, Dengeli Model D altında **`15` komite koltuğu** (`8 MN + 2 Coin-Lock + 3 Fonlu PoW-Lock + 2 Ücretsiz Katılım`), Model D öncesinde **`11` koltuk** (`7 + 1 + 3`) seçer. `GetAdamEffectiveThreshold`, `vAdamSolutions` içinde en az `GetAdamThreshold(nHeight, nActiveMasternodes)` kadar geçerli çözüm bulunmasını şart koşar (`nAdamThresholdBootstrapLimit` [Mainnet `50.000` / Testnet `100.000` / Regtest `1.000`] altında aktif MN `< 8` iken **`5`**, Model D + `UPGRADE_HARDENING_V4` altında `nActiveMasternodes >= 8` olduğu anda derhal **`nAdamThresholdModelD = 8`** [`> 7` MN-dışı koltuk], bootstrap sonrasında Mainnet/Regtest üzerinde **`8`** [Model D öncesi `nAdamThreshold = 7`] ve Testnet üzerinde **`3`**) — $t \ge 120\text{ sn}$ durumunda zamana bağlı gevşetme ile Model D altında `nActiveMasternodes >= 8` iken veya bootstrap sonrasında tabanı **`ADAM_MODELD_MIN_RELAXED_THRESHOLD = 8`** olarak korur (`ADAM_MIN_RELAXED_THRESHOLD = 5` Model D öncesi; yalnızca aktif MN sayısı Model D'de `< 8` / öncesinde `< 7` olan bootstrap aşamasında `3` tabanına iner). Bu kural ADAM işbirlikçi bulmaca doğrulama katmanına aittir.
> * **LLMQ Eşiği**: Aktif LLMQ üyelerinden en az `4-of-5`..`11-of-15` tanesinin (`UPGRADE_HARDENING_V4` altında `floor(2N/3) + 1`; V4 öncesi `3-of-5..8-of-15`) BLS12-381 anahtarlarıyla blok özetini imzalamış olmasını (`vQuorumSig`) şart koşar. Bu kural merkeziyetsiz blok doğrulama katmanına aittir.

---

## 5. Konsensüs Dayatmaları ve Doğrulama Kuralları

Bir blok alındığında, `Consensus::UPGRADE_POMBL` aktif olduğunda `src/main.cpp` içerisindeki `CheckBlock()` fonksiyonundaki doğrulama kuralları aşağıdaki kontrolleri zorunlu kılar:

1. **Sürüm Zorunluluğu (Version Enforcement)**: Blok sürümü en az `12` olmalıdır. Sürümü 12'den küçük olan blok başlıkları `bad-version` kodu ile kesinlikle reddedilir.
2. **Yakma Çıktısı Bütünlüğü (Burn Output Integrity)**: Kayıtlı bir yakma adresinden harcama yapan tüm işlem girdileri, mempool katmanında (`AcceptToMemoryPool`) ve blok bağlantı katmanında (`ConnectBlock`) `bad-txns-spend-burn-address` hatasıyla kesinlikle reddedilir.
3. **Kilit Süresi Betiği Zorunluluğu (Locktime Script Enforcement)**: PoL için kullanılan kilitli işlem çıktıları kilitli betiklere kesinlikle uymalıdır ve belirtilen kilit yüksekliği/zaman damgası geçene kadar harcanamaz.
4. **Kurul Oyu Doğrulaması (Quorum Vote Validation)**: Blok başlığına dahil edilen kurul imzası (`vQuorumSig`), aktif LLMQ açık anahtarına (`UPGRADE_HARDENING_V4` altında `4-of-5..11-of-15`) karşı doğrulanır.
5. **PoW CoinStake Yasağı (`UPGRADE_HARDENING_V4`)**: `UPGRADE_HARDENING_V4` (`Yükseklik 1`) altında bir PoW bloğuna (`!block.IsProofOfStake()`) dahil edilen her `IsCoinStake()` işlemi **`bad-cs-in-pow`** ile kesinlikle reddedilir ve `fAllowCoinStake` parametresi `CheckTxInputs` kontrolünü yalnızca doğrulanmış PoS bloklarıyla sınırlar.

---

## 6. Test Etme ve Doğrulama

MPA konsensüs değişiklikleri çift taraflı bir test paketi ile tamamen kapsanmaktadır:

### 6.1. Birim Testleri (`test_kristatech`)
`src/test/mpa_tests.cpp` dosyasındaki C++ birim testleri (unit tests) şunları doğrular:
* Standart PoS için ağırlık hesaplamaları.
* Proof of Burn (PoB) için doğru azalma (decay) parametreleri ve azalma hesaplamaları.
* Proof of Lock (PoL) işlem girdileri için yükseklik ve süre hesaplamaları.

### 6.2. Fonksiyonel Testler (`consensus_pombl.py`)
Python fonksiyonel test paketi `test/functional/consensus_pombl.py`, kuralları yerel bir Regtest ağı üzerinde doğrular:
* Etkinleştirme yüksekliğine kadar blok üretimini simüle eder.
* Etkinleştirme sınırında sürümü 12'den küçük olan blokların reddedildiğini onaylar.
* Blok sürümü 12'nin kabul edildiğini onaylar.
* Bir yakma adresine fon gönderilmesinin kabul edildiğini doğrular.
* Bir yakma adresinden harcama yapma girişimlerinin engellendiğini ve `bad-txns-spend-burn-address` ile reddedildiğini doğrular.
