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

MPA, belirli bir `nPoMBLHeight` blok yüksekliğinde etkinleştirilir. Temel yapılandırması `src/consensus/params.h` içinde yer alır ve ağ başına `src/chainparams.cpp` içinde tanımlanır.

### Temel Parametreler
* **`nPoMBLHeight`**: MPA konsensüs kurallarının etkinleştiği blok yüksekliği. PoMBL, **Proof of Masternode, Burn and Lock** anlamına gelir. Bu yüksekliğin altında ağ, eski kurallar veya sürüm 11 ADAM kuralları altında çalışır.
* **`nPoMBLTargetSpacing`**: Blok üretimi için hedef aralık (30 saniye olarak yapılandırılmıştır).
* **`mBurnAddresses`**: Kayıtlı harcanamaz yakma adreslerini (burn addresses) ve bunların aktif başlangıç yüksekliklerini içeren bir harita (map).

### Ağ Etkinleştirme Yükseklikleri
| Ağ | `nPoMBLHeight` (UPGRADE_POMBL) | Zorunlu Blok Sürümü |
| :--- | :--- | :--- |
| **Mainnet** | 2000 | Sürüm 12 |
| **Testnet** | 400 | Sürüm 12 |
| **Regtest** | 300 | Sürüm 12 |

---

## 3. Madencilik Çekirdek Ağırlığı Hesaplamaları

Hibrit MPA sistemi altında bir sonraki bloğu kazma olasılığı, çekirdek değerlendirme (kernel evaluation) denklemi tarafından yönetilir:

$$\text{KernelHash} < \text{Target} \times \text{Weight}_{\text{Total}}$$

Verilen bir işlem çıktısı (UTXO) veya Masternode teminatı için madencilik $\text{Weight}_{\text{Total}}$ değeri, `src/kernel.cpp` içerisindeki `CalculateMPAWeight()` fonksiyonunda uygulanan aşağıdaki kurallar kullanılarak hesaplanır:

### 3.1. Proof of Stake (PoS - Baseline)

$$W_{\text{PoS}} = \text{Amount}$$

### 3.2. Proof of Lock (PoL)
Mutlak (`OP_CHECKLOCKTIMEVERIFY`) veya göreceli (`OP_CHECKSEQUENCEVERIFY`) zaman kilitleri (timelocks) kullanılarak $T_{\text{MAX}}$ değerine kadar $L$ blok süresi boyunca kilitlenen işlem çıktılarına uygulanır:

$$W_{\text{PoL}} = \text{Amount} \times \left(1 + \gamma \cdot \frac{L}{T_{\text{MAX}}}\right)$$

* $\gamma$ kilit çarpanı parametresidir (varsayılan: `2.0`, 3 kata kadar ağırlık bonusu verir).
* $T_{\text{MAX}}$ değerlendirilen maksimum kilit süresidir (varsayılan: `1.000.000` blok).

### 3.3. Proof of Burn (PoB)
Kayıtlı harcanamaz bir yakma adresine (örneğin `ktBurn42LtQP2pJ2fS5X2kpRx4Sd86kNgx4`) gönderilen coin'ler, yakma bloğundan bu yana geçen süre olan $T$ (blok cinsinden) boyunca doğrusal olarak sıfıra düşen önemli bir ağırlık çarpanı alır:

$$W_{\text{PoB}} = \text{BurnAmount} \times \beta \times \left(1 - \frac{T}{T_{\text{MAX}}}\right)$$

* $\beta$ yakma teşvik çarpanıdır (varsayılan: `5.0`).
* $T_{\text{MAX}}$ azalma (decay) eşiğidir (varsayılan: `500.000` blok). $T \ge T_{\text{MAX}}$ olduğunda, madencilik ağırlığı 0 olur.

### 3.4. Proof of Masternode (PoM)
Teminatı $C$ ve aktif ömrü $t_{\text{active}}$ ( `ENABLED` durumuna geçilmesinden bu yana geçen blok sayısı) olan aktif, etkinleştirilmiş (enabled) Masternode'lar:

$$W_{\text{PoM}} = C \times \left(1 + \alpha \cdot \min\left(\frac{t_{\text{active}}}{T_{\text{MAX}}}, 1.0\right)\right)$$

* $\alpha$ masternode ömür çarpanıdır (varsayılan: `1.0`, 2 kata kadar ağırlık sağlar).
* $T_{\text{MAX}}$ maksimum ömür olgunluğudur (Mainnet'te 100.000 blok, Testnet/Regtest'te 10.000 blok).
* Bir Masternode `ENABLED` durumundan çıkarsa (yeniden başlatma, ping zaman aşımı veya yapılandırma değişikliği nedeniyle), $t_{\text{active}}$ hemen 0'a sıfırlanır.

---

## 4. Uzun Ömürlü Masternode Kurulları (Long-Living Masternode Quorums - LLMQs)

Güvenli lider seçimi ve imza birleştirmeyi (signature aggregation) desteklemek için LLMQ'lar, BLS eşik imzaları yerine bireysel **secp256k1** imzaları kullanılarak uygulanır. Kurul üyeleri, gizli secp256k1 anahtarlarını (private keys) kullanarak blok özetini (block hash) imzalar ve bu imzalar bireysel olarak doğrulanır. Ağın, VRF döngüsel tohumları, PoBLS geçici bilet üretimi ve koordinatör imzaları için gerçek bir **BLS12-381** kütüphanesini (`blst`) yerel olarak entegre etmeye devam ettiğini unutmayın.

### Kurul Seçimi ve Boyutu
* **Kurul Boyutu (Quorum Size)**: Tam olarak **5 üye**.
* **DKG Aralığı**: DKG oturumları Mainnet'te her **100 blokta** bir ve Testnet/Regtest'te her **10 blokta** bir çalışır.
* **Aktif Masternode Filtreleme**: Herhangi bir ağda (Mainnet, Testnet veya Regtest) kodlanmış (hardcoded) anahtar kimliği filtrelemesi veya yerel cüzdan kısıtlaması uygulanmaz. Seçim, ağdaki tüm aktif ve etkinleştirilmiş Masternode'lar arasından dinamik olarak yapılır.
* **Belirleyici Geri Çekilme (Deterministic Fallback)**: 5'ten az aktif masternode mevcutsa, DKG oturum yöneticisi kayıtlı madenci havuzundan (`GetAdamMinerPool(nHeight - 1)`) üyeler seçmeye geri döner. Regtest'te kayıtlı madenci havuzu, yerel testleri kolaylaştırmak amacıyla **15 deterministik anahtarla** (0'dan 14'e kadar olan indekslerden türetilir) önceden doldurulur. Diğer ağlarda ise bu havuz, kayıtlı madencilerden (Coin-Lock veya PoW-Lock yoluyla) ve bootstrap madencilerinden oluşur.

### İmza ve Eşik Doğrulaması
* Kurul üyeleri, gizli secp256k1 anahtarlarını (private keys) kullanarak blok özetini (block hash) imzalar.
* Sürüm `>= 12` olduğunda (`SPORK_21_ADAM_STANDARD_MODE` etkinleştirildiğinde ve blok yüksekliği $\ge$ `nPoMBLHeight` olduğunda, yani Standart Mod aktifken) kurul imzası `vQuorumSig` blok başlıklarına doldurulur. Model D (`UPGRADE_MODELD`), daha sonra etkinleşen ayrı bir yükseltme yüksekliğidir (Mainnet'te 2200, Testnet'te 500 ve Regtest'te 200).
* **Kurul Doğrulama Eşiği (Quorum Validation Threshold)**:
  - **Mainnet**: İmza doğrulama eşiği kurul boyutunun **%75**'idir (`quorum.members.size() * 3 / 4`) ve en az 2 imza olmalıdır (eğer kurul boyutu daha küçükse gerçek kurul boyutuna sınırlandırılır). 5 üyeli standart bir kurul için doğrulama eşiği 3 imzadır. Blok üretimi sırasında (`miner.cpp`), basit çoğunluk + 1 eşiği (`quorum.members.size() / 2 + 1`) kullanılır.
  - **Testnet**: Model D aktif olduğunda (yükseklik $\ge 500$), küçük kurulumlarda canlılığı sağlamak için eşik tam olarak **2 imzadır**. Model D etkinleştirilmeden önce (400 ile 499 yükseklikleri arasında) eşik **0 imzadır** (doğrulama atlanır - bypassed).
  - **Regtest**: LLMQ yüksekliği 300'de (`UPGRADE_POMBL`) etkinleştiğinden ve Model D 200'de (`UPGRADE_MODELD`) etkinleştiğinden, kurul çalışmaya başladığında Model D zaten aktiftir. Bu nedenle, Regtest üzerindeki doğrulama eşiği her zaman tam olarak **2 imzadır**.

> [!NOTE]
> **Eşik Değerleri Arasındaki Fark:**
> **ADAM İşbirlikçi Madencilik Eşiği** (madenci bulmaca çözümleri `vAdamSolutions` için uygulanan 11'de 7 kuralı) ile **LLMQ İmza Doğrulama Eşiği** (blok imzası `vQuorumSig` için Mainnet'te uygulanan 5'te 3 kuralı) karıştırılmamalıdır:
> * **ADAM Eşiği**: 11 seçilmiş madenciden en az 7'sinin (`nAdamThreshold = 7` Mainnet/Regtest üzerinde, `3` Testnet üzerinde) PoW bulmaca çözümünü (`vAdamSolutions`) çözmüş ve göndermiş olmasını şart koşar. Bu kural ADAM işbirlikçi bulmaca doğrulama katmanına aittir.
> * **LLMQ Eşiği**: Blok hash'inin geçerli sayılabilmesi için 5 LLMQ üyesinden en az 3'ünün secp256k1 gizli anahtarlarıyla bloğu imzalamış olmasını (`vQuorumSig`) şart koşar. Bu kural merkeziyetsiz blok doğrulama katmanına aittir.

---

## 5. Konsensüs Dayatmaları ve Doğrulama Kuralları

Bir blok alındığında, yükseklik $\ge \text{nPoMBLHeight}$ olduğunda `src/main.cpp` içerisindeki `CheckBlock()` fonksiyonundaki doğrulama kuralları aşağıdaki kontrolleri zorunlu kılar:

1. **Sürüm Zorunluluğu (Version Enforcement)**: Blok sürümü en az `12` olmalıdır. Sürümü 12'den küçük olan blok başlıkları `bad-version` kodu ile kesinlikle reddedilir.
2. **Yakma Çıktısı Bütünlüğü (Burn Output Integrity)**: Kayıtlı bir yakma adresinden harcama yapan tüm işlem girdileri, mempool katmanında (`AcceptToMemoryPool`) ve blok bağlantı katmanında (`ConnectBlock`) kesinlikle reddedilir.
3. **Kilit Süresi Betiği Zorunluluğu (Locktime Script Enforcement)**: PoL için kullanılan kilitli işlem çıktıları kilitli betiklere kesinlikle uymalıdır ve belirtilen kilit yüksekliği/zaman damgası geçene kadar harcanamaz.
4. **Kurul Oyu Doğrulaması (Quorum Vote Validation)**: Blok başlığına dahil edilen kurul imzası (`vQuorumSig`), aktif LLMQ açık anahtarına (public key) karşı doğrulanır.

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
* Bir yakma adresinden harcama yapma girişimlerinin engellendiğini ve `bad-txns-invalid-outputs` ile reddedildiğini iddia eder (asserts).
