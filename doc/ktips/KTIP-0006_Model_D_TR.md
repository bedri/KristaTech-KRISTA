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
  * **Mainnet**: 1.400. bloktan önce her **100 blokta** bir, 1.400. blok ve sonrasında ise **her blokta (`nInterval = 1`)** yeni bir DKG oturumu çalıştırılır:
  
    $$H_{\text{DKG}} = \begin{cases} \lfloor H / 100 \rfloor \times 100 & H < 1400 \text{ ise} \\ H & H \ge 1400 \text{ ise} \end{cases}$$
  
  * **Testnet / Regtest**: 1.000. bloktan (Testnet) / 800. bloktan (Regtest) önce her **10 blokta** bir, sonrasında ise **her blokta (`nInterval = 1`)** yeni bir DKG oturumu çalıştırılır:
  
    $$H_{\text{DKG}} = \begin{cases} \lfloor H / 10 \rfloor \times 10 & H < H_{\text{rot}} \text{ ise} \\ H & H \ge H_{\text{rot}} \text{ ise} \end{cases}$$
  
  Burada $H$ bir önceki blok yüksekliğini (`pindexPrev->nHeight`), $H_{\text{rot}}$ Testnet'te $1000$ (Regtest'te $800$) değerini ve $H_{\text{DKG}}$ ise mevcut aktif kurulun seçildiği blok yüksekliğini temsil eder.

* **Kurul Seçimi**: Kurul üyeleri, bir önceki bloğun dönen entropi seed (tohum) değeri ve masternode teminat outpoint verisine dayalı bir puanlama sistemi kullanılarak tüm aktif masternode'ların deterministik olarak sıralanmasıyla seçilir.

---

### 3. Kurul İmzası Doğrulama Eşikleri
Sürüm 12 ve üzeri bloklar, blok başlığı hash'ini doğrulayan bir kurul imzası yükü `vQuorumSig` içerir. `CQuorumSignature::Verify()` (`src/llmq.cpp`) içindeki kurul imzası doğrulama eşiği ($T$) şu şekilde hesaplanır:

* **Model D Aktivasyonundan Önce (Mainnet / Testnet üzerinde `!NetworkUpgradeActive(nHeight, UPGRADE_MODELD)`)**:
  Aktif LLMQ kurulları zorunlu hale gelmeden önce bootstrap sürecini kolaylaştırmak için kurul imzası doğrulaması atlanır ($T = 0$).
* **Model D Aktif Olduktan Sonra (Tüm Ağlar: Mainnet, Testnet, Regtest)**:
  Eşik değer, katı bir BFT salt çoğunluğu ($\lfloor N / 2 \rfloor + 1$) olarak ayarlanır:
  
  $$T = \lfloor N / 2 \rfloor + 1 = 3$$
  
  Bu doğrultuda, Model D aktif olduktan sonra tüm ağlarda blok hash'inin kurul üyelerinden en az **5'te 3'ü** tarafından imzalanması gerekir.

---

### 4. Ekonomik Model ve Blok Ödülü Dağılımları
Model D, blok ödülünün nihai ve sürdürülebilir ekonomik paylaşımını zorunlu tutar. Dağıtım yapısı blok türüne göre değişiklik gösterir:

#### A. Proof-of-Work (PoW) Blokları
PoW (ADAM kooperatif madenciliği) yoluyla üretilen bloklarda, dağıtılabilir blok ödülü $R$ (%7,0 Geliştirici Hazinesi ve 2–50.000. bloklar arasında %0,7 Başlangıç Musluğu düşüldükten sonra) şu şekilde paylaştırılır:
* **Pasif Masternode Ödülü (%35)**: Deterministik ödeme kuyruğunun en üstündeki masternode'a dağıtılır.
* **Aktif LLMQ Ödülü (%10)**: Mevcut LLMQ kurulunda blok doğrulamasına katılan 5 aktif üyeye eşit şekilde dağıtılır.
* **Kooperatif Katılımcılar (%40 — 7 MN Koltuğu %20 + 4 Bağımsız Madenci Koltuğu %20)**: Geçerli PoW çözümü sunan (`!vAdamSolutions[i].empty()`) komite koltukları arasında 7 Masternode onaylayıcı koltuğuna toplam **%20** (koltuk başına `~%2,857`) ve 4 bağımsız madenci koltuğuna (`1 Coin-Lock + 3 Open PoW`) toplam **%20** (1 Coin-Lock koltuğu için **%8,00** ve 3 Açık PoW koltuğu için koltuk başına **%4,00**) olarak dağıtılır. Çözülmeyen/boş koltuklar **%0** alır ve sahipsiz kalan paylar (boş madenci koltuğuna Masternode taşması durumunda Coin-Lock koltuğu için `~%5,143` veya Açık PoW koltuğu için `~%1,143` fark dahil) **%50 Otomatik Protokol Yakımına (`nExpectedBurn = nUnclaimedPartSplit / 2`)** ve **%50 Geliştirici Hazinesine (`nExpectedTreasury = nTreasurySplit + (nUnclaimedPartSplit - nExpectedBurn)`)** bölüştürülür.
* **Blok Çözücü / Kazanan (%15 — Tam Kurtarma Ödülü)**: Bloğu öneren kazanan PoBLS Koordinatörüne kesintisiz olarak ödenir. $t \ge 60\text{ sn}$ sonrasında sessiz bir birincil koltuk meşru aynı kulvar yedek doğrulayıcısı ile değiştirildiğinde (`vAdamMiners[i] != vExpectedMiners[i]`), yedek doğrulayıcı ödülünü doğrudan %40'lık Kooperatif Katılımcılar Havuzundan (`nPartSplitTotal`) alır ve Koordinatörün %15'lik ödülünden herhangi bir ikame cezası düşülmez. Birincil doğrulayıcılar oportünistik ikamelere karşı şu üç katmanla korunur: (1) birincil Koordinatörün münhasırlık penceresinin $t = 45\text{ sn}$'de (yedek rotasyonu $t = 60\text{ sn}$'de açılmadan önce) sona ermesi, (2) deterministik aynı kulvar yedek sıra düzeni (`m = 11..16`), ve (3) yerel önbellekte geçerli çözümü bulunan birincil bir koltuğu değiştiren önerilere karşı LLMQ `qblockprop` ikame vetosu.

#### B. Proof-of-Stake (PoS) Blokları
PoS (ikili staking + ADAM kooperatif doğrulaması) yoluyla üretilen bloklarda, dağıtılabilir blok ödülü $R$ şu şekilde paylaştırılır:
* **Pasif Masternode Ödülü (%35)**: Ödeme kuyruğunun en üstündeki masternode'a dağıtılır.
* **Aktif LLMQ Ödülü (%10)**: Mevcut LLMQ kurulundaki 5 aktif üyeye eşit şekilde dağıtılır.
* **Kooperatif Katılımcılar (%40 — 7 MN Koltuğu %20 + 4 Bağımsız Madenci Koltuğu %20)**: Blok için kısmi PoW bulmacalarını çözen (`!vAdamSolutions[i].empty()`) staker dışındaki ADAM komite koltuklarına kulvar bazlı ödenir; çözülmeyen boş koltukların payları **%50 Otomatik Protokol Yakımına** (blok üreticisinin aktif PoB ağırlığına işlenerek `nMoneySupply` arzından düşülür) ve **%50 Geliştirici Hazinesine** yönlendirilir.
* **Staking Cüzdanı / Kazanan (%15)**: Coinstake işlemini gerçekleştiren staker'a doğrudan ödenir.

---

### 5. Madenci Kayıt Süresinin (`nRegPeriod`) Düşürülmesi
Eski konsensüs kurallarında ve Mainnet üzerinde, işlem yükünü azaltmak amacıyla kooperatif madencilerin kayıt geçerlilik süresi ~24 saat olarak tutulmuştur:

$$\text{nRegPeriod}_{\text{Mainnet / Legacy}} = 2880 \text{ blok}$$

Testnet ve Regtest üzerinde, hızlı test süreçlerinde aktif madenci listesinde eskiyen veya terk edilmiş madencilerin birikmesini önlemek ve blok üretimindeki olası gecikmeleri engellemek amacıyla Model D bu kayıt geçerlilik süresini düşürür:

$$\text{nRegPeriod}_{\text{Model D (Testnet / Regtest)}} = 100 \text{ blok}$$

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
