```
KTIP: 0003
Başlık: PoMPA Ağırlık Modeli ve Çift Madencilik (PoMPA Weight & Dual Mining)
Yazar: KristaTech Core Developers
Durum: Active
Tür: Standard Track (Consensus)
Oluşturulma Tarihi: 2026-06-16
```

## Özet
Bu öneri, **PoMPA (Proof of Multi-Proof Algorithm)** ağırlık modelini ve **Çift Madencilik (Dual Mining)** konsensüs mekanizmasını tanımlamaktadır. PoMPA, standart Proof-of-Stake (PoS) mekanizmasını aktif katkı kanıtlarıyla ölçeklendirir: `UPGRADE_FAIRNESS` (Yükseklik `1`) altında Proof of Stake (PoS, `1x`), azaltılmış katsayılı Proof of Masternode (`PoM`, eski `2x` yerine $\alpha = 0,25$ ile `1,25x`'e kadar), canlı kilit koruyan Proof of Lock (PoL, `3x`'e kadar) ve Proof of Burn (PoB, `6x`'e kadar) dengeli biçimde birlikte çalışır. Çift Madencilik, PoS staking ile ADAM işbirlikçi PoW madenciliğinin paralel çalışmasını güvence altına alır.

## Motivasyon
Standart PoS ağları, aşağıdakiler dahil olmak üzere çeşitli konsensüs güvenlik açıklarına sahiptir:
1. **Nothing-at-Stake**: Onaylayıcılar (validators), rakip çatallanmalar üzerinde aynı anda sıfır maliyetle blok imzalayabilir ve bu da çatallanmanın çözülmesini zorlaştırır.
2. **Servet Merkezileşmesi**: En zengin staking cüzdanlarının blok üretimini domine etmesi.
3. **Düşük Ekonomik Hız**: Coinleri cüzdanda pasif olarak tutmanın yüksek oranda ödüllendirilmesi, aktif ağ katılımını veya kilitlenmeleri caydırır.

PoMPA; pasif token sahipliği yerine ağa olan bağlılığı (kilitlenmeler, yakımlar ve aktif masternode operasyonları) ödüllendiren dinamik bir staking ağırlık modeli sunarken, Çift Madencilik ise Nothing-at-Stake sorununu tamamen çözmek için fiziksel PoW'u PoS bloklarına bağlar.

## Teknik Özellikler

### 1. PoMPA Dinamik Ağırlık Modeli
Staking kernel hedef değerlendirmeleri (`src/kernel.cpp` içindeki `CStakeKernel::CheckKernelHash()`), `Consensus::UPGRADE_POMBL` aktifleştiğinde (Mainnet'te `4800`, Testnet'te `400`, Regtest'te `300`) `CalculateMPAWeight()` aracılığıyla hesaplanan bir staking ağırlık çarpanı kullanır. `UPGRADE_FAIRNESS` (Yükseklik `1`) kapsamında `CalculateMPAWeight()`, `nHeight >= 2280` taşma korumasıyla **`1. PoM (1,25x'e kadar, alpha = 0,25) -> 2. PoL (3x, canlı yenilenebilir CLTV) -> 3. PoB (6x) -> 4. PoS (1x)`** önceliğini uygular:

$$
W_{\text{effective}} \in \left\lbrace W_{\text{PoM}}, W_{\text{PoL}}, W_{\text{PoB}}, W_{\text{PoS}} \right\rbrace
$$

Her kademe aşağıda tanımlanmıştır:

#### A. Proof of Stake (PoS - Temel Ağırlık)
Temel ağırlık, cüzdanda tutulan kilitsiz, standart coin miktarına karşılık gelir:

$$W_{\text{PoS}} = \text{Amount}$$

---

#### B. Proof of Lock (PoL - Canlı Kilit Koruyucu Zaman Kilidi Bonusu)
Mutlak (`OP_CHECKLOCKTIMEVERIFY`) veya göreceli (`OP_CHECKSEQUENCEVERIFY`) script zaman kilitleri kullanılarak $L$ blok süresi boyunca kilitlenen coinler bir kilit çarpanı alır:

$$W_{\text{PoL}} = \text{Amount} \times \left(1 + \gamma \cdot \min\left(\frac{L}{L_{\text{MAX}}}, 1.0\right)\right)$$

Burada:
* $\gamma = 2.0$, maksimum %200 bonusu temsil eder (3 kata kadar ağırlık çarpanı sağlar).
* $L_{\text{MAX}} = 50,000$ blok, ödüllendirilen maksimum kilit süresini temsil eder.
* `UPGRADE_FAIRNESS` (Yükseklik `1`) kapsamında **canlı `TX_CLTV_TIMELOCK` kendi kendini yenileme** özelliği sayesinde, olgunlaşmış bir CLTV UTXO'su (`OP_CLTV` kilit yüksekliği henüz dolmadan bile) `vout[1].scriptPubKey == prevout.scriptPubKey` ve `vout[1].nValue >= prevout.nValue` şartlarını sağlayan tek-girdili coinstake üreterek anapara ve kilit betiğini koruyup stake edebilir.

---

#### C. Proof of Burn (PoB - Yakım-Zaman Aşımı Ağırlığı)
Standart (coinbase / coinstake olmayan: `!tx.IsCoinBase() && !tx.IsCoinStake()`) işlemlerle kayıtlı harcanamaz yakım adresine (Mainnet üzerinde `KTburnCYcDjF1GsXa5k3tc3eZrVVbqefyWr` / `KTburnUHdmKMVx4z8BcnZYZCWYKxRgfZp95`, Testnet üzerinde `ktBurn42LtQP2pJ2fS5X2kpRx4Sd86kNgx4` / `ktBurnKmNRzVXVVVDXxFhhKytkH5x2yqsCG`) gönüllü olarak gönderilen coinler, `mapAddressBurns` içinde zamanla ($T$, yakım bloğundan bu yana geçen blok sayısı) doğrusal olarak azalan yüksek bir kişisel PoB staking başlangıç çarpanı alır. `CoinBase` veya `CoinStake` çıktılarındaki otomatik protokol yakımları ise yalnızca `nMoneySupply` arzından kalıcı olarak düşülür ve kişisel PoB ağırlığı **oluşturmaz**:

$$W_{\text{PoB}} = \text{Amount} + \min\left(\text{BurnWeight}_{\text{aktif}}, \text{Amount} \times \beta\right)$$

Burada:
* $\text{BurnWeight}_{\text{aktif}} = \text{BurnAmount} \times \beta \times \left(1 - \frac{T}{T_{\text{MAX}}}\right)$.
* $\beta = 5.0$, yakılan miktar üzerinde başlangıçta 5 katı çarpanı temsil eder. UTXO başına yakma bonusunun $\text{Amount} \times \beta$ ile sınırlandırılması, bakiyeyi binlerce küçük UTXO'ya bölerek sahte ağırlık çarpanı patlaması oluşturulmasını engelleyerek %100 UTXO-bölme duyarsızlığı sağlar.
* $T_{\text{MAX}}$ (`nBurnDecayBlocks`), Mainnet üzerinde **500.000 blok** (Testnet ve Regtest üzerinde **10.000 blok**) olup yakım ağırlığının sönümlenme süresini temsil eder. $T \ge T_{\text{MAX}}$ olduğunda, yakım ağırlığı çarpanı 0'a düşer ve geriye yalnızca temel UTXO miktarı kalır.

---

#### D. Proof of Masternode (PoM - `UPGRADE_FAIRNESS` Altında Azaltılmış Sadakat Bonusu, `1,25x`)
Zincir üstünde `%100` deterministik olarak doğrulanan aktif Masternode'lar (`evodb` KTDIP3 kayıt/coin yüksekliği veya eski `IsMasternodeActiveAtHeight` 2.880 blok penceresi ve `IsValidMasternodeCollateralAndAuth`) `4.200 KRISTA` teminatları için bir kesintisiz çalışma (uptime) çarpanı alır (`W_PoM`):

$$W_{\text{PoM}} = C \times \left(1 + \alpha \cdot \min\left(\frac{t_{\text{active}}}{T_{\text{MAX}}}, 1.0\right)\right)$$

**`UPGRADE_FAIRNESS` (tüm ağlarda Blok `1`)** kapsamında $\alpha = 0,25$ (**en fazla `1,25x` / `+%25`**, kesin tamsayı formülü `C + floor(C * capped / (4 * T_MAX))`, Mainnet'te $T_{\text{MAX}} = 100.000$ / Testnet ve Regtest'te $10.000$ blok) uygulanır; böylece Model D Pasif MN (`%5`) ve LLMQ (`%10`) ödüllerinin yanında ölçülü bir sadakat bonusu sağlanırken `PoM (1,25x) << PoL (3x) < PoB (6x)` adalet dengesi korunur. *(Tarihsel Fairness öncesi parametre: $\alpha = 1,0$, en fazla `2,0x`.)*

---

### 2. Çift Madencilik ve Staking Entegrasyonu
Çift Madencilik (Dual Mining), PoS blok doğrulamasını ADAM işbirlikçi PoW çözücü ile birleştirir (işbirlikçi ADAM PoW bloklarıyla paralel çalışır ve `UPGRADE_POMBL` aktifleştiğinde ebeveyn blok türüne göre dönüşümlü `1.500 ms` `ADAM_SLOT_PRIORITY_WINDOW_MS` slot açılış önceliği uygular). İşbirlikçi bir PoS bloğu (`IsProofOfStake()`) ancak şu durumlarda ağ tarafından kabul edilir:
1. Staking kernel kontrolü hedef zorluğu karşılıyorsa:

   $$\text{kernelHash} < \left\lfloor \frac{W_{\text{total}}}{100} \right\rfloor \times \text{Target}$$

2. Blok başlığı, seçilen ADAM madencilerinden gelen geçerli, imzalanmış kısmi PoW çözümlerinin bir kuorumunu (quorum) içeriyorsa.
3. PoS bloğu, stake edenin anahtarı (`vchBlockSig`) ve seçilen ADAM koordinatörünün anahtarı (`vAdamCoordinatorSig`, ayrıca `UPGRADE_MODELD` aktifleştiğinde `GetTargetQuorumSize` ile `5 -> 9 -> 11 -> 15` ölçeklenen dinamik BFT LLMQ `vQuorumSig` imzası) ile imzalanmışsa.

## Geriye Dönük Uyumluluk
* PoMPA ağırlıkları ve Çift Madencilik, blok yüksekliğine bağlı olarak koşullu şekilde etkinleştirilir:
  - PoS staking Mainnet/Testnet üzerinde `200`, Regtest üzerinde `251` blok yüksekliğinde etkinleşir (`Consensus::UPGRADE_POS`).
  - Standart Mod Sürüm 12 blok serileştirmesi (`Consensus::UPGRADE_POMBL`) Mainnet üzerinde `4800`, Testnet üzerinde `400`, Regtest üzerinde `300` yüksekliğinde etkinleşir.
* `UPGRADE_POS` öncesindeki eski PoW bloklarında PoMPA ağırlıkları değerlendirilmez.

## Referans Uygulama
* Ağırlık hesaplamaları ve kernel doğrulaması: `src/kernel.cpp` içindeki `CalculateMPAWeight()`, `CStakeKernel::CheckKernelHash()` ve `CheckProofOfStake()`.
* Staking madencilik iş parçacığı: `src/miner.cpp` içindeki `ThreadStakeMinter()`.
* Coinbase ve coinstake işlem doğrulaması: `src/main.cpp` içindeki `CheckBlock()`.
