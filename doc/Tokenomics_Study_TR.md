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
| **KRISTATECH (KRISTATECH)** | PoS/Masternode | Sınırsız (Dinamik Deflasyon)| Blok başına sabit 5 KRISTATECH (MN/Staker dinamik) | Değişken (Genelde %60 MN / %40 Staker) |

---

## 3. Önerilen KRISTA Tokenomics Modeli

KRISTA'nın hem altyapı sağlayıcılarını (Masternode) hem de ağ güvenliğini (Staking) maksimum düzeyde koruması ve **itibarlı (reputable) bir dijital varlık** olarak kalması için aşağıdaki model önerilmektedir:

### 3.1. Sınırlı Maksimum Arz (Hard Cap)
* **Önerilen Hard Cap:** **210.000.000 (210 Milyon) KRISTA**
* **Premine:** **0 KRISTA** (Premine Yok)
* **Madencilik/Staking Yoluyla Dağıtılacak Arz:** **210.000.000 KRISTA** (%100)

### 3.2. Üç Aylık Emisyon Azalması (Decay Modeli)
Emisyon programı, **%1.9 üç aylık azalma** (decay) modeliyle (her ~90 günde bir / 259.200 blokta bir uygulanır) başlar ve başlangıç blok ödülü (10.000 blokluk ilk bootstrap aşamasından sonra) **15 KRISTA** (10.000 bloğa kadar **100 KRISTA** bootstrap ödülüyle) olarak uygulanır. Bu model, Bitcoin'in sert 4 yıllık halving şokları veya yıllık sert azalma adımları yerine daha yumuşak ve öngörülebilir bir geçiş sunarak ödül süresini 50+ yılın üzerine yayar.

### 3.3. Masternode & Miner-Staker Ödül Paylaşımının Dengelenmesi
Blok ödülü dağılımı, Model D hibrit dağılım kurallarına göre hem PoW madencilerini hem de PoS stakerlarını teşvik edecek şekilde optimize edilmiştir:
* **Blok 2 - 2.199 (Erken Aşama / PoW-PoS Hibrit):** %100 Miner/Staker (Masternodeler kurulurken ağ güvenliğini ve kazım gücünü sağlamak için masternode ödemeleri kapalıdır).
* **Blok 2.200 - 5.000 (Model D Erken Dönem):** Model D ödül dağılımı aktiftir (%50 pasif MN, %10 aktif LLMQ, %25 katılımcılar, %15 blok kazananı). Model D aktif olduğunda (`IsModelDActive(nHeight)`), 5000. bloğa kadar 0 masternode ödemesi öngören eski bootstrap kuralı (`nHeight <= 5000`) geçersiz kılınır. Bu sayede %50 pasif masternode payı tamamen ödenir ve konsensüs/oylama ile zorunlu tutulur. (Not: Model D, Mainnet'te 2200, Testnet'te 500 ve Regtest'te 200 blok yüksekliğinde etkinleşir).
* **Blok 5.001+ (Olgunlaşma Dönemi):** **%60 Masternode / %40 Miner-Staker** paylaşımı Model D altında tamamen etkin ve zorunludur (%50 pasif MN, %10 aktif LLMQ, %25 katılımcılar, %15 blok kazananı).

> [!NOTE]
> Ağ dual (hibrit) yapıda olduğundan, PoW madencileri (blok PoW ile üretildiğinde) veya PoS stakerları (blok PoS ile üretildiğinde) blok ödülünün %40'ını (%15 üretici + %25 katılımcılar) ve işlem ücretlerinin %100'ünü alarak sürekli teşvik edilirler. Miner/Staker payı hiçbir zaman %0'a düşmez.

### 3.4. Sabit Masternode Teminatı
Ağ güvenliği, validator katılımı ve sunucu ROI oranlarını en dengeli seviyede tutmak amacıyla masternode teminatı 1. bloktan itibaren sabit **2.100 KRISTA** olarak kilitlenmiştir. Bu durum, LLMQ quorum yapısının sağlıklı çalışabilmesi için gereken 20+ aktif masternode'un, dolaşımdaki arzın teminat gereksinimlerini kolayca karşılayabilmesi sayesinde hızlıca kurulmasını sağlar.

---

## 4. Matematiksel Projeksiyon (25 Yıllık Simülasyon)

Bootstrap + %1.9 Üç Aylık (~90 Günlük) Azalma modeline göre blok ödülleri ve arz büyümesi:

* **Bootstrap Aşaması (Blok 2 - 9.999):** Blok başına **100 KRISTA**
  - Toplam Bootstrap Üretimi: **999.800 KRISTA**
* **1. Yıl (0-3. Dönemler, Blok 10.000 - 1.046.799):** Blok başına **15 KRISTA** (her 259.200 blokta bir %1.9 azalma)
  - Ortalama Blok Ödülü: **14.44 KRISTA**
  - Yıllık Üretim: **18.715.182,85 KRISTA**
  - Yıl Sonu Dolaşımdaki Toplam Arz: **19.714.982,85 KRISTA**
* **2. Yıl (4-7. Dönemler, Blok 1.046.800 - 2.083.599):**
  - Ortalama Blok Ödülü: **13.37 KRISTA** (%1.9 üç aylık azalma adımları)
  - Yıllık Üretim: **13.732.027,76 KRISTA**
  - Yıl Sonu Dolaşımdaki Toplam Arz: **33.447.010,61 KRISTA**
* **3. Yıl (8-11. Dönemler, Blok 2.083.600 - 3.120.399):**
  - Ortalama Blok Ödülü: **12.36 KRISTA**
  - Yıllık Üretim: **12.028.806,91 KRISTA**
  - Yıl Sonu Dolaşımdaki Toplam Arz: **45.475.817,52 KRISTA**
* **5. Yıl (16-19. Dönemler, Blok 4.157.200 - 5.193.999):**
  - Ortalama Blok Ödülü: **10.53 KRISTA**
  - Yıllık Üretim: **10.217.998,66 KRISTA**
  - Yıl Sonu Dolaşımdaki Toplam Arz: **68.851.627,48 KRISTA**
* **10. Yıl (36-39. Dönemler, Blok 9.341.200 - 10.377.999):**
  - Ortalama Blok Ödülü: **7.16 KRISTA**
  - Yıllık Üretim: **7.164.158,41 KRISTA**
  - Yıl Sonu Dolaşımdaki Toplam Arz: **112.434.373,54 KRISTA**
* **Sonsuz Vade:** Toplam dolaşımdaki arz **205.631.379 KRISTA** seviyesinde asimptot yapar ve 210.000.000 KRISTA olan mutlak arz limitinin (hard cap) hemen altında kalarak ödüllerin ani şoklar olmadan sonsuza kadar sürmesini sağlar.

```
Arz Doyum Grafiği Projeksiyonu:
[0] (Genesis) -> [19.71M] (Yıl 1) -> [33.45M] (Yıl 2) -> [45.48M] (Yıl 3) -> [68.85M] (Yıl 5) -> [205.63M] (Asimptot)
```

---

## 5. Bu Model Neden KRISTA'yı Prestijli (Reputable) Kılar?

1. **Deflationary Yapı:** Toplam arzın 210 Milyon gibi prestijli bir sınırda kilitli olması, birim değerin uzun vadede artmasını sağlar. Fiili arzın ~205.63M seviyesinde durması onu daha da nadir kılar.
2. **Yüksek Kilitlenme Oranı (Lock-up Rate):** Masternode teminatlarının sabit 2.100 KRISTA olarak belirlenmesi, ağda çok yüksek sayıda aktif masternode (LLMQ quorum'lar için 20+ ve üstü) kurulmasını sağlar. Bu durum dolaşımdaki arzı kilitleyerek borsalardaki likiditeyi daraltır ve fiyatı destekler.
3. **Güvenli PoS:** Ödüllerin %40'ının (%15 üretici + %25 katılımcılar) staking/validator havuzuna gitmesi, küçük yatırımcıların da coinlerini kilitleyip cüzdanlarını açık tutmasını (staking) sağlayarak ağın güvenliğini merkezsizleştirir.

---

## 6. Kod Seviyesinde Yapılan Değişiklikler

Kod üzerinde uygulanan nihai değişiklikler şunlardır:

1. **Maksimum Arz Limitinin Ayarlanması:**
   [src/chainparams.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/chainparams.cpp) içinde `consensus.nMaxMoneyOut = 210000000 * COIN;` (210M) olarak set edilmesi.
2. **Blok Ödülü Mantığının Güncellenmesi:**
   [src/masternode.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/masternode.cpp) içindeki `GetBlockValue` fonksiyonunu bootstrap ve %1.9 üç aylık (~90 günlük) decay hesaplayacak şekilde güncellemek:
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
           return 100 * COIN; // Bootstrap
       }

       // ~90 günde bir %1.9 azalma (Decay) - Her 259.200 blokta bir
       int period = (nHeight - 10000) / 259200;
       double subsidy = 15.0 * pow(0.981, period);
       CAmount nSubsidy = (CAmount)(subsidy * COIN + 0.5);

       if (nMoneySupply + nSubsidy > maxMoneyOut) {
           return maxMoneyOut - nMoneySupply;
       }

       return nSubsidy;
   }
   ```
3. **Paylaşım Oranlarının Güncellenmesi:**
   [src/masternode.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/masternode.cpp) içinde `GetMasternodePayment` fonksiyonunu Model D ve diğer fazlara uygun şekilde güncellemek:
   ```cpp
    CAmount CMasternode::GetMasternodePayment(int nHeight)
    {
        // Model D activates at block 2,200 on Mainnet and Testnet (200 on Regtest).
        // When active, it overrides the legacy bootstrap rules below.
        if (IsModelDActive(nHeight)) {
            return CMasternode::GetBlockValue(nHeight) * 50 / 100; // %50 MN passive pay (Model D)
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

## 7. Geliştirici Hazinesi (Developer Treasury) ve Musluk (Faucet) Kesintileri

Ekosistem fonlamasını güvence altına almak ve yeni kullanıcıların ağa katılımını kolaylaştırmak amacıyla Mainnet üzerinde 2. bloktan itibaren bir blok ödülü bölüşüm mekanizması etkindir:

* **Geliştirici Hazinesi (%7)**:
  - **Kesinti**: Blok ödülünün %7'si otomatik olarak Geliştirici Fonu Adresine (`KTMbi3v9yXtJ4z3QuWG5urXVn5WwxHBEAfm`) aktarılır.
  - **Kapsam**: 2. blok yüksekliğinden itibaren tüm bloklarda uygulanır. 1. blok (premine) bu kesintiden muaftır.
* **Başlangıç Musluğu (Bootstrap Faucet - %0.7)**:
  - **Kesinti**: Blok ödülünün %0.7'si Musluk Adresine (`KTP9wyzSbStzXa8xNuZB4pXytzDZkSFsQKh`) aktarılır.
  - **Kapsam**: 2 ile 50.000. bloklar arasında etkindir. 1. blok (premine) bu kesintiden muaftır.

Bu kesintiler doğrudan blok değerinden (block value) düşülerek blok üreticisinin coinbase ödülünden düşülür (örneğin 2-9.999. bloklar arasında madenciye giden coinbase ödülü 50 KRISTA yerine 46.15 KRISTA olur).
