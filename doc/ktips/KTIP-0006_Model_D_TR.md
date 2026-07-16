```
KTIP: 0006
Title: Model D Ağ Güncellemesi (LLMQ topolojileri, DKG oturum zamanlaması ve doğrulama eşikleri)
Author: KristaTech Core Developers
Status: Active
Type: Standard Track (Consensus)
Created: 2026-06-16
```

## Özet
Bu teklif, KRISTA ağının ilk bootstrap (başlangıç) aşamasından olgun, uzun vadeli konsensüs ve ekonomik tasarımına geçişini belirleyen **Model D Ağ Güncellemesini** tanımlar. Model D güncellemesi; Dağıtılmış Anahtar Üretimi (DKG) oturumlarını kullanan Uzun Ömürlü Masternode Kurullarını (LLMQ) aktif hale getirir, hibrit PoBLS kurul imzası doğrulamasını zorunlu kılar, eski/terk edilmiş kayıtları önlemek için aktif madenci kayıt süresini kısaltır ve nihai ekonomik blok ödülü dağılımını devreye sokar.

## Motivasyon
Bootstrap aşamasında (Mainnet'te 2.200. bloktan önce), KRISTA ağının henüz kurulmamış veya olgunlaşmamış kurullar nedeniyle kilitlenmesini (deadlock) önlemek için masternode havuzunun birikmesi ve doğrulayıcı teminatlarının güvence altına alınması gerekiyordu.

Model D, bu durumu aşamalı bir olgunlaşma modeliyle çözer:
1. **Bootstrap Dönemi**: Yatırımcıların 4.200 KRISTA teminatı biriktirmelerini ve masternode sunucularını kurmalarını teşvik etmek amacıyla masternode ödemeleri %0 olarak ayarlanır. Bu aşamada, blok üretiminin kesintisiz devam edebilmesi için kurul imzası doğrulama eşikleri bypass edilir ($T = 0$).
2. **Olgunlaşma Dönemi (Model D)**: Yeterli aktif masternode sayısına ulaşıldığında, Model D bir sert çatallanma (hard fork) ile aktifleşir. Bu sayede gelişmiş LLMQ topolojileri devreye alınır, katı imza doğrulama eşikleri zorunlu kılınır ve nihai sürdürülebilir blok ödülü dağılımı başlar.

## Spesifikasyon

### 1. Güncelleme Aktivasyon Zamanlaması
Model D güncellemesi (`Consensus::UPGRADE_MODELD`), ağ türüne bağlı olarak farklı blok yüksekliklerinde aktifleşir:

| Ağ | Aktivasyon Yüksekliği | Durum |
| --- | --- | --- |
| **Mainnet** | Blok 2.200 | Aktif |
| **Testnet** | Blok 500 | Aktif |
| **Regtest** | Blok 200 | Aktif |

---

### 2. Uzun Ömürlü Masternode Kurulları (LLMQ) ve DKG Oturumları
Model D altında ağ, aktif ve kayıtlı masternode'ları simüle edilmiş bir Dağıtılmış Anahtar Üretimi (DKG) oturumu kullanarak Uzun Ömürlü Masternode Kurulları (LLMQ) şeklinde organize eder.

* **Kurul Boyutu ($N$)**: Donanım ve kaynak gereksinimlerini minimumda tutmak amacıyla kurul boyutu tam olarak **5 üye** olarak sabitlenmiştir.
* **DKG Rotasyon Aralığı**:
  * **Mainnet**: Her **100 blokta** bir yeni bir DKG oturumu çalıştırılır.
  
    $$H_{\text{DKG}} = \lfloor H / 100 \rfloor \times 100$$
  
  * **Testnet / Regtest**: Her **10 blokta** bir yeni bir DKG oturumu çalıştırılır.
  
    $$H_{\text{DKG}} = \lfloor H / 10 \rfloor \times 10$$
  
  Burada $H$ mevcut blok yüksekliğini, $H_{\text{DKG}}$ ise mevcut aktif kurulun seçildiği blok yüksekliğini temsil eder.

* **Kurul Seçimi**: Kurul üyeleri, bir önceki bloğun dönen entropi seed (tohum) değeri ve masternode teminat outpoint verisine dayalı bir puanlama sistemi kullanılarak tüm aktif masternode'ların deterministik olarak sıralanmasıyla seçilir.

---

### 3. Kurul İmzası Doğrulama Eşikleri
Sürüm 12 ve üzeri bloklar, blok başlığı hash'ini doğrulayan bir kurul imzası yükü `vQuorumSig` içerir. Kurul imzası doğrulaması için gereken eşik değer ($T$) şu şekilde hesaplanır:

* **Mainnet**:
  Eşik değer, kurul boyutunun %75'i (aşağı yuvarlanmış) olarak ayarlanır ve minimum değeri 2'dir:
  
  $$T_{\text{Mainnet}} = \max\left(2, \lfloor N \times 3 / 4 \rfloor\right) = 3$$
  
  Bu doğrultuda, blok hash'inin kurul üyelerinden en az **5'te 3'ü** tarafından imzalanması gerekir.

* **Testnet / Regtest**:
  * Model D aktivasyonundan önce:
    
    $$T_{\text{Testnet}} = 0$$
    
    Bootstrap sürecini kolaylaştırmak için kurul imzası doğrulaması atlanır (bypass edilir).
  * Model D aktif olduktan sonra:
    
    $$T_{\text{Testnet}} = 2$$
    
    Küçük geliştirme ve test ortamlarında ağ canlılığını (liveness) korumak için en az **5'te 2** kurul üyesinin imzası zorunlu kılınır.

---

### 4. Ekonomik Model ve Blok Ödülü Dağılımları
Model D, blok ödülünün nihai ve sürdürülebilir ekonomik paylaşımını zorunlu tutar. Dağıtım yapısı blok türüne göre değişiklik gösterir:

#### A. Proof-of-Work (PoW) Blokları
PoW (ADAM kooperatif madenciliği) yoluyla üretilen bloklarda, blok ödülü $R$ şu şekilde paylaştırılır:
* **Pasif Masternode Ödülü (%50)**: Ödeme kuyruğunun en üstündeki masternode'a dağıtılır.
* **Aktif LLMQ Ödülü (%10)**: Mevcut LLMQ kurulunda blok doğrulamasına katılan 5 aktif üyeye eşit şekilde dağıtılır.
* **Kooperatif Katılımcılar (%25)**: ADAM modeli kapsamında blok için geçerli kısmi PoW çözümleri gönderen madencilere dağıtılır.
* **Blok Çözücü / Kazanan (%15)**: Kazanan blok çözümünü bulan madenciye doğrudan ödenir.

#### B. Proof-of-Stake (PoS) Blokları
PoS (standart staking) yoluyla üretilen bloklarda, blok ödülü $R$ şu şekilde paylaştırılır:
* **Pasif Masternode Ödülü (%50)**: Ödeme kuyruğunun en üstündeki masternode'a dağıtılır.
* **Aktif LLMQ Ödülü (%10)**: Mevcut LLMQ kurulundaki 5 aktif üyeye eşit şekilde dağıtılır.
* **Staking Cüzdanı / Kazanan (%40)**: Coinstake işlemini gerçekleştiren staker'a doğrudan ödenir.

---

### 5. Madenci Kayıt Süresinin (`nRegPeriod`) Düşürülmesi
Eski konsensüs kurallarında, ağın başlangıç aşamasında işlem yükünü azaltmak amacıyla kooperatif madencilerin kayıt geçerlilik süresi uzun tutulmuştu:

$$\text{nRegPeriod}_{\text{Legacy}} = 2880 \text{ blok}$$

Model D, aktif madenci listesinde eskiyen veya terk edilmiş madencilerin birikmesini önlemek, blok üretimindeki olası gecikmeleri veya kesintileri engellemek amacıyla bu kayıt geçerlilik süresini düşürür:

$$\text{nRegPeriod}_{\text{Model D}} = 100 \text{ blok}$$

Madenciler, blokzinciri üzerinde düzenli olarak madenci kayıt işlemi (miner registration transaction) göndererek kayıtlarını yenilemelidir.

## Geriye Dönük Uyumluluk
* Model D kuralları, blok yüksekliği `UPGRADE_MODELD` değerine ulaştığında dinamik olarak devreye girer.
* Aktivasyon yüksekliğinin altındaki bloklarda Sürüm 12 blok yapıları ve kurul imzası kontrolleri yok sayılır, böylece orijinal bootstrap davranışı korunur.

## Referans Uygulama
* Güncelleme aktivasyon bayrakları ve yükseklikleri: `src/chainparams.cpp`.
* Kurul imzası eşik mantığı: `src/llmq.cpp` içindeki `Verify` fonksiyonu.
* DKG oturum planlaması: `src/llmq.cpp` içindeki `GetActiveQuorum` fonksiyonu.
* Ödül dağılım mantığı: `src/masternode.cpp` içindeki `GetMasternodePayment` fonksiyonu ve `src/miner.cpp` içindeki madenci/staker ödül fonksiyonları.
* Madenci kayıt geçerliliği kontrolü: `src/adam.cpp` içindeki `GetAdamMinerPool` fonksiyonu.
