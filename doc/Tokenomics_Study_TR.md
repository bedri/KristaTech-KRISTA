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
| **KRISTATECH (KRISTA)** | PoW/PoS Hibrit (ADAM/Model D) | 210.000.000 KRISTA | Her ~90 günde bir %1.9 azalma (Decay) | Model D: %35 Pasif MN / %10 LLMQ / %15 Üretici / %40 Onaylayıcı (%20 7 MN + %20 4 Madenci) |

---

## 3. Önerilen KRISTA Tokenomics Modeli

KRISTA'nın hem altyapı sağlayıcılarını (Masternode) hem de ağ güvenliğini (Staking) maksimum düzeyde koruması ve **itibarlı (reputable) bir dijital varlık** olarak kalması için aşağıdaki model önerilmektedir:

### 3.1. Sınırlı Maksimum Arz (Hard Cap)
* **Önerilen Hard Cap:** **210.000.000 (210 Milyon) KRISTA**
* **Premine:** **0 KRISTA** (Premine Yok)
* **Madencilik/Staking Yoluyla Dağıtılacak Arz:** **210.000.000 KRISTA** (%100)

### 3.2. Üç Aylık Emisyon Azalması (Decay Modeli) ve Kademeli Bootstrap
Emisyon programı, **%1.9 üç aylık azalma** (decay) modeliyle (her ~90 günde bir / 259.200 blokta bir uygulanır) başlar ve başlangıç blok ödülü (10.000 blokluk kademeli ilk bootstrap aşamasından sonra: Blok 2–2.200 arası **40 KRISTA**, Blok 2.201–5.000 arası **30 KRISTA**, Blok 5.001–9.999 arası **20 KRISTA**) **15 KRISTA** olarak uygulanır. Bu model, hem erken dönem enflasyon balonunu (%72,8 oranında) engeller hem de Bitcoin'in sert 4 yıllık halving şokları yerine yumuşak ve öngörülebilir bir geçiş sunarak ödül süresini 50+ yılın üzerine yayar.

### 3.3. Masternode & Miner-Staker Ödül Paylaşımının Dengelenmesi
Blok ödülü dağılımı, Model D hibrit dağılım kurallarına göre hem PoW madencilerini hem de PoS stakerlarını teşvik edecek şekilde optimize edilmiştir:
* **Blok 2 - 199 (Klasik PoW Bootstrap):** %100 Madenci (ilk coin dağılımını ve başlangıç kazım gücünü sağlamak için).
* **Blok 200 - 2.199 (Model D Öncesi Kooperatif ADAM):** **%60 Blok Üreticisi (Koordinatör / Staker) + %40 Aktif ADAM Doğrulayıcıları** (1 Coin-Lock koltuğu için `%8,00`, 3 Açık PoW koltuğu için koltuk başı `%4,00`, 7 MN/taşma koltuğu için koltuk başı `~%2,857`; çözülmeyen koltuk payları %50 Otomatik Protokol Yakımı + %50 Geliştirici Hazinesi), böylece Masternode teminatları birikirken bulmaca çözen tüm komite doğrulayıcıları doğrudan ödüllendirilir.
* **Blok 2.200 - 5.000 (Model D Erken Dönem):** Model D ödül dağılımı aktiftir (%35 pasif MN, %10 aktif LLMQ, %40 katılımcılar [%20 7 MN koltuğu + %20 4 bağımsız madenci koltuğu], %15 blok kazananı). Model D aktif olduğunda (`IsModelDActive(nHeight)`), 5000. bloğa kadar 0 masternode ödemesi öngören eski bootstrap kuralı (`nHeight <= 5000`) geçersiz kılınır. Bu sayede %35 pasif masternode payı tamamen ödenir ve konsensüs/oylama ile zorunlu tutulur. (Not: Model D, Mainnet'te 2200, Testnet'te 500 ve Regtest'te 200 blok yüksekliğinde etkinleşir).
* **Blok 5.001+ (Olgunlaşma Dönemi):** **%45 Pasif+LLMQ Masternode / %55 Aktif Üretici+Onaylayıcı** paylaşımı Model D altında tamamen etkin ve zorunludur (%35 pasif MN, %10 aktif LLMQ, %40 katılımcılar [%20 7 MN + %20 4 bağımsız madenci], %15 blok kazananı — böylece toplam Masternode ekosistemi payı %80, bağımsız madenci payı ise %20 olur).

> [!NOTE]
> Ağ dual (hibrit) yapıda olduğundan, PoW madencileri ve PoS stakerları aktif blok üretimi ve doğrulama karşılığında net blok ödülünün %55'ini (%15 üretici/koordinatör + 7 Masternode onaylayıcı koltuğu için %20 + 1 Coin-Lock koltuğu için %8 ve 3 Açık PoW koltuğu için koltuk başına %4 olmak üzere %20; $t \ge 60\text{ sn}$ anında devreye giren aynı kulvar yedek doğrulayıcıları kendi kulvar paylarının %100'ünü, kulvarlar arası taşan doğrulayıcılar ise kendi kulvar oranlarını alır) kazanarak sürekli teşvik edilirler. İşlem ücretlerinin (`nFees`) %100'ü PoW bloklarında Blok Üreticisine (Koordinatör) ödenirken, PoS bloklarında yakılarak arzdan düşülür. Miner/Staker payı hiçbir zaman %0'a düşmez.

### 3.4. Sabit Masternode Teminatı
Ağ güvenliği, validator katılımı ve sunucu ROI oranlarını en dengeli seviyede tutmak amacıyla masternode teminatı 1. bloktan itibaren sabit **4.200 KRISTA** olarak kilitlenmiştir. Bu durum, LLMQ quorum yapısının sağlıklı çalışabilmesi için gereken 20+ aktif masternode'un, dolaşımdaki arzın teminat gereksinimlerini kolayca karşılayabilmesi sayesinde hızlıca kurulmasını sağlar.

---

## 4. Matematiksel Projeksiyon (25 Yıllık Simülasyon)

Bootstrap + %1.9 Üç Aylık (~90 Günlük) Azalma modeline göre blok ödülleri ve arz büyümesi:

* **Kademeli Bootstrap Aşaması (Blok 0 - 9.999):**
  - **Blok 0 – 1:** **0 KRISTA** (Adil Başlangıç — Mainnet üzerinde Sıfır Premine)
  - **Blok 2 – 2.200:** Blok başına **40 KRISTA** (`87.960 KRISTA`)
  - **Blok 2.201 – 5.000:** Blok başına **30 KRISTA** (`84.000 KRISTA`)
  - **Blok 5.001 – 9.999:** Blok başına **20 KRISTA** (`99.980 KRISTA`)
  - Toplam Bootstrap Üretimi: **271.940,00 KRISTA**
* **1. Yıl (0-3. Dönemler, Blok 10.000 - 1.046.799):** Blok başına **15 KRISTA** (her 259.200 blokta bir %1.9 azalma)
  - Ortalama Blok Ödülü: **14,58 KRISTA**
  - Yıllık Üretim: **15.114.355,61 KRISTA**
  - Yıl Sonu Kümülatif Arz: **15.386.295,61 KRISTA** (Hard Cap'in `%7,33`'ü)
* **2. Yıl (4-7. Dönemler, Blok 1.046.800 - 2.083.599):**
  - Ortalama Blok Ödülü: **13,50 KRISTA** (%1.9 üç aylık azalma adımları)
  - Yıllık Üretim: **13.997.989,56 KRISTA**
  - Yıl Sonu Kümülatif Arz: **29.384.285,17 KRISTA** (Hard Cap'in `%13,99`'u)
* **3. Yıl (8-11. Dönemler, Blok 2.083.600 - 3.120.399):**
  - Ortalama Blok Ödülü: **12,50 KRISTA**
  - Yıllık Üretim: **12.964.079,78 KRISTA**
  - Yıl Sonu Kümülatif Arz: **42.348.364,95 KRISTA** (Hard Cap'in `%20,17`'si)
* **5. Yıl (16-19. Dönemler, Blok 4.157.200 - 5.193.999):**
  - Ortalama Blok Ödülü: **10,72 KRISTA**
  - Yıllık Üretim: **11.119.717,50 KRISTA**
  - Yıl Sonu Kümülatif Arz: **65.474.618,37 KRISTA** (Hard Cap'in `%31,18`'i)
* **10. Yıl (36-39. Dönemler, Blok 9.341.200 - 10.377.999):**
  - Ortalama Blok Ödülü: **7,31 KRISTA**
  - Yıllık Üretim: **7.576.592,01 KRISTA**
  - Yıl Sonu Kümülatif Arz: **109.901.474,71 KRISTA** (Hard Cap'in `%52,33`'ü)
* **Sonsuz Vade:** Toplam brüt emisyon **204.903.519 KRISTA** seviyesinde asimptot yapar ve 210.000.000 KRISTA olan mutlak arz limitinin (hard cap) hemen altında (~5,10 Milyon KRISTA tampon ile) kalarak ödüllerin ani şoklar olmadan 50+ yıl boyunca sürmesini sağlar.

```
Arz Doyum Grafiği Projeksiyonu:
[0] (Genesis) -> [0,27M] (Blok 10k) -> [15,39M] (Yıl 1) -> [29,38M] (Yıl 2) -> [42,35M] (Yıl 3) -> [65,47M] (Yıl 5) -> [109,90M] (Yıl 10) -> [204,90M] (Asimptot)
```

---

## 5. Bu Model Neden KRISTA'yı Prestijli (Reputable) Kılar?

1. **Deflationary Yapı:** Toplam arzın 210 Milyon gibi prestijli bir sınırda kilitli olması, birim değerin uzun vadede artmasını sağlar. Fiili arzın ~204.90M seviyesinde durması onu daha da nadir kılar.
2. **Yüksek Kilitlenme Oranı (Lock-up Rate):** Masternode teminatlarının sabit 4.200 KRISTA olarak belirlenmesi, ağda çok yüksek sayıda aktif masternode (LLMQ quorum'lar için 20+ ve üstü) kurulmasını sağlar. Bu durum dolaşımdaki arzı kilitleyerek borsalardaki likiditeyi daraltır ve fiyatı destekler.
3. **Güvenli PoW/PoS:** Net ödüllerin %55'inin (%15 üretici + %40 katılımcılar; bunun içinde 1 Coin-Lock madencisi için %8 ve 3 Açık PoW madencisi için koltuk başına %4 olmak üzere ayrılmış %20'lik alt havuz) aktif blok üreticisi ve doğrulayıcı havuzuna gitmesi, hem donanım kazım gücünü hem de cüzdanların açık tutulmasını (staking) teşvik ederek ağın güvenliğini merkezsizleştirir.

---

## 6. Kod Seviyesinde Yapılan Değişiklikler

Kod üzerinde uygulanan nihai değişiklikler şunlardır:

1. **Maksimum Arz Limitinin Ayarlanması:**
   [src/chainparams.cpp](../src/chainparams.cpp) içinde `consensus.nMaxMoneyOut = 210000000 * COIN;` (210M) olarak set edilmesi.
2. **Blok Ödülü Mantığının Güncellenmesi:**
   [src/masternode.cpp](../src/masternode.cpp) içindeki `GetBlockValue` fonksiyonunu bootstrap ve %1.9 üç aylık (~90 günlük) decay hesaplayacak şekilde güncellemek:
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

       if (nHeight < 10000) {
           if (nHeight == 0) {
               return 15 * COIN;
           }
           if (nHeight <= 2200) {
               return 40 * COIN; // Bootstrap Phase 1 (Blocks 2..2,200)
           }
           if (nHeight <= 5000) {
               return 30 * COIN; // Bootstrap Phase 2 (Blocks 2,201..5,000)
           }
           return 20 * COIN;     // Bootstrap Phase 3 (Blocks 5,001..9,999)
       }

       // ~90 günde bir %1.9 azalma (Decay) - Her 259.200 blokta bir (Deterministik 128-bit Sabit Noktalı Tam Sayı Matematiği)
       int period = (nHeight - 10000) / 259200;
       static const unsigned __int128 FIXED_SCALE = 1000000000000000000ULL; // 10^18
       static const unsigned __int128 DECAY_NUM   = 981000000000000000ULL;  // 0.981 * 10^18
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
       unsigned __int128 rawSubsidy = (unsigned __int128)(15 * COIN) * factor;
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
        // Model D Mainnet'te 2.200, Testnet'te 500 ve Regtest'te 200. blokta aktifleşir.
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
  - 200. bloktan itibaren (`IsAdamValidatorSplitActive` ve Mainnet `blok >= 2.200` Model D kapsamında), sahipsiz kalan tüm doğrulayıcı veya LLMQ payları (`nUnclaimedPartSplit`) **%50 Otomatik Protokol Yakımına** (`KTburnCYcDjF1GsXa5k3tc3eZrVVbqefyWr`, kişisel PoB ağırlığı oluşturmadan `nMoneySupply` arzından kalıcı olarak düşülür) ve **%50 Geliştirici Hazinesine** bölüştürülür.

%7'lik Geliştirici Hazinesi kesintisi doğrudan blok değerinden (block value) düşülerek kalan tüm emisyon fazlarında doğrulayıcı ve masternode ödül havuzunu tekdüze biçimde **%93,0** olarak belirler (örneğin 2–2.200. bloklar arasında 40 KRISTA'dan 37,20 KRISTA'ya, 2.201–5.000. bloklar arasında 30 KRISTA'dan 27,90 KRISTA'ya, 5.001–9.999. bloklar arasında 20 KRISTA'dan 18,60 KRISTA'ya ve 10.000–269.199. bloklar arasında ise 15 KRISTA'dan 13,95 KRISTA'ya düşer).
