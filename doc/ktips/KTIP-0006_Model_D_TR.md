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
Kısa PoW bootstrap aşamasında (Mainnet ve Testnet'te `200`. blok öncesi `1..199` arası), ağ olgunlaşmamış doğrulayıcı havuzları yüzünden kilitlenme (deadlock) yaşamadan coinbase anahtarlarını ve ilk UTXO'ları oluşturur.

Model D, blok `200`'de tek seferde devreye giren birleşik geçiş modeliyle çalışır (`1..199` bootstrap $\rightarrow$ Blok `200` tam Model D + LLMQ + PoMBL):
1. **Bootstrap Dönemi (`1..199`)**: Model D öncesinde Pasif Masternode (`%5`) ve LLMQ (`%10`) ödemeleri `%0` olarak tutulur (Klasik PoW blokları `2..199` arasında net `%100` Madenci; Blok `200`'de doğrudan Dengeli Model D'ye geçilir). Blok `200` öncesinde kurul imzası doğrulaması henüz etkin değildir ($T = 0$).
2. **Olgunlaşma Dönemi (Dengeli Model D, Blok `200+`)**: Mainnet, Testnet ve Regtest üzerinde Blok `200` itibarıyla Model D gelişmiş LLMQ topolojilerini ve katı imza doğrulama eşiklerini zorunlu kılar (`UPGRADE_HARDENING_V4` Yükseklik `1` altında `nActiveMasternodes >= 8` olduğunda `8`-of-`15` ADAM eşiği, `< 8` iken `5` koruyucu eşik — ve `2/3` BFT LLMQ eşiği `floor(2N/3)+1 ∈ {4, 7, 8, 11}`), ADAM komitesini **`15` koltuğa (`8 MN + 2 Coin-Lock + 3 PoW-Lock + 2 Ücretsiz 0 KRISTA`)** ve **`%55` Aktif MN Koordinatör VRF tabanına** geçirir, nihai dağıtımı da **`%5` Pasif MN + `%10` 1-Blok Gecikmeli İmzacı-Odaklı LLMQ + `%25` Üretici (`PoS %22 Staker + %3 Koordinatör`) + `%60` Eşit Aktif Doğrulayıcılar (`%60 / 15 = %4,00`/koltuk)** şeklinde uygular.

## Spesifikasyon

### 1. Güncelleme Aktivasyon Zamanlaması
Model D güncellemesi (`Consensus::UPGRADE_MODELD`), `UPGRADE_POMBL`, `UPGRADE_ADAM_V2` ve `UPGRADE_STAKE_MIN_DEPTH_V2` ile birlikte Blok `200`'de tek seferde aktifleşir:

| Ağ | Aktivasyon Yüksekliği | Durum |
| --- | --- | --- |
| **Mainnet** | Blok 200 | Aktif |
| **Testnet** | Blok 200 | Aktif |
| **Regtest** | Blok 200 | Aktif |

---

### 2. Uzun Ömürlü Masternode Kurulları (LLMQ) ve DKG Oturumları
Model D altında ağ, aktif ve kayıtlı masternode'ları simüle edilmiş bir Dağıtılmış Anahtar Üretimi (DKG) oturumu kullanarak Uzun Ömürlü Masternode Kurulları (LLMQ) şeklinde organize eder.

* **Kurul Boyutu ($N$)**: Dinamik BFT ölçeklendirme (`UPGRADE_HARDENING_V1` / `src/llmq.cpp` içindeki `GetTargetQuorumSize`), doğrulanmış aktif zincir üstü Masternode sayısına göre ölçeklenir (`<25` $\rightarrow$ `5`, `25–49` $\rightarrow$ `9`, `50–99` $\rightarrow$ `11`, `>=100` $\rightarrow$ `15`). Aktif Masternode sayısı $< N$ ise eksik koltuklar kota sınırlı yedeklerle (`en fazla 1 Coin-Lock + en fazla 3 Fonlu PoW-Lock`) tamamlanır.
* **DKG Rotasyon Aralığı (`GetActiveQuorum`)**:
  * **Mainnet & Testnet**: Blok `200` (`nHeight >= 200`) itibarıyla aktifleştiği andan başlayarak her blokta (`H_{\text{DKG}} = H`) taze kurul seçilir; arada ikincil bir rotasyon geçişi yoktur.
  * **Regtest**: 800. bloktan önce aktif kurul rotasyon yüksekliği her **10 blokta** bir (`UPGRADE_POMBL = 300` alt sınırıyla), 800 ve sonrasında ise **her blokta** yeniden belirlenir:

    $$
    H_{\text{DKG}} = \begin{cases}
    \max\left(\lfloor H / 10 \rfloor \times 10,\; H_{\text{PoMBL}}\right) & H < 800 \text{ ise (yalnızca Regtest)} \\
    H & H \ge H_{\text{rot}} \text{ ise}
    \end{cases}
    $$
  
  Burada $H$ mevcut blok yüksekliğini (`nHeight`), $H_{\text{PoMBL}}$ `UPGRADE_POMBL` yüksekliğini (Mainnet/Testnet `200`, Regtest `300`), $H_{\text{rot}}$ Mainnet/Testnet'te $200$ (Regtest'te $800$) değerini ve $H_{\text{DKG}}$ ise aktif kurulun seçildiği rotasyon yüksekliğini temsil eder.

* **Kurul Seçimi**: Kurul üyeleri, bir önceki bloğun dönen entropi seed (tohum) değeri ve masternode teminat outpoint verisine dayalı bir puanlama sistemi kullanılarak tüm aktif masternode'ların deterministik olarak sıralanmasıyla seçilir. Zincir üzerinde $N$'den az doğrulanmış Aktif Masternode bulunduğunda (`nVerifiedOnChainMNs < N`), mevcut tüm gerçek Aktif Masternode'lar kurula doğrudan dahil edilerek gerçek Masternode operatörlerinin koltukları korunur. $N$ üyeli kurulu tamamlamak için eksik kalan koltuklar, kota izolasyonlu yedek aday havuzlarından (en yüksek puanlı Coin-Lock ve ardından Fonlu PoW-Lock doğrulayıcıları) doldurulur; her bir aday strictly kota limitlidir (en fazla 1 Coin-Lock + en fazla 3 Fonlu PoW-Lock). Böylece Bizans izolasyonu korunur ve tek bir tarafın veya çoklu kilit kümesinin LLMQ imzalama gücünü ele geçirmesi engellenir.

---

### 3. Kurul İmzası Doğrulama Eşikleri
Sürüm 12 ve üzeri bloklar, blok başlığı hash'ini doğrulayan bir kurul imzası yükü `vQuorumSig` içerir. `CQuorumSignature::Verify()` içinde `llmq::GetQuorumThreshold(N, nHeight)` (`src/llmq.cpp`) ile kurul imzası doğrulama eşiği ($T$) şu şekilde hesaplanır:

* **Model D Aktivasyonundan Önce (Mainnet / Testnet üzerinde `!NetworkUpgradeActive(nHeight, UPGRADE_MODELD)`)**:
  Aktif LLMQ kurulları zorunlu hale gelmeden önce bootstrap sürecini kolaylaştırmak için kurul imzası doğrulaması atlanır ($T = 0$).
* **Model D Aktif Olduktan Sonra (Tüm Ağlar: Mainnet, Testnet, Regtest)**:
  `UPGRADE_HARDENING_V4` (`"Hardening_v4"`, tüm ağlarda Yükseklik `1`) kapsamında eşik değer, katı ve dinamik bir `2/3` BFT nitelikli çoğunluğu ($\lfloor 2N / 3 \rfloor + 1$) olarak ayarlanır:
  
  $$T = \lfloor 2N / 3 \rfloor + 1 \in \{4, 7, 8, 11\}$$
  
  Bu doğrultuda, Model D aktif olduktan sonra tüm ağlarda blok hash'inin kurul büyüklüğüne göre dinamik `2/3` BFT nitelikli çoğunluğu (`5'te 4`, `9'da 7`, `11'de 8` veya `15'te 11`, `GetTargetQuorumSize` ile uyumlu) tarafından imzalanması gerekir. *(V4 öncesi tarihsel formül: $\lfloor N / 2 \rfloor + 1 \in \{3, 5, 6, 8\}$, yani `5'te 3`, `9'da 5`, `11'de 6`, `15'te 8`.)*

---

### 4. Ekonomik Model ve Blok Ödülü Dağılımları
Model D, blok ödülünün nihai ve sürdürülebilir ekonomik paylaşımını zorunlu tutar. Dağıtım yapısı blok türüne göre değişiklik gösterir:

#### A. Proof-of-Work (PoW) Blokları
PoW (ADAM kooperatif madenciliği) yoluyla üretilen bloklarda, dağıtılabilir blok ödülü $R$ (%7,0 Geliştirici Hazinesi düşüldükten sonra) Dengeli Model D + `UPGRADE_FAIRNESS` altında şu şekilde paylaştırılır:
* **Pasif Masternode Ödülü (%5)**: VRF tohumu modülo hesabı (`GetModelDPassiveMasternodePayee` içinde `seed.GetUint64(0) % vActiveMns.size()`) ile deterministik olarak seçilen aktif bir Masternode'a dağıtılır.
* **İmzacı-Odaklı LLMQ Ödülü (%10, 1-Blok Gecikmeli)**: $N \in \{5, 9, 11, 15\}$ kurul koltuğuna (`%10 / N`) bölünür ve **1-blok gecikmeyle yalnızca önceki bloğu imzalayan (`prevBlock.vQuorumSig`)** üyelere ödenir (`UPGRADE_HARDENING_V4` `M-4` PoSe adaleti altında LLMQ imzalama `-5` PoSe iyileşmesi sağlarken LLMQ imzalamama artık `+5` PoSe cezası doğurmaz — V4 öncesi `-5/+5` — bunun yerine `+5` PoSe kaçırılan birincil koordinatörlere ve yedek ikamesiyle yeri dolan sessiz birincil ADAM komitesi üyelerine uygulanır); imzalamayan paylar `%50` Yakım + `%50` Hazine olarak ayrılır.
* **Kooperatif Katılımcılar (%60 — Çözülen Koltuk Başına Eşit `1/15` = `%4,00`)**: Zorunlu 33 baytlık sıkıştırılmış pubkey ile geçerli PoW çözümü sunan tüm **15 koltuğa (`8 MN + 2 Coin-Lock + 3 PoW-Lock + 2 Ücretsiz 0 KRISTA`)** kulvar ayrımı olmadan **eşit `%60 / 15 = %4,00`** dağıtılır. Çözülmeyen koltuklar **%0** alır ve sahipsiz paylar (`nUnclaimedPartSplit`) **%50 Otomatik Protokol Yakımına (`nExpectedBurn = nUnclaimedPartSplit / 2`)** ve **%50 Geliştirici Hazinesine (`nExpectedTreasury = nTreasurySplit + (nUnclaimedPartSplit - nExpectedBurn)`)** bölüştürülür.
* **Blok Çözücü / Kazanan (%25 + `nFees` — Tam Kurtarma Ödülü)**: Bloğu öneren kazanan PoBLS Koordinatörüne (Model D altında `%55` aktif MN tabanıyla orantılı `4.200 : 250 : 10` VRF ile seçilir) kesintisiz olarak ödenir. $t \ge 60\text{ sn}$ sonrasında sessiz bir birincil koltuk meşru aynı kulvar yedek doğrulayıcısı ile değiştirildiğinde (`vAdamMiners[i] != vExpectedMiners[i]`), yedek doğrulayıcı eşit `%4,00` koltuk payını %60'lık havuzdan alır ve Koordinatörün %25'lik ödülünden ceza düşülmez (yerine geçilen sessiz birincil DIP3 Masternode ise `M-4` kapsamında `+5` PoSe alır). Birincil doğrulayıcılar fırsatçı ikame ve dışlamaya karşı şu üç kural ile korunur: (1) yedek rotasyonu $t = 60\text{ sn}$'de başlamadan önce birincil Koordinatör münhasırlık penceresinin $t = 45\text{ sn}$'de dolması (`llmq::QUORUM_EQUIVOCATION_GUARD_SECS = 45` ile uyumlu), (2) deterministik aynı kulvar yedek rank sıralaması (`vStandbyMiners`) ve (3) geçerli birincil çözüm yerel olarak önbellekteyse `qblockprop` üzerinde LLMQ ikame vetosu ($t \ge 60\text{ sn}$) ile R35-1 `5 sn` birincil dışlama vetosu (`mapAdamSolutionFirstSeen >= 5 sn`).

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
* Model D kuralları, blok `200`'de `UPGRADE_POMBL`, `UPGRADE_ADAM_V2` ve `UPGRADE_STAKE_MIN_DEPTH_V2` ile birlikte tek seferde devreye girer (`UPGRADE_POMBL` Mainnet/Testnet'te `200`, Regtest'te `300`; `UPGRADE_HARDENING_V4` tüm ağlarda `1`. blokta etkinleşir — aktif MN `>= 8` olduğu anda `8`-of-`15` Model D ADAM eşiğini uygular, yalnızca `< 8` iken koruyucu `5` veya Testnet'te `3` eşiğine düşer).
* `UPGRADE_MODELD` yüksekliğinin altındaki bloklarda dinamik `2/3` BFT (`UPGRADE_HARDENING_V4` altında `floor(2N/3)+1` ile `5'te 4`..`15'te 11`; V4 öncesi `5'te 3`..`15'te 8`) LLMQ `vQuorumSig` doğrulaması ve Model D ödül dağılımları uygulanmaz, böylece bootstrap davranışı korunur.

## Referans Uygulama
* Güncelleme aktivasyon bayrakları ve yükseklikleri: `src/chainparams.cpp`.
* Kurul imzası eşik mantığı: `src/llmq.cpp` içindeki `Verify` fonksiyonu.
* DKG oturum planlaması: `src/llmq.cpp` içindeki `GetActiveQuorum` fonksiyonu.
* Ödül dağılım mantığı: `src/masternode-payments.cpp` içindeki `FillBlockPayee`, `IsBlockPayeeValid` ve `ComputeModelDValidatorSeatPayments`, `src/masternode.cpp` içindeki `GetMasternodePayment` ve `src/miner.cpp`.
* Madenci kayıt geçerliliği kontrolü: `src/adam.cpp` içindeki `GetAdamMinerPool` fonksiyonu.
