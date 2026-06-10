# KristaTech (KRISTA) Tokenomics Study & Design Proposal

Bu çalışma, KristaTech (KRISTA) blokzincirinin finansal sürdürülebilirliğini, güvenlik teşviklerini ve piyasa itibarını (reputation) maksimize etmek amacıyla hazırlanmış kapsamlı bir **Tokenomics (Token Ekonomisi)** tasarım teklifidir. 

Mevcut durumda kullanılan geçici (dummy) değerler yerine, matematiksel olarak modellenmiş, enflasyonu sınırlayan ve masternode/staking dengesini kuran yeni bir emisyon programı önerilmektedir.

---

## 1. Mevcut Durum ve Enflasyon Riski Analizi

Mevcut (dummy) yapıda 30 saniyelik blok süresiyle yılda yaklaşık **1.051.200 blok** kazılmaktadır.
* **İlk 5 ay (400.000 blok):** Dolaşımdaki arz hızla 5M (premine) + 50M (madencilik) = **55.000.000 KRISTA** seviyesine ulaşmaktadır.
* **Sonrasında:** Yılda **105.120.000 KRISTA** sabit emisyonla sınırsız enflasyon üretilmektedir.
* **Ödül Dağılımı:** Ödüllerin %95'i masternodelara, sadece %5'i madencilere/stakerlara gitmektedir.

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
| **PIVX (PIVX)** | PoS/Masternode | Sınırsız (Dinamik Deflasyon)| Blok başına sabit 5 PIVX (MN/Staker dinamik) | Değişken (Genelde %60 MN / %40 Staker) |

---

## 3. Önerilen KRISTA Tokenomics Modeli

KRISTA'nın hem altyapı sağlayıcılarını (Masternode) hem de ağ güvenliğini (Staking) maksimum düzeyde koruması ve **itibarlı (reputable) bir dijital varlık** olarak kalması için aşağıdaki model önerilmektedir:

### 3.1. Sınırlı Maksimum Arz (Hard Cap)
* **Önerilen Hard Cap:** **100.000.000 (100 Milyon) KRISTA**
* **Premine (Kurucu / Ekosistem / Fonlama):** **5.000.000 KRISTA** (%5)
* **Madencilik/Staking Yoluyla Dağıtılacak Arz:** **95.000.000 KRISTA** (%95)

### 3.2. Yıllık Emisyon Azalması (Decay Modeli)
Blok ödüllerinin her **1.051.200 blokta bir** (yaklaşık 1 yıl) **%15 oranında azalması** (decay) önerilir. Bu model, Bitcoin'in sert 4 yıllık halving şokları yerine daha yumuşak ve öngörülebilir bir geçiş sunar.

### 3.3. Masternode & Miner-Staker Ödül Paylaşımının Dengelenmesi
Blok ödülü dağılımı, hem PoW madencilerini hem de PoS stakerlarını teşvik edecek şekilde kademeli olarak optimize edilmelidir:
* **Blok 2 - 5000 (L1 Geçiş Dönemi):** %100 Miner/Staker (Masternodeler kurulurken ağ güvenliğini ve kazım gücünü sağlamak için).
* **Blok 5001 - 100.000:** %80 Masternode / %20 Miner-Staker
* **Blok 100.001+ (Olgunlaşma Dönemi):** **%60 Masternode / %40 Miner-Staker** (Sektör standardı en dengeli oran).

> [!NOTE]
> Ağ dual (hibrit) yapıda olduğundan, PoW madencileri (blok PoW ile üretildiğinde) veya PoS stakerları (blok PoS ile üretildiğinde) blok ödülünün %40'ını ve işlem ücretlerinin %100'ünü alarak sürekli teşvik edilirler. Miner/Staker payı hiçbir zaman %0'a düşmez.

### 3.4. Teminat (Collateral) Kademelendirmesi
Masternode kurmak için kilitlenmesi gereken teminat miktarı kademeli olarak artırılarak dolaşımdaki arzın kilitlenmesi (lock-up rate) teşvik edilir:
* **Blok 1 - 100.000:** 15.000 KRISTA
* **Blok 100.001 - 200.000:** 17.500 KRISTA
* **Blok 200.001+:** 20.000 KRISTA

---

## 4. Matematiksel Projeksiyon (10 Yıllık Simülasyon)

Önerilen yıllık %15 emisyon azalması (Decay) modeline göre blok ödülleri ve yıllık arz büyümesi:

* **1. Yıl (Blok 2 - 1.051.200):** Blok başına **25 KRISTA**
  - Yıllık Üretim: ~26.280.000 KRISTA
  - Yıl Sonu Toplam Arz: **31.280.000 KRISTA** (Premine dahil)
* **2. Yıl (Blok 1.051.201 - 2.102.400):** Blok başına **21.25 KRISTA** (%15 Azalma)
  - Yıllık Üretim: ~22.338.000 KRISTA
  - Yıl Sonu Toplam Arz: **78.618.000 KRISTA**
* **3. Yıl (Blok 2.102.401 - 3.153.600):** Blok başına **18.06 KRISTA**
  - Yıllık Üretim: ~18.984.672 KRISTA
  - Yıl Sonu Toplam Arz: **97.602.672 KRISTA**
* **4. Yıl ve Sonrası:** Hard Cap olan **100.000.000 KRISTA** sınırına ulaşıldığı için emisyon durur (Blok ödülü 0 olur, ağ sadece işlem ücretleri/transaction fees ile beslenir).

```
Arz Doyum Grafiği Projeksiyonu:
[5M] (Genesis) -> [31.28M] (Yıl 1) -> [53.61M] (Yıl 2) -> [72.6M] (Yıl 3) -> [100M Max Cap] (Yıl 4.5)
```

---

## 5. Bu Model Neden KRISTA'yı Prestijli (Reputable) Kılar?

1. **Deflasyonist Yapı:** Toplam arzın 100 Milyon gibi prestijli bir sınırda kilitli olması, birim değerin uzun vadede artmasını sağlar.
2. **Yüksek Kilitlenme Oranı (Lock-up Rate):** Masternode teminatlarının 20.000 KRISTA'ya çıkması, dolaşımdaki arzın %60-%70'inin masternodelerda kilitlenmesini sağlar. Bu durum borsalardaki likiditeyi daraltarak fiyatı yukarı taşır.
3. **Güvenli PoS:** Ödüllerin %40'ının doğrudan staking yapan cüzdanlara gitmesi, küçük yatırımcıların da coinlerini kilitleyip cüzdanlarını açık tutmasını (staking) sağlayarak ağın güvenliğini merkezsizleştirir.

---

## 6. Kod Seviyesinde Yapılacak Değişiklikler

Eğer bu planı onaylarsanız, kod üzerinde yapılacak basit ve etkili değişiklikler şunlardır:

1. **Maksimum Arz Limitinin Ayarlanması:**
   [src/chainparams.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/chainparams.cpp) içinde `consensus.nMaxMoneyOut = 100000000 * COIN;` (100M) olarak set edilmesi.
2. **Blok Ödülü Mantığının Güncellenmesi:**
   [src/masternode.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/masternode.cpp#L349-L378) içindeki `GetBlockValue` fonksiyonunu yıllık decay hesaplayacak şekilde güncellemek:
   ```cpp
   CAmount CMasternode::GetBlockValue(int nHeight) {
       CAmount maxMoneyOut = Params().GetConsensus().nMaxMoneyOut;
       if (nMoneySupply >= maxMoneyOut) return 0;

       if (nHeight == 1) return 5000000 * COIN; // Premine

       // Her 1.051.200 blokta bir (yılda bir) %15 azaltma
       int year = (nHeight - 2) / 1051200;
       double subsidy = 25.0 * pow(0.85, year);
       CAmount nSubsidy = (CAmount)(subsidy * COIN);

       if (nSubsidy <= 0) nSubsidy = 1 * COIN; // Minimum emisyon sınırı (isteğe bağlı)

       if (nMoneySupply + nSubsidy > maxMoneyOut) {
           return maxMoneyOut - nMoneySupply;
       }
       return nSubsidy;
   }
   ```
3. **Paylaşım Oranlarının Güncellenmesi:**
   [src/masternode.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/masternode.cpp#L380-L385) içinde `GetMasternodePayment` fonksiyonunu %60 masternode payı olacak şekilde güncellemek:
   ```cpp
    CAmount CMasternode::GetMasternodePayment(int nHeight) {
        if (nHeight <= 5000) return 0;
        return CMasternode::GetBlockValue(nHeight) * 60 / 100; // %60 MN, %40 Staker/Miner
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

Bu kesintiler doğrudan blok değerinden (block value) düşülerek blok üreticisinin coinbase ödülünden düşülür (örneğin 2-999. bloklar arasında madenciye giden coinbase ödülü 100 KRISTA yerine 92.3 KRISTA olur).
