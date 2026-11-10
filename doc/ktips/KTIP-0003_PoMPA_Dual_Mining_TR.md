```
KTIP: 0003
Başlık: PoMPA Ağırlık Modeli ve Çift Madencilik (PoMPA Weight & Dual Mining)
Yazar: KristaTech Core Developers
Durum: Active
Tür: Standard Track (Consensus)
Oluşturulma Tarihi: 2026-06-16
```

## Özet
Bu öneri, **PoMPA (Proof of Multi-Proof Algorithm)** ağırlık modelini ve **Çift Madencilik (Dual Mining)** konsensüs mekanizmasını tanımlamaktadır. PoMPA, standart Proof-of-Stake (PoS) mekanizmasını; Proof of Stake (PoS), Proof of Lock (PoL), Proof of Burn (PoB) ve Proof of Masternode (PoM) olmak üzere dört farklı katkı kanıtı kullanarak blok stake etme kernel ağırlıklarını dinamik olarak ölçeklendirerek geliştirir. Çift Madencilik, PoS staking ile ADAM işbirlikçi PoW madenciliğinin paralel çalışmasına izin vererek ağı çift kilitli (dual-locking) bir doğrulama döngüsü altında güvence altına alır.

## Motivasyon
Standart PoS ağları, aşağıdakiler dahil olmak üzere çeşitli konsensüs güvenlik açıklarına sahiptir:
1. **Nothing-at-Stake**: Onaylayıcılar (validators), rakip çatallanmalar üzerinde aynı anda sıfır maliyetle blok imzalayabilir ve bu da çatallanmanın çözülmesini zorlaştırır.
2. **Servet Merkezileşmesi**: En zengin staking cüzdanlarının blok üretimini domine etmesi.
3. **Düşük Ekonomik Hız**: Coinleri cüzdanda pasif olarak tutmanın yüksek oranda ödüllendirilmesi, aktif ağ katılımını veya kilitlenmeleri caydırır.

PoMPA; pasif token sahipliği yerine ağa olan bağlılığı (kilitlenmeler, yakımlar ve aktif masternode operasyonları) ödüllendiren dinamik bir staking ağırlık modeli sunarken, Çift Madencilik ise Nothing-at-Stake sorununu tamamen çözmek için fiziksel PoW'u PoS bloklarına bağlar.

## Teknik Özellikler

### 1. PoMPA Dinamik Ağırlık Modeli
Staking kernel hedef değerlendirmeleri (`src/kernel.cpp` içindeki `CStakeKernel::CheckKernelHash()`), `Consensus::UPGRADE_POMBL` aktifleştiğinde (Mainnet'te `2000`, Testnet'te `400`, Regtest'te `300`) `CalculateMPAWeight()` aracılığıyla dinamik olarak hesaplanan bir staking ağırlık çarpanı kullanır. Her staking UTXO'su (`prevout`) için `CalculateMPAWeight()`, `nHeight >= 2280` itibarıyla 256-bit taşma korumasıyla birlikte dört öncelik kademesini (`1. PoM -> 2. PoL -> 3. PoB -> 4. PoS`) sırasıyla değerlendirir:

$$W_{\text{effective}} \in \left\{W_{\text{PoM}},\, W_{\text{PoL}},\, W_{\text{PoB}},\, W_{\text{PoS}}\right\}$$

Her kademe aşağıda tanımlanmıştır:

#### A. Proof of Stake (PoS - Temel Ağırlık)
Temel ağırlık, cüzdanda tutulan kilitsiz, standart coin miktarına karşılık gelir:

$$W_{\text{PoS}} = \text{Amount}$$

---

#### B. Proof of Lock (PoL - Zaman Kilidi Bonusu)
Mutlak (`OP_CHECKLOCKTIMEVERIFY`) veya göreceli (`OP_CHECKSEQUENCEVERIFY`) script zaman kilitleri kullanılarak $L$ blok süresi boyunca kilitlenen coinler bir kilit çarpanı alır:

$$W_{\text{PoL}} = \text{Amount} \times \left(1 + \gamma \cdot \min\left(\frac{L}{L_{\text{MAX}}}, 1.0\right)\right)$$

Burada:
* $\gamma = 2.0$, maksimum %200 bonusu temsil eder (3 kata kadar ağırlık çarpanı sağlar).
* $L_{\text{MAX}} = 50,000$ blok, ödüllendirilen maksimum kilit süresini temsil eder.

---

#### C. Proof of Burn (PoB - Yakım-Zaman Aşımı Ağırlığı)
Standart (coinbase / coinstake olmayan: `!tx.IsCoinBase() && !tx.IsCoinStake()`) işlemlerle kayıtlı harcanamaz yakım adresine (Mainnet üzerinde `KTburnCYcDjF1GsXa5k3tc3eZrVVbqefyWr` / `KTburnUHdmKMVx4z8BcnZYZCWYKxRgfZp95`, Testnet üzerinde `ktBurn42LtQP2pJ2fS5X2kpRx4Sd86kNgx4` / `ktBurnKmNRzVXVVVDXxFhhKytkH5x2yqsCG`) gönüllü olarak gönderilen coinler, `mapAddressBurns` içinde zamanla ($T$, yakım bloğundan bu yana geçen blok sayısı) doğrusal olarak azalan yüksek bir kişisel PoB staking başlangıç çarpanı alır. `CoinBase` veya `CoinStake` çıktılarındaki otomatik protokol yakımları ise yalnızca `nMoneySupply` arzından kalıcı olarak düşülür ve kişisel PoB ağırlığı **oluşturmaz**:

$$W_{\text{PoB}} = \text{Amount} + \text{BurnAmount} \times \beta \times \left(1 - \frac{T}{T_{\text{MAX}}}\right)$$

Burada:
* $\beta = 5.0$, yakılan miktar üzerinde başlangıçta 5 katı çarpanı temsil eder.
* $T_{\text{MAX}}$ (`nBurnDecayBlocks`), Mainnet üzerinde **500.000 blok** (Testnet ve Regtest üzerinde **10.000 blok**) olup yakım ağırlığının sönümlenme süresini temsil eder. $T \ge T_{\text{MAX}}$ olduğunda, yakım ağırlığı çarpanı 0'a düşer ve geriye yalnızca temel UTXO miktarı kalır.

---

#### D. Proof of Masternode (PoM - Masternode Yaş Bonusu)
Zincir üstünde %100 deterministik olarak doğrulanan (`IsMasternodeActiveAtHeight` ile son 2.880 blok içinde aktif ve `IsValidMasternodeCollateralAndAuth` ile harcanmamış $C = 4,200 \text{ KRISTA}$ teminata sahip) Aktif Masternode'lar, zincir üstü aktif ömürleri $t_{\text{active}} = nHeight - nFirstHeight$ üzerinden bir Masternode yaş çarpanı alır:

$$W_{\text{PoM}} = C \times \left(1 + \alpha \cdot \min\left(\frac{t_{\text{active}}}{T_{\text{MAX}}}, 1.0\right)\right)$$

Burada:
* $\alpha = 1.0$, maksimum %100 yaş bonusunu temsil eder (teminat üzerinde 2 kata kadar ağırlık çarpanı sağlar).
* $T_{\text{MAX}}$ (`nMasternodeUptimeLimit`), Mainnet üzerinde **100.000 blok** (Testnet ve Regtest üzerinde **10.000 blok**) olup teminat UTXO yüksekliği ve `mapMasternodeActiveHeights` üzerinden hesaplanan maksimum aktif yaşı temsil eder.

---

### 2. Çift Madencilik ve Staking Entegrasyonu
Çift Madencilik (Dual Mining), PoS blok doğrulamasını ADAM işbirlikçi PoW çözücü ile birleştirir. Bir blok ancak şu durumlarda ağ tarafından kabul edilir:
1. Staking kernel kontrolü hedef zorluğu karşılıyorsa:

   $$\text{kernelHash} \le W_{\text{total}} \times \text{Target}$$

2. Blok başlığı, seçilen ADAM madencilerinden gelen geçerli, imzalanmış kısmi PoW çözümlerinin bir kuorumunu (quorum) içeriyorsa.
3. Blok, stake edenin anahtarı (`vchBlockSig`) ve seçilen ADAM koordinatörünün anahtarı (`vAdamCoordinatorSig`) ile çift imzalanmışsa.

## Geriye Dönük Uyumluluk
* PoMPA ağırlıkları ve Çift Madencilik, blok yüksekliğine bağlı olarak koşullu şekilde etkinleştirilir:
  - PoS staking Mainnet üzerinde `200`. blok yüksekliğinde etkinleşir (`Consensus::UPGRADE_POS`).
  - Standart Mod Sürüm 12 blok serileştirmesi Mainnet üzerinde `2000` veya Testnet üzerinde `400` yüksekliğinde etkinleşir.
* 200. blok yüksekliğinden önceki eski PoW bloklarında PoMPA ağırlıkları değerlendirilmez.

## Referans Uygulama
* Ağırlık hesaplamaları: `src/kernel.cpp` içindeki `CalculateMPAWeight()` ve `CheckStakeKernelHash()`.
* Staking madencilik iş parçacığı: `src/wallet/wallet.cpp` içindeki `ThreadStakeMinter()`.
* Coinbase ve coinstake işlem doğrulaması: `src/main.cpp` içindeki `CheckBlock()`.
