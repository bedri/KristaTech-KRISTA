# KristaTech (KRISTA) Tokenomics Study & Design Proposal

Bu çalışma, KristaTech (KRISTA) blokzincirinin finansal sürdürülebilirliğini, güvenlik teşviklerini ve piyasa itibarını (reputation) maksimize etmek amacıyla hazırlanmış kapsamlı bir **Tokenomics (Token Ekonomisi)** tasarım teklifidir. 

Mevcut durumda kullanılan geçici (dummy) değerler yerine, matematiksel olarak modellenmiş, enflasyonu sınırlayan ve masternode/staking dengesini kuran yeni bir emisyon programı önerilmektedir.

---

## 1. Mevcut Durum ve Enflasyon Riski Analizi

Orijinal (dummy) yapıda 30 saniyelik blok süresiyle yılda yaklaşık **1.051.200 blok** kazılmaktaydı.
* **İlk 5 ay (400.000 blok):** Dolaşımdaki arz hızla 5M (premine) + 50M (madencilik) = **55.000.000 KRISTA** seviyesine ulaşmaktaydı.
* **Sonrasında:** Yılda **105.120.000 KRISTA** sabit emisyonla sınırsız enflasyon üretilmekteydi.
* **Ödül Dağılımı:** Ödüllerin %95'i masternodelara, sadece %5'i madencilere/stakerlara gitmekteydi.

### Riskler:
1. **Aşırı Satış Baskısı (Sell Pressure):** Ödüllerin %95'inin masternodelara gitmesi, sürekli piyasaya coin satma eğilimi olan operatörler nedeniyle fiyatta ezici bir düşüş baskısı yaratır.
2. **Güvensiz Kazım/Stake Altyapısı:** Sadece %5 ödül payı alan madenciler (PoW) ve stakerlar (PoS), ağda kalmak için yeterli ekonomik teşvike sahip olamazlar. Bu durum blok üretim kararlılığını ve güvenliğini zayıflatır.
3. **İtibar Kaybı:** Sınırsız veya çok yüksek arz limitleri, coinin "değer saklama aracı" (store of value) kimliğini yok eder.

---

## 2. Global Standartlar ve Karşılaştırma (Bitcoin ve Diğerleri)

| Kripto Para | Konsensüs | Maksimum Arz (Hard Cap) | Emisyon Azalma Modeli | Masternode / Staking Payı |
| :--- | :---: | :---: | :---: | :---: |
| **Bitcoin (BTC)** | PoW | 21.000.000 BTC | Her 4 yılda bir %50 azalma (Halving) | Yok |
| **Dash (DASH)** | PoW/Masternode | ~18.900.000 DASH | Her yıl %7.14 azalma (Decay) | %47.5 MN / %47.5 Miner / %5 Hazine |
| **KRISTATECH (KRISTA)** | PoW/PoS Hibrit (ADAM/Model D) | 210.000.000 KRISTA | Her ~90 günde bir %2,15 azalma (Decay) | Model D: %35 Pasif MN / %10 LLMQ / %15 Üretici / %40 Onaylayıcı (%20 7 MN + %20 4 Madenci) |

---

## 3. Önerilen KRISTA Tokenomics Modeli

KRISTA'nın hem altyapı sağlayıcılarını (Masternode) hem de ağ güvenliğini (Staking) maksimum düzeyde koruması ve **itibarlı (reputable) bir dijital varlık** olarak kalması için aşağıdaki model önerilmektedir:

### 3.1. Sınırlı Maksimum Arz (Hard Cap)
* **Önerilen Hard Cap:** **210.000.000 (210 Milyon) KRISTA**
* **Premine:** **0 KRISTA** (Premine Yok)
* **Madencilik/Staking Yoluyla Dağıtılacak Arz:** **210.000.000 KRISTA** (%100)

### 3.2. Üç Aylık Emisyon Azalması (Blok 2'den İtibaren Decay Modeli)
Emisyon programı, Blok 2'den itibaren tekdüze **17 KRISTA** ile başlar (Blok 0–1 `0 KRISTA` üretir) ve her ~90 günde / `259.200` blokta bir (`period = (nHeight - 2) / 259200`) **%2,15 üç aylık azalma** (decay) uygular. Geçici bootstrap basamaklarının kaldırılması ani arz uçurumu şoklarını ortadan kaldırırken, `4.200 KRISTA` masternode oluşumu için yeterli erken likidite sağlar ve ödül ömrünü 45+ yıl boyunca sürdürür.

### 3.3. Masternode & Miner-Staker Ödül Paylaşımının Dengelenmesi
Blok ödülü dağılımı, Model D hibrit dağılım kurallarına göre hem PoW madencilerini hem de PoS stakerlarını teşvik edecek şekilde optimize edilmiştir:
* **Blok 2 - 199 (Klasik PoW Dönem 0 Başlangıcı):** %100 Madenci (%7 Hazine kesintisi sonrası net `15,81 KRISTA`) ile ilk coin dağılımı ve başlangıç kazım gücü sağlanır.
* **Blok 200 - 4.999 (Model D Öncesi Kooperatif ADAM):** **%60 Blok Üreticisi (Koordinatör / Staker) + %40 Aktif ADAM Doğrulayıcıları** (1 Coin-Lock koltuğu için `%8,00`, 3 Açık PoW koltuğu için koltuk başı `%4,00`, 7 MN/taşma koltuğu için koltuk başı `~%2,857`; çözülmeyen koltuk payları %50 Otomatik Protokol Yakımı + %50 Geliştirici Hazinesi); Sürüm 12 ve LLMQ/PoMBL ise Blok `4.800` (`UPGRADE_POMBL`) üzerinde etkinleşir.
* **Blok 5.000+ (Model D Olgunlaşma Dönemi):** **%45 Pasif+LLMQ Masternode / %55 Aktif Üretici+Onaylayıcı** paylaşımı Model D altında (`UPGRADE_MODELD = 5.000` Mainnet, `500` Testnet, `200` Regtest) tamamen etkin ve zorunludur: %35 pasif MN (Dönem 0'da `5,5335 KRISTA`), %10 aktif LLMQ (`1,5810 KRISTA`), %40 katılımcılar [%20 7 MN + %20 4 bağımsız madenci] ve %15 blok kazananı — böylece toplam Masternode ekosistemi payı %80, bağımsız madenci payı ise %20 olur.

> [!NOTE]
> Ağ dual (hibrit) yapıda olduğundan, PoW madencileri ve PoS stakerları aktif blok üretimi ve doğrulama karşılığında net blok ödülünün %55'ini (%15 üretici/koordinatör + 7 Masternode onaylayıcı koltuğu için %20 + 1 Coin-Lock koltuğu için %8 ve 3 Açık PoW koltuğu için koltuk başına %4 olmak üzere %20; $t \ge 60\text{ sn}$ anında devreye giren aynı kulvar yedek doğrulayıcıları kendi kulvar paylarının %100'ünü, boş Masternode koltuklarını dolduran taşma doğrulayıcıları ise Masternode temel koltuk oranını (`~%2,857`) alır) kazanarak sürekli teşvik edilirler. İşlem ücretlerinin (`nFees`) %100'ü PoW bloklarında Blok Üreticisine (Koordinatör) ödenirken, PoS bloklarında yakılarak arzdan düşülür. Miner/Staker payı hiçbir zaman %0'a düşmez.

### 3.4. Sabit Masternode Teminatı
Ağ güvenliği, validator katılımı ve sunucu ROI oranlarını en dengeli seviyede tutmak amacıyla masternode teminatı 1. bloktan itibaren sabit **4.200 KRISTA** olarak kilitlenmiştir. Bu durum, LLMQ quorum yapısının sağlıklı çalışabilmesi için gereken 20+ aktif masternode'un, dolaşımdaki arzın teminat gereksinimlerini kolayca karşılayabilmesi sayesinde hızlıca kurulmasını sağlar.

---

## 4. Matematiksel Projeksiyon (25 Yıllık Simülasyon)

**17 KRISTA + %2,15 Üç Aylık (~90 Günlük) Azalma** modeline (`period = (nHeight - 2) / 259200`) göre blok ödülleri ve arz büyümesi:

* **Başlangıç ve Erken Konsensüs Eşikleri (Blok 0 - 5.000):**
  - **Blok 0 – 1:** **0 KRISTA** (Adil Başlangıç — Mainnet üzerinde Sıfır Premine)
  - **Blok 2 – 4.800 (`UPGRADE_POMBL`):** Blok başına **17 KRISTA** (`81.583 KRISTA` brüt / `75.872,19 KRISTA` net -> maks. 18,06 MN)
  - **Blok 4.801 – 5.000 (`UPGRADE_MODELD`):** Blok başına **17 KRISTA** (`84.983 KRISTA` kümülatif brüt / `79.034,19 KRISTA` net -> maks. 18,82 MN)
* **1. Yıl (0-3. Dönemler, Blok 2 - 1.036.801 [4Ç] / ~365g Blok 1.051.199):** **17 KRISTA** (her 259.200 blokta bir %2,15 azalma)
  - Dönem Ödülleri (`0..3`): `17,00` -> `16,6345` -> `16,2769` -> `15,9269 KRISTA` (Ort. **16,46 KRISTA**)
  - Yıllık Üretim (`4Ç`): **17.065.278,04 KRISTA** (`365g`: **17.289.663,35 KRISTA**)
  - Yıl Sonu Kümülatif Arz (`4Ç`): **17.065.278,04 KRISTA** (Hard Cap'in `%8,13`'ü)
* **2. Yıl (4-7. Dönemler, Blok 1.036.802 - 2.073.601 [4Ç]):**
  - Ortalama Blok Ödülü: **15,09 KRISTA** (%2,15 üç aylık azalma adımları)
  - Yıllık Üretim: **15.644.319,92 KRISTA**
  - Yıl Sonu Kümülatif Arz: **32.709.597,96 KRISTA** (Hard Cap'in `%15,58`'i)
* **3. Yıl (8-11. Dönemler, Blok 2.073.602 - 3.110.401 [4Ç]):**
  - Ortalama Blok Ödülü: **13,83 KRISTA**
  - Yıllık Üretim: **14.341.679,35 KRISTA**
  - Yıl Sonu Kümülatif Arz: **47.051.277,31 KRISTA** (Hard Cap'in `%22,41`'i)
* **5. Yıl (16-19. Dönemler, Blok 4.147.202 - 5.184.001 [4Ç]):**
  - Ortalama Blok Ödülü: **11,63 KRISTA**
  - Yıllık Üretim: **12.052.763,88 KRISTA**
  - Yıl Sonu Kümülatif Arz: **72.251.545,70 KRISTA** (Hard Cap'in `%34,41`'i)
* **10. Yıl (36-39. Dönemler, Blok 9.331.202 - 10.368.001 [4Ç]):**
  - Ortalama Blok Ödülü: **7,53 KRISTA**
  - Yıllık Üretim: **7.803.748,21 KRISTA**
  - Yıl Sonu Kümülatif Arz: **119.031.925,39 KRISTA** (Hard Cap'in `%56,68`'i)
* **Sonsuz Vade:** Toplam brüt emisyon **204.948.837 KRISTA** (Hard Cap'in `%97,59`'u) seviyesinde asimptot yapar ve 210.000.000 KRISTA olan mutlak arz limitinin hemen altında (**5,05 Milyon KRISTA / %2,41 rezerv boşluğu** ile) kalarak ödüllerin halving şokları olmadan 45+ yıl boyunca sürmesini sağlar.

```
Arz Doyum Grafiği Projeksiyonu:
[0] (Genesis) -> [0,085M] (Blok 5k) -> [17,07M] (Yıl 1) -> [32,71M] (Yıl 2) -> [47,05M] (Yıl 3) -> [72,25M] (Yıl 5) -> [119,03M] (Yıl 10) -> [204,95M] (Asimptot)
```

---

## 5. Bu Model Neden KRISTA'yı Prestijli (Reputable) Kılar?

1. **Deflationary Yapı:** Toplam arzın 210 Milyon gibi prestijli bir sınırda kilitli olması, birim değerin uzun vadede artmasını sağlar. Fiili arzın ~204.95M seviyesinde durması onu daha da nadir kılar.
2. **Yüksek Kilitlenme Oranı (Lock-up Rate):** Masternode teminatlarının sabit 4.200 KRISTA olarak belirlenmesi, ağda çok yüksek sayıda aktif masternode (LLMQ quorum'lar için 20+ ve üstü) kurulmasını sağlar. Bu durum dolaşımdaki arzı kilitleyerek borsalardaki likiditeyi daraltır ve fiyatı destekler.
3. **Güvenli PoW/PoS:** Net ödüllerin %55'inin (%15 üretici + %40 katılımcılar; bunun içinde 1 Coin-Lock madencisi için %8 ve 3 Açık PoW madencisi için koltuk başına %4 olmak üzere ayrılmış %20'lik alt havuz) aktif blok üreticisi ve doğrulayıcı havuzuna gitmesi, hem donanım kazım gücünü hem de cüzdanların açık tutulmasını (staking) teşvik ederek ağın güvenliğini merkezsizleştirir.

---

## 6. Kod Seviyesinde Yapılan Değişiklikler

Kod üzerinde uygulanan nihai değişiklikler şunlardır:

1. **Maksimum Arz Limitinin Ayarlanması:**
   [src/chainparams.cpp](../src/chainparams.cpp) içinde `consensus.nMaxMoneyOut = 210000000 * COIN;` (210M) olarak set edilmesi.
2. **Blok Ödülü Mantığının Güncellenmesi:**
   [src/masternode.cpp](../src/masternode.cpp) içindeki `GetBlockValue` fonksiyonunu Blok 2 `17 KRISTA` başlangıcı ve %2,15 üç aylık (~90 günlük) decay hesaplayacak şekilde güncellemek:
   ```cpp
   CAmount CMasternode::GetBlockValue(int nHeight)
   {
       CAmount maxMoneyOut = Params().GetConsensus().nMaxMoneyOut;

       if (nMoneySupply >= maxMoneyOut) {
           return 0;
       }

       if (nHeight == 1) {
           return 0; // Set premine to 0
       }

       if (nHeight == 0) {
           return 17 * COIN;
       }

       if (nHeight < 2) {
           return 0;
       }

       // ~90 günde bir %2.15 azalma (Decay) - Blok 2'den itibaren her 259.200 blokta bir (Deterministik 128-bit Sabit Noktalı Tam Sayı Matematiği)
       int period = (nHeight - 2) / 259200;
       static const unsigned __int128 FIXED_SCALE = 1000000000000000000ULL; // 10^18
       static const unsigned __int128 DECAY_NUM   = 978500000000000000ULL; // 0.9785 * 10^18
       unsigned __int128 factor = FIXED_SCALE;
       unsigned __int128 base = DECAY_NUM;
       int exp = period;
       while (exp > 0) {
           if (exp & 1) {
               factor = (factor * base + (FIXED_SCALE / 2)) / FIXED_SCALE;
           }
           exp >>= 1;
           if (exp > 0) {
               base = (base * base + (FIXED_SCALE / 2)) / FIXED_SCALE;
           }
       }
       unsigned __int128 rawSubsidy = (unsigned __int128)(17 * COIN) * factor;
       CAmount nSubsidy = (CAmount)((rawSubsidy + (FIXED_SCALE / 2)) / FIXED_SCALE);

       if (nMoneySupply + nSubsidy > maxMoneyOut) {
           return maxMoneyOut - nMoneySupply;
       }

       return nSubsidy;
   }
   ```
3. **Paylaşım Oranlarının Güncellenmesi:**
   [src/masternode.cpp](../src/masternode.cpp) içinde `GetMasternodePayment` fonksiyonunu Model D ve diğer fazlara uygun şekilde güncellemek:
   ```cpp
    CAmount CMasternode::GetMasternodePayment(int nHeight)
    {
        // Model D Mainnet'te 5.000, Testnet'te 500 ve Regtest'te 200. blokta aktifleşir.
        // Aktif olduğunda aşağıdaki eski bootstrap kurallarını geçersiz kılar.
        if (IsModelDActive(nHeight)) {
            CAmount nBlockValActual = CMasternode::GetBlockValue(nHeight);
            CAmount nTreasurySplit = (nHeight > 1) ? (nBlockValActual * 7 / 100) : 0;
            CAmount nBlockVal = nBlockValActual - nTreasurySplit;
            return nBlockVal * 35 / 100; // %35 MN passive pay (Model D) after 7% Treasury split
        }

        // Legacy/Fallback Rules (prior to Model D activation):
        // 1. Early Bootstrap (blocks 2 to 5,000): Masternodes receive no payment to allow initial setup.
        if (nHeight <= 5000) {
            return 0;
        }

        // 2. Late Bootstrap (blocks 5,001 to 100,000): Masternodes receive 80% of block value.
        if (nHeight <= 100000) {
            return CMasternode::GetBlockValue(nHeight) * 80 / 100; // %80 MN, %20 Miner-Staker
        }

        // 3. Maturation Phase (blocks 100,001+): Masternodes receive 60% of block value.
        return CMasternode::GetBlockValue(nHeight) * 60 / 100; // %60 MN, %40 Miner-Staker
    }
   ```

---

## 7. Geliştirici Hazinesi (Developer Treasury) Kesintisi

Uzun vadeli ekosistem fonlamasını güvence altına almak amacıyla Mainnet üzerinde 2. bloktan itibaren protokol düzeyinde blok ödülü bölüşüm mekanizması etkindir:

* **Geliştirici Hazinesi (%7)**:
  - **Kesinti**: Blok ödülünün %7'si otomatik olarak Geliştirici Fonu Adresine (`KTeRC2Kj8hXcuzRohxPp2LtYFRJm2BceRoP`) aktarılır.
  - **Kapsam**: 2. blok yüksekliğinden itibaren tüm bloklarda uygulanır. 0. ve 1. bloklar (sıfır premine) bu kesintiden muaftır.
* **Sahipsiz Kooperatif / Model D Payları (%50 Otomatik Protokol Yakımı / %50 Geliştirici Hazinesi)**:
  - 200. bloktan itibaren (`IsAdamValidatorSplitActive` ve Mainnet `blok >= 5.000` Model D kapsamında), sahipsiz kalan tüm doğrulayıcı veya LLMQ payları (`nUnclaimedPartSplit`) **%50 Otomatik Protokol Yakımına** (`KTburnCYcDjF1GsXa5k3tc3eZrVVbqefyWr`, kişisel PoB ağırlığı oluşturmadan `nMoneySupply` arzından kalıcı olarak düşülür) ve **%50 Geliştirici Hazinesine** bölüştürülür.

%7'lik Geliştirici Hazinesi kesintisi doğrudan blok değerinden (block value) düşülerek kalan tüm emisyon fazlarında doğrulayıcı ve masternode ödül havuzunu tekdüze biçimde **%93,0** olarak belirler (örneğin Dönem 0'da [Blok 2–259.201] 17 KRISTA'dan `15,81 KRISTA`'ya, Dönem 1'de [Blok 259.202–518.401] `16,6345 KRISTA`'dan `15,470085 KRISTA`'ya düşer).

**KTDIP3 v2** (`nDip3V2Height = 1` Mainnet, Testnet & Regtest) kapsamında masternode blok ve LLMQ kurul ödemeleri `AppendDip3OrDirectPayment` ile deterministik `ProRegTx` `scriptPayout` adresine (ve isteğe bağlı `nOperatorReward` baz puan payı ile `scriptOperatorPayout` adresine) yönlendirilirken, LLMQ kurul büyüklüğü aktif havuzla birlikte dinamik olarak (`GetTargetQuorumSize`: `5 -> 9 -> 11 -> 15` üye) ölçeklenir.
