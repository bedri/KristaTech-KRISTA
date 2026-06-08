# KristaTech (KRISTA) Tokenomics Implementation Plan

Bu doküman, KristaTech (KRISTA) blokzincirinin sürdürülebilir ve itibarlı (reputable) yeni tokenomics planının kod seviyesinde nasıl uygulanacağını detaylandırmaktadır. Projeye katkı sağlayan geliştiriciler için bir uygulama kılavuzudur.

---

## 1. Yeni Tokenomics Parametreleri

Yapılan finansal simülasyon ve analizler sonucunda aşağıdaki parametreler üzerinde uzlaşılmıştır:

* **Maksimum Arz (Strict Hard Cap):** **100.000.000 KRISTA** (100 Milyon). Arz bu sınıra ulaştığında blok ödülü tamamen sıfırlanır; madenciler ve stakerlar sadece işlem ücretlerini alır.
* **Başlangıç Blok Ödülü:** **14.5 KRISTA** (Blok 2 itibarıyla).
* **Yıllık Azalma (Decay):** **%20 Yıllık Sönümlenme**. Her 1.051.200 blokta bir (yaklaşık 1 yıl, 30 saniye blok spacing ile) blok ödülü %20 oranında azalır.
* **Ödül Dağılım Modeli (Kademeli):**
  * **Blok 2 - 5000 (Kurulum Dönemi):** %0 Masternode / %100 Miner-Staker (Kazım gücünü ve ağ bootstrap sürecini korumak için).
  * **Blok 5001 - 100.000:** %80 Masternode / %20 Miner-Staker.
  * **Blok 100.001+ (Olgunluk Dönemi):** %60 Masternode / %40 Miner-Staker.
* **Masternode Teminatı (Collateral):** **20.000 KRISTA** (Başlangıçtan itibaren sabit teminat).

---

## 2. Kod Seviyesindeki Değişiklikler

### 2.1. Maksimum Arz Sınırı
`src/chainparams.cpp` dosyasında ana ağ (Mainnet) için maksimum arz limiti 100.000.000 COIN olarak güncellenir:
```cpp
consensus.nMaxMoneyOut = 100000000 * COIN;
```

### 2.2. Masternode Teminatı
`src/masternode.cpp` dosyasında `CMasternode::GetMasternodeNodeCollateral(int nHeight)` fonksiyonu güncellenerek tüm blok yükseklikleri için sabit `20000 * COIN` döndürmesi sağlanır:
```cpp
CAmount CMasternode::GetMasternodeNodeCollateral(int nHeight) 
{
    return 20000 * COIN;
}
```

### 2.3. Yıllık Decay Hesaplaması
`src/masternode.cpp` içindeki `CMasternode::GetBlockValue(int nHeight)` fonksiyonu güncellenerek 14.5 başlangıç ödülü ve %20 yıllık decay algoritması uygulanır:
```cpp
CAmount CMasternode::GetBlockValue(int nHeight)
{
    CAmount maxMoneyOut = Params().GetConsensus().nMaxMoneyOut;

    if (nMoneySupply >= maxMoneyOut) {
        return 0;
    }

    if (nHeight == 1) {
        return 5000000 * COIN; // Genesis Premine (5M)
    }

    // Yıllık %20 azalma (Decay) - Her 1.051.200 blokta bir
    int year = (nHeight < 2) ? 0 : (nHeight - 2) / 1051200;
    double subsidy = 14.5 * pow(0.8, year);
    CAmount nSubsidy = (CAmount)(subsidy * COIN);

    // Limit kontrolü (100M aşılmamalıdır)
    if (nMoneySupply + nSubsidy > maxMoneyOut) {
        return maxMoneyOut - nMoneySupply;
    }

    return nSubsidy;
}
```

### 2.4. Ödül Dağılım Oranları
`src/masternode.cpp` dosyasında `CMasternode::GetMasternodePayment(int nHeight)` fonksiyonu kademeli dağılım planını uygulayacak şekilde güncellenir:
```cpp
CAmount CMasternode::GetMasternodePayment(int nHeight)
{
    if (nHeight <= 5000) return 0; // %0 MN, %100 Miner-Staker

    if (nHeight <= 100000) {
        return CMasternode::GetBlockValue(nHeight) * 80 / 100; // %80 MN, %20 Miner-Staker
    }

    return CMasternode::GetBlockValue(nHeight) * 60 / 100; // %60 MN, %40 Miner-Staker
}
```

---

## 3. Test ve Doğrulama Planı

### 3.1. Birim Testlerin (Unit Tests) Güncellenmesi
`src/test/main_tests.cpp` dosyasında `subsidy_limit_test` içindeki eski ödül değerlerine dair iddialar (assertions) güncellenerek yeni 14.5 KRISTA başlangıç ve yıllık %20 decay değerleri doğrulanır:
* Yükseklik 0, 50.000, 150.000, 250.000, 350.000, 450.000 testlerinin yeni değeri: `14.5 * COIN` (Yıl 0 içindeler).
* Yıl 1 (Blok 1.051.202): `11.6 * COIN` (14.5 * 0.8)
* Yıl 2 (Blok 2.102.402): `9.28 * COIN` (14.5 * 0.64)

Birim testleri çalıştırmak için:
```bash
podman run --rm --userns=keep-id -u 1000:1000 -v /home/bedri/Coin-Projects/KristaTech-KRISTA:/kristatech:z -w /kristatech localhost/kristatech-builder:latest make check
```

### 3.2. Cüzdan Derleme ve Senkronizasyon Testi
Değişikliklerin ardından ağdaki senkronizasyonun sorunsuz sürdüğünü doğrulamak için:
1. Kaynak kodu derleyin:
   ```bash
   podman run --rm --userns=keep-id -u 1000:1000 -v /home/bedri/Coin-Projects/KristaTech-KRISTA:/kristatech:z -w /kristatech localhost/kristatech-builder:latest make -j$(nproc)
   ```
2. Cüzdanı ve ağ düğümlerini yeniden başlatıp blokların doğrulanmasını kontrol edin.
