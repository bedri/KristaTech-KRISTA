```
KTIP: 0006
Başlık: Model D Ağ Güncellemesi (LLMQ topolojileri, DKG oturum zamanlaması ve doğrulama eşikleri)
Yazar: KristaTech Core Developers
Durum: Active
Tür: Standard Track (Consensus)
Oluşturulma Tarihi: 2026-06-16
```

## Özet
Bu teklif, KRISTA ağının ilk bootstrap (başlangıç) aşamasından olgun, uzun vadeli konsensüs ve ekonomik tasarımına geçişini belirleyen **Model D Ağ Güncellemesini** tanımlar. Model D güncellemesi; Dağıtılmış Anahtar Üretimi (DKG) oturumlarını kullanan Uzun Ömürlü Masternode Kurullarını (LLMQ) aktif hale getirir, hibrit PoBLS kurul imzası doğrulamasını zorunlu kılar, eski/terk edilmiş kayıtları önlemek için aktif madenci kayıt süresini kısaltır ve nihai ekonomik blok ödülü dağılımını devreye sokar.

## Motivasyon
Bootstrap aşamasında (Mainnet'te 5.000. bloktan önce), KRISTA ağının henüz kurulmamış veya olgunlaşmamış kurullar nedeniyle kilitlenmesini (deadlock) önlemek için masternode havuzunun birikmesi ve doğrulayıcı teminatlarının güvence altına alınması gerekiyordu.

Model D, bu durumu aşamalı bir olgunlaşma modeliyle çözer:
1. **Bootstrap Dönemi**: Yatırımcıların ve madencilerin `4.200 KRISTA` teminatı biriktirmelerini ve masternode sunucularını kurmalarını teşvik etmek amacıyla Model D öncesinde Pasif Masternode (`%5`) ve LLMQ (`%10`) ödemeleri `%0` olarak ayarlanır (Klasik PoW blokları `2..199` arasında `%100` Madenci; Blok `200` itibarıyla `IsAdamValidatorSplitActive` + `UPGRADE_FAIRNESS` ile **`%25` Blok Üreticisi [PoS'ta `%20` Staker + `%5` İmzacı Koordinatör] + `%75` Eşit Aktif ADAM Doğrulayıcıları [11 koltuk]** [`~%6,818`/koltuk] ve çözülmemiş koltuklar için `%50/%50` Yakım/Hazine paylaşımı). Bu aşamada, blok üretiminin kesintisiz devam edebilmesi için kurul imzası doğrulama eşikleri bypass edilir ($T = 0$).
2. **Olgunlaşma Dönemi (Dengeli Model D)**: Yeterli aktif masternode sayısına ulaşıldığında Model D gelişmiş LLMQ topolojileriyle katı imza doğrulama eşiklerini zorunlu kılar, ADAM komitesini **`15` koltuğa (`8 MN + 2 Coin-Lock + 3 PoW-Lock + 2 Ücretsiz 0 KRISTA`)** ve **`%55` Aktif MN Koordinatör VRF tabanına** günceller, nihai dağıtımı da **`%5` Pasif MN + `%10` 1-Blok Gecikmeli İmzacı-Odaklı LLMQ + `%25` Üretici (`PoS %22 Staker + %3 Koordinatör`) + `%60` Eşit Aktif Doğrulayıcılar (`%60 / 15 = %4,00`/koltuk)** şeklinde uygular.

## Spesifikasyon

### 1. Güncelleme Aktivasyon Zamanlaması
Model D güncellemesi (`Consensus::UPGRADE_MODELD`), ağ türüne bağlı olarak farklı blok yüksekliklerinde aktifleşir:

| Ağ | Aktivasyon Yüksekliği | Durum |
| --- | --- | --- |
| **Mainnet** | Blok 5.000 | Aktif |
| **Testnet** | Blok 500 | Aktif |
| **Regtest** | Blok 200 | Aktif |

---

### 2. Uzun Ömürlü Masternode Kurulları (LLMQ) ve DKG Oturumları
Model D altında ağ, aktif ve kayıtlı masternode'ları simüle edilmiş bir Dağıtılmış Anahtar Üretimi (DKG) oturumu kullanarak Uzun Ömürlü Masternode Kurulları (LLMQ) şeklinde organize eder.

* **Kurul Boyutu ($N$)**: Dinamik BFT ölçeklendirme (`UPGRADE_HARDENING_V1` / `src/llmq.cpp` içindeki `GetTargetQuorumSize`), doğrulanmış aktif zincir üstü Masternode sayısına göre ölçeklenir (`<25` $\rightarrow$ `5`, `25–49` $\rightarrow$ `9`, `50–99` $\rightarrow$ `11`, `>=100` $\rightarrow$ `15`). Aktif Masternode sayısı $< N$ ise eksik koltuklar kota sınırlı yedeklerle (`en fazla 1 Coin-Lock + en fazla 3 Fonlu PoW-Lock`) tamamlanır.
* **DKG Rotasyon Aralığı (`GetActiveQuorum`)**:
  * **Mainnet**: 1.400. bloktan önce aktif kurul rotasyon yüksekliği her **100 blokta** bir (`UPGRADE_POMBL` alt sınırıyla), 1.400. blok ve sonrasında ise **her blokta** yeniden belirlenir:

    $$
    H_{\text{DKG}} = \begin{cases}
    \max\left(\lfloor H / 100 \rfloor \times 100,\; H_{\text{PoMBL}}\right) & H < 1400 \text{ ise} \\
    H & H \ge 1400 \text{ ise}
    \end{cases}
    $$

  * **Testnet / Regtest**: 1.000. bloktan (Testnet) / 800. bloktan (Regtest) önce aktif kurul rotasyon yüksekliği her **10 blokta** bir (`UPGRADE_POMBL` alt sınırıyla), sonrasında ise **her blokta** yeniden belirlenir:

    $$
    H_{\text{DKG}} = \begin{cases}
    \max\left(\lfloor H / 10 \rfloor \times 10,\; H_{\text{PoMBL}}\right) & H < H_{\text{rot}} \text{ ise} \\
    H & H \ge H_{\text{rot}} \text{ ise}
    \end{cases}
    $$
  
  Burada $H$ mevcut blok yüksekliğini (`nHeight`), $H_{\text{PoMBL}}$ `UPGRADE_POMBL` yüksekliğini (Mainnet `4800`, Testnet `400`, Regtest `300`), $H_{\text{rot}}$ Testnet'te $1000$ (Regtest'te $800$) değerini ve $H_{\text{DKG}}$ ise aktif kurulun seçildiği rotasyon yüksekliğini temsil eder.

* **Kurul Seçimi**: Kurul üyeleri, bir önceki bloğun dönen entropi seed (tohum) değeri ve masternode teminat outpoint verisine dayalı bir puanlama sistemi kullanılarak tüm aktif masternode'ların deterministik olarak sıralanmasıyla seçilir. Zincir üzerinde $N$'den az doğrulanmış Aktif Masternode bulunduğunda (`nVerifiedOnChainMNs < N`), mevcut tüm gerçek Aktif Masternode'lar kurula doğrudan dahil edilerek gerçek Masternode operatörlerinin koltukları korunur. $N$ üyeli kurulu tamamlamak için eksik kalan koltuklar, kota izolasyonlu yedek aday havuzlarından (en yüksek puanlı Coin-Lock ve ardından Fonlu PoW-Lock doğrulayıcıları) doldurulur; her bir aday strictly kota limitlidir (en fazla 1 Coin-Lock + en fazla 3 Fonlu PoW-Lock). Böylece Bizans izolasyonu korunur ve tek bir tarafın veya çoklu kilit kümesinin LLMQ imzalama gücünü ele geçirmesi engellenir.

---

### 3. Kurul İmzası Doğrulama Eşikleri
Sürüm 12 ve üzeri bloklar, blok başlığı hash'ini doğrulayan bir kurul imzası yükü `vQuorumSig` içerir. `CQuorumSignature::Verify()` (`src/llmq.cpp`) içindeki kurul imzası doğrulama eşiği ($T$) şu şekilde hesaplanır:

* **Model D Aktivasyonundan Önce (Mainnet / Testnet üzerinde `!NetworkUpgradeActive(nHeight, UPGRADE_MODELD)`)**:
  Aktif LLMQ kurulları zorunlu hale gelmeden önce bootstrap sürecini kolaylaştırmak için kurul imzası doğrulaması atlanır ($T = 0$).
* **Model D Aktif Olduktan Sonra (Tüm Ağlar: Mainnet, Testnet, Regtest)**:
  Eşik değer, katı ve dinamik bir BFT salt çoğunluğu ($\lfloor N / 2 \rfloor + 1$) olarak ayarlanır:
  
  $$T = \lfloor N / 2 \rfloor + 1 \in \{3, 5, 6, 8\}$$
  
  Bu doğrultuda, Model D aktif olduktan sonra tüm ağlarda blok hash'inin kurul büyüklüğüne göre dinamik BFT çoğunluğu (`5'te 3`, `9'da 5`, `11'de 6` veya `15'te 8`) tarafından imzalanması gerekir.

---

### 4. Ekonomik Model ve Blok Ödülü Dağılımları
Model D, blok ödülünün nihai ve sürdürülebilir ekonomik paylaşımını zorunlu tutar. Dağıtım yapısı blok türüne göre değişiklik gösterir:

#### A. Proof-of-Work (PoW) Blokları
PoW (ADAM kooperatif madenciliği) yoluyla üretilen bloklarda, dağıtılabilir blok ödülü $R$ (%7,0 Geliştirici Hazinesi düşüldükten sonra) Dengeli Model D + `UPGRADE_FAIRNESS` altında şu şekilde paylaştırılır:
* **Pasif Masternode Ödülü (%5)**: VRF tohumu modülo hesabı (`GetModelDPassiveMasternodePayee` içinde `seed.GetUint64(0) % vActiveMns.size()`) ile deterministik olarak seçilen aktif bir Masternode'a dağıtılır.
* **İmzacı-Odaklı LLMQ Ödülü (%10, 1-Blok Gecikmeli)**: $N \in \{5, 9, 11, 15\}$ kurul koltuğuna (`%10 / N`) bölünür ve **1-blok gecikmeyle yalnızca önceki bloğu imzalayan (`prevBlock.vQuorumSig`)** üyelere (`-5/+5` PoSe puanlamasıyla) ödenir; imzalamayan paylar `%50` Yakım + `%50` Hazine olarak ayrılır.
* **Kooperatif Katılımcılar (%60 — Çözülen Koltuk Başına Eşit `1/15` = `%4,00`)**: Zorunlu 33 baytlık sıkıştırılmış pubkey ile geçerli PoW çözümü sunan tüm **15 koltuğa (`8 MN + 2 Coin-Lock + 3 PoW-Lock + 2 Ücretsiz 0 KRISTA`)** kulvar ayrımı olmadan **eşit `%60 / 15 = %4,00`** dağıtılır. Çözülmeyen koltuklar **%0** alır ve sahipsiz paylar (`nUnclaimedPartSplit`) **%50 Otomatik Protokol Yakımına (`nExpectedBurn = nUnclaimedPartSplit / 2`)** ve **%50 Geliştirici Hazinesine (`nExpectedTreasury = nTreasurySplit + (nUnclaimedPartSplit - nExpectedBurn)`)** bölüştürülür.
* **Blok Çözücü / Kazanan (%25 + `nFees` — Tam Kurtarma Ödülü)**: Bloğu öneren kazanan PoBLS Koordinatörüne (Model D altında `%55` aktif MN tabanıyla orantılı `4.200 : 250 : 10` VRF ile seçilir) kesintisiz olarak ödenir. $t \ge 60\text{ sn}$ sonrasında sessiz bir birincil koltuk meşru aynı kulvar yedek doğrulayıcısı ile değiştirildiğinde, yedek doğrulayıcı eşit `%4,00` koltuk payını %60'lık havuzdan alır ve Koordinatörün %25'lik ödülünden ceza düşülmez.

#### B. Proof-of-Stake (PoS) Blokları
PoS (ikili staking + ADAM kooperatif doğrulaması) yoluyla üretilen bloklarda, dağıtılabilir blok ödülü $R$ Dengeli Model D + `UPGRADE_FAIRNESS` altında simetrik biçimde şu şekilde paylaştırılır:
* **Pasif Masternode Ödülü (%5)**: VRF tohumu modülo hesabıyla (`GetModelDPassiveMasternodePayee`) deterministik olarak seçilen aktif bir Masternode'a dağıtılır.
* **İmzacı-Odaklı LLMQ Ödülü (%10, 1-Blok Gecikmeli)**: Önceki bloğun `prevBlock.vQuorumSig` imzacılarına (`%10 / N`; imzasız paylar `%50` Yakım + `%50` Hazine) ödenir.
* **Kooperatif Katılımcılar (%60 — Eşit `1/15` = `%4,00` Koltuk Payı)**: Geçerli PoW bulmacası çözen tüm komite koltuklarına eşit (`%60 / 15 = %4,00`) ödenir; sahipsiz paylar **%50 Otomatik Protokol Yakımına** ve **%50 Geliştirici Hazinesine** yönlendirilir.
* **PoS Staker (`%22 + nFees`) ve İmzacı Koordinatör (`%3`)**: %25 üretici payı Staker (`%22` + **PoS işlem ücretlerinin `nFees` %100'ü**, Model D öncesi `%20 + nFees`) ve seçilen imzacı Koordinatör (`%3`, Model D öncesi `%5`) arasında simetrik bölünür.

---

### 5. Madenci Kayıt Süresinin (`nRegPeriod`) Düşürülmesi
Eski konsensüs kurallarında ve Mainnet üzerinde, işlem yükünü azaltmak amacıyla kooperatif madencilerin kayıt geçerlilik süresi ~24 saat olarak tutulmuştur:

$$\text{nRegPeriod}_{\text{Mainnet / Legacy}} = 2880 \text{ blok}$$

Testnet ve Regtest üzerinde, hızlı test süreçlerinde aktif madenci listesinde eskiyen veya terk edilmiş madencilerin birikmesini önlemek ve blok üretimindeki olası gecikmeleri engellemek amacıyla Model D bu kayıt geçerlilik süresini düşürür:

$$\text{nRegPeriod}_{\text{Model D (Testnet / Regtest)}} = 100 \text{ blok}$$

Madenciler, blokzinciri üzerinde düzenli olarak madenci kayıt işlemi (miner registration transaction) göndererek kayıtlarını yenilemelidir.

## Geriye Dönük Uyumluluk
* Model D kuralları, blok yüksekliği `UPGRADE_MODELD` değerine ulaştığında dinamik olarak devreye girer (Sürüm 12 blok başlığı serileştirmesi ise `UPGRADE_POMBL` yüksekliğinde — Mainnet `4800`, Testnet `400`, Regtest `300` — etkinleşir).
* `UPGRADE_MODELD` yüksekliğinin altındaki bloklarda dinamik BFT (`5'te 3`..`15'te 8`) LLMQ `vQuorumSig` doğrulaması ve Model D ödül dağılımları uygulanmaz, böylece bootstrap davranışı korunur.

## Referans Uygulama
* Güncelleme aktivasyon bayrakları ve yükseklikleri: `src/chainparams.cpp`.
* Kurul imzası eşik mantığı: `src/llmq.cpp` içindeki `Verify` fonksiyonu.
* DKG oturum planlaması: `src/llmq.cpp` içindeki `GetActiveQuorum` fonksiyonu.
* Ödül dağılım mantığı: `src/masternode-payments.cpp` içindeki `FillBlockPayee`, `IsBlockPayeeValid` ve `ComputeModelDValidatorSeatPayments`, `src/masternode.cpp` içindeki `GetMasternodePayment` ve `src/miner.cpp`.
* Madenci kayıt geçerliliği kontrolü: `src/adam.cpp` içindeki `GetAdamMinerPool` fonksiyonu.
