```
KTIP: 0002
Başlık: ADAM İşbirlikçi Konsensüsü (ADAM Cooperative Consensus)
Yazar: KristaTech Core Developers
Durum: Active
Tür: Standard Track (Consensus)
Oluşturulma Tarihi: 2026-06-16
```

## Özet
Bu öneri, **ADAM (A Decentralized Approach Model)** işbirlikçi konsensüs mekanizmasını tanıtmaktadır. ADAM, blok doğrulama ve oluşturma süreçlerini üç ayrı işbirlikçi aşamaya böler: lider seçimi (leader election), hafif bulmaca çözme (lightweight puzzle solving) ve blok imza birleştirme (block signature aggregation). Her blok için seçilen birden fazla düğümün (Dengeli Model D altında `15` madenci [Model D öncesinde `11`] ve `1` koordinatör) işbirlikçi çalışmasını zorunlu kılarak madencilik havuzu merkezileşmesini, bencil madenciliği ve blok liderlerine yönelik hedefli DoS saldırılarını önler.

## Motivasyon
Geleneksel Proof-of-Work (PoW) protokolleri, birkaç büyük oluşumun blok üretimine hakim olduğu madencilik havuzu merkezileşmesinden muzdariptir. Ayrıca, bencil madencilik (selfish mining) saldırılarına ve kimlikleri önceden öngörülebilen blok üreticilerine yönelik hedefli Denial-of-Service (DoS) saldırılarına karşı savunmasızdırlar. Standart Proof-of-Stake (PoS) protokolleri ise "nothing-at-stake" (risksiz stake etme) çatallanmalarından etkilenir.

ADAM, bir grup onaylayıcı düğümü birlikte çalışmaya zorlayarak bu güvenlik açıklarını çözer:
* Blok üreticisinin (Koordinatör) kimliği, blok üretilene kadar gizli ve öngörülemez kalır.
* Blok hash değeri, seçilen birden fazla bağımsız madencinin sıralı kriptografik çalışmasına bağlı olduğundan bencil madenciliği etkisiz hale getirir.
* Masternode'lar temel seçim havuzunu oluşturarak Sybil saldırılarını engeller.

## Teknik Özellikler

### 1. Deterministik Lider Seçimi (SSLE)
Her $H$ blok yüksekliğinde, aktif onaylayıcı havuzu (masternode'lar ve kayıtlı madenciler) sorgulanır. Ağ, hareketli bir rastgele tohuma (rolling seed) dayalı olarak Dengeli Model D altında $N = 15$ Madenci (Model D öncesinde $N = 11$) ve $1$ Koordinatör seçmek için bir Tekli Gizli Lider Seçimi (SSLE) protokolü kullanır:

$$\text{Seed}_H = \text{Hash}\left(\text{Seed}_{H-1} \mathbin{\Vert} \text{VRFProof}_{H-1}\right)$$

Burada $\text{VRFProof}_{H-1}$, $H-1$ blokunun Koordinatörü tarafından oluşturulan belirlenimci hibrit imzadır.

Aktif havuzdaki her $i$ düğümü sıralanır:

$$\text{Rank}_i = \text{Hash}\left(\text{Seed}_H \mathbin{\Vert} \text{PubKey}_i\right)$$

Havuz, rank değerlerine göre artan düzende sıralanır. `nHeight >= consensus.nAdamDualTrackHeight` (Mainnet/Testnet'te `UPGRADE_ADAM` ile eşleşen blok `200`, Regtest'te blok `1`) olduğunda, $N$ Madenci (Model D öncesinde `11`; `UPGRADE_MODELD` ile Mainnet'te blok `5.000`, Testnet'te `500`, Regtest'te `200` itibarıyla `15` koltuğa genişler) **Çift Kulvarlı Kota Seçimi (`ElectDualTrackValidators`)** ile belirlenir:
* **1. Kulvar (Teminatlı Havuz — Model D'de `10` Koltuk / Öncesinde `8`)**: **8 koltuk** Aktif Masternode'lara (`4.200 KRISTA`; Model D öncesinde `7` koltuk, `static_assert(ADAM_MODELD_MASTERNODE_TRACK_QUOTA >= 8)` ve `ADAM_MASTERNODE_TRACK_QUOTA >= 7` ile güvence altına alınmış) ve **2 kesin izole koltuk** Coin-Lock madencilerine (`250 KRISTA`; Model D öncesinde `1` koltuk) ayrılır (boş Coin-Lock koltukları Aktif MN'lere devredilir).
* **2. Kulvar (Açık PoW Havuzu — Model D'de `5` Koltuk / Öncesinde `3`)**: `nHeight >= consensus.nAdamOpenEntryHeight` (Mainnet'te blok `2.500`, Testnet/Regtest'te blok `1`) itibarıyla Model D altında **3 koltuk** (Model D öncesinde **2 koltuk**) Fonlu PoW-Lock madencilerine (`10 KRISTA`, boşta ise Aktif MN / Bootstrap'a devredilir) ve Model D altında **2 kalıcı açık koltuk** (Model D öncesinde **1 koltuk**) `0 KRISTA` Open-Entry madencilerine (`Kademe 2C`, `UPGRADE_FAIRNESS` Blok `1` itibarıyla Mainnet `15`-bit `HashX11KVS` PoW ve `240` anahtar kapasitesi, boşta ise Fonlu PoW-Lock / Bootstrap'a devredilir) ayrılır. Seçilemeyen `Coin-Lock` kayıtları `Kademe 2A` dışına çıkarılmış olup yalnızca Model D'de `13`'ten az MN-dışı/Coin-Lock-dışı aday kaldığında (Model D öncesinde `< 10`) son çare canlılık yedeğidir. Boş koltuklar katı sırayla doldurulur: **Aktif Masternode $\to$ Açık PoW $\to$ Coin-Lock (son çare)** — ve `UPGRADE_FAIRNESS` altında çözülen her koltuk kulvar ayrımı olmadan **tam eşit pay** alır.
`UPGRADE_HARDENING_V2` ve `UPGRADE_FAIRNESS` (`nActivationHeight = 1`) kapsamında birincil Koordinatör, fonlu doğrulayıcılar arasından **Orantılı Kilitli Sermaye VRF (`4.200` Aktif MN, `250` `Coin-Lock`, `10` `Fonlu PoW-Lock` ağırlığı ve kulvar içi temsilci tavanı olmadan; Model D öncesinde `ADAM_FAIR_COORD_V4` ile `3 * sumNonMN > sumMN` olduğunda kesin `>= %75` Aktif MN tabanı, `UPGRADE_MODELD` sonrasında ise `ADAM_FAIR_COORD_V5` ile `11 * sumNonMN > 9 * sumMN` olduğunda kesin `>= %55` Aktif MN güvenlik tabanına geçiş; eski `65:8:2` ağırlığının yerini alır)** ile seçilir (fonlu doğrulayıcı yoksa `Bootstrap -> Zero-Coin PoW` yedeği kullanılır).

---

### 2. Hafif PoW Bulmaca Çözümü
Seçilen madenciler, hafif bir kriptografik bulmacayı çözerek iş yaptıklarını kanıtlamalıdır. Hedef zorluk limiti, bir bit kaydırma (bit-shift) çarpanı kullanılarak gevşetilir:

$$\text{scaledTarget} = \text{Target} \ll \text{activeShift}$$

Kaydırma değeri blok yüksekliğine bağlıdır (`NetworkUpgradeActive(nHeight, Consensus::UPGRADE_ADAM_V2)`):
* `Consensus::UPGRADE_ADAM_V2` öncesinde (Mainnet üzerinde blok `5.000`, Testnet üzerinde blok `500`, Regtest üzerinde blok `200`) $\text{activeShift} = \text{nAdamDifficultyShiftV1} = 10$.
* `Consensus::UPGRADE_ADAM_V2` ve sonrasında (Mainnet üzerinde blok `5.000`, Testnet üzerinde blok `500`, Regtest üzerinde blok `200`) $\text{activeShift} = \text{nAdamDifficultyShiftV2} = 6$.

Madenciler aşağıdaki hedef denklemi çözer:

$$\text{PuzzleHash}_i \le \text{scaledTarget}$$

Fallback Modunda (Sürüm 11, `Consensus::UPGRADE_POMBL` öncesinde), $\text{PuzzleHash}_i$ `UPGRADE_FAIRNESS` Blok `1` itibarıyla tek-geçişli `17` algoritma arasından (`kUniformAlgos`, asimetrik `X11KVS` `algo 11` hariç; Fairness öncesi `18`) deterministik olarak seçilen tek bir $\text{algo}_i = \text{GetAdamPuzzleAlgo}(\text{Seed}_H, \text{MinerPubKey}_i)$ algoritmasını kullanır. `Consensus::UPGRADE_POMBL` (Sürüm 12) ile birlikte $\text{PuzzleHash}_i$, zorunlu 33 baytlık sıkıştırılmış pubkey ve `4.080` uniform permütasyonlu 3-permütasyon seçici şemasıyla (`GetAdam3PermutationAlgos`) hesaplanır:

$$\text{PuzzleHash}_i = \text{algo1}\left( (i + 1) \times \text{algo2}\left( (i + 1) \times \text{algo3}(\text{Challenge}) \right) \right) \pmod{2^{256}}$$

Serileştirilmiş mücadele (`CDataStream`) verisi ise şu şekilde tanımlanır:

$$\text{Challenge} = \text{Seed}_H \mathbin{\Vert} \text{CompactSize}(\text{MinerPubKey}_i) \mathbin{\Vert} \text{MinerPubKey}_i \mathbin{\Vert} \text{LE32}(\text{Nonce}_i)$$

---

### 3. Blok Hashing Zinciri (Durumsuz Hashing)
Blok başlığını, seçilen tüm madencilerin işlerine kriptografik olarak bağlamak için, blok hash'i ($H_{\text{block}}$), seçilen tüm madencilerin hashing işlemlerini sıralı olarak zincirleyerek hesaplanır. `CBlockHeader::GetHash()` (`src/primitives/block.cpp`) içinde `fFallbackMode = (nVersion == 11) || (vAdamMiners.size() == vAdamSolutions.size() + 1)` kuralı uygulanır; dolayısıyla Sürüm 11 (veya sonunda ayrı bir Koordinatör anahtarı taşıyan eski fallback başlıkları) $M = \text{vAdamMiners.size()} - 1$ tur işlerken, eşit uzunluklu vektörlere sahip tüm Sürüm 12 başlıkları (Model D'de `15`, Model D öncesinde `11` koltuk) 3-permütasyon modunda $M = \text{vAdamMiners.size()}$ turun tamamını zincirler.

Serileştirilmiş bir blok başlığı $S$ için:
1. Her $i \in \lbrace 0, \dots, M-1 \rbrace$ turu için (burada $M$ madenci sayısıdır), hash entropisini korumak için tek bir aralarında asal çarpan $m_i$ hesaplarız:

   $$\text{roundHash}_i = \text{Hash}\left(\text{hashPrevBlock} \mathbin{\Vert} i\right)$$

   $$m_i = \max\left(\text{roundHash}_i[0] \mid 1, 3\right)$$

2. Turlar sıralı olarak zincirlenir:
   - **Tur 0**:
     * Madenci 0 için $\text{algo1}$, $\text{algo2}$ ve $\text{algo3}$ türetilir.
     * Hesaplama:

       $$H_0^{(3)} = \text{CalculateAdamPuzzleHash}(\text{algo3}, S)$$

       $$H_0^{(2)} = \text{CalculateAdamPuzzleHash}\left(\text{algo2}, \left( H_0^{(3)} \times 1 \right) \pmod{2^{256}}\right)$$

       $$H_0 = \text{CalculateAdamPuzzleHash}\left(\text{algo1}, \left( H_0^{(2)} \times 1 \right) \pmod{2^{256}}\right)$$

     * Çarpanı uygula:

       $$H_{\text{prev}} = (H_0 \times m_0) \pmod{2^{256}}$$

   - **Tur $i > 0$**:
     * Madenci $i$ için $\text{algo1}$, $\text{algo2}$ ve $\text{algo3}$ türetilir.
     * Hesaplama:

       $$H_i^{(3)} = \text{CalculateAdamPuzzleHash}(\text{algo3}, H_{\text{prev}})$$

       $$H_i^{(2)} = \text{CalculateAdamPuzzleHash}\left(\text{algo2}, \left( H_i^{(3)} \times (i + 1) \right) \pmod{2^{256}}\right)$$

       $$H_i = \text{CalculateAdamPuzzleHash}\left(\text{algo1}, \left( H_i^{(2)} \times (i + 1) \right) \pmod{2^{256}}\right)$$

     * Çarpanı uygula:

       $$H_{\text{prev}} = (H_i \times m_i) \pmod{2^{256}}$$

3. Nihai çıktı blok hash'idir:

   $$H_{\text{block}} = H_{\text{prev}}$$

---

### 4. Quorum Dirençli Yanıt Verme, Yedek Koltuk Devri ve Zamana Bağlı Eşik Gevşetme
Çevrimdışı veya yavaş madencilerin blok üretimini dondurmasını önlemek amacıyla, minimum bir quorum eşiği ($T = \text{GetAdamThreshold}(nHeight)$) kadar çözüm alındığı sürece blok şablonları oluşturulabilir ve ağa yayılabilir:
* **Bootstrap Aşaması (`nHeight < nAdamThresholdBootstrapLimit`, yani Mainnet'te 50.000, Testnet'te 100.000, Regtest'te 1.000)**: Model D altında $15$ madenciden (Model D öncesinde $11$) en az $T = 5$'i.
* **Bootstrap Sonrası (`nHeight >= nAdamThresholdBootstrapLimit`)**: Mainnet / Regtest üzerinde Model D altında $15$ madenciden en az $T = 8$'i (`ADAM_MODELD_MAX_CONSENSUS_THRESHOLD = 8`; Model D öncesinde $11$ madenciden $T = 7$'si; Testnet üzerinde `3`).
* **Yedek Doğrulayıcı Koltuk Devri ($t \ge 60\text{ sn}$)**: Birincil koltuklar `nTimeElapsed >= 60 sn` (`ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS = 60`) boyunca sessiz kalırsa, `ResolveAdamCommitteeForTime` / `ValidateAdamCommitteeForTime` aktif Koordinatörün çözülmemiş birincil koltukları yalnızca aynı kulvardaki yedek adaylarla (`MN -> MN`, `Coin-Lock -> Coin-Lock`, `Open PoW -> Open PoW`) sıralı olarak değiştirmesine izin verir (bloğun kendi `CCoinsViewCache* pView` görünümüne göre doğrulanır).
* **Zamana Bağlı Eşik Gevşetme ($t \ge 120\text{ sn}$)**: Komite buna rağmen temel $T$ eşiğine ulaşamazsa, `GetAdamEffectiveThreshold(nHeight, nTimeElapsed, nActiveMasternodes)` fonksiyonu $t = 120\text{ sn}$'den itibaren her `60 sn`'de bir eşiği `1` düşürerek Model D altında **`ADAM_MODELD_MIN_RELAXED_THRESHOLD = 8`** değerine (`> 7` MN-dışı koltuk [`2 Coin-Lock + 5 Açık PoW`]; bootstrap sonrasında veya aktif MN $\ge 8$ iken en az 1 Aktif Masternode katılımını zorunlu kılar), Model D öncesinde `ADAM_MIN_RELAXED_THRESHOLD = 5` değerine (`> 4` MN-dışı koltuk, bootstrap sonrası veya aktif MN $\ge 7$ iken), erken bootstrap aşamasında ise aktif MN Model D'de $< 8$ (Model D öncesinde $< 7$) iken `ADAM_MIN_RELAXED_THRESHOLD_BOOTSTRAP = 3` değerine kadar gevşetir.
* **Yedek Koordinatör Rotasyonu ($t \ge 45\text{ sn}$)**: Birincil Koordinatör `45 saniye` (`ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS = 45`) içinde blok yayınlamazsa, `GetAdamFallbackCoordinatorForSlot` blok önerme yetkisini 30 saniyelik yedek dilimler halinde PoBLS XOR mesafesine göre sıralanan (`RankValidatorsByPoBLSDistance`) `15` seçilmiş doğrulayıcı arasında (Model D öncesinde `11`) döndürür.

Seçilen bir madencinin çözümü eksikse ancak quorum eşiği karşılanmışsa, Koordinatör eksik madencinin çözümünü boş bir vektör yer tutucusu ile değiştirir:

$$
\text{vAdamSolutions}[i] = \text{std::vector}{<}\text{unsigned char}{>}\text{()}
$$

Bu, blok başlığı içindeki madenciler ile çözümler arasındaki 1:1 konumsal hizalamayı korur. Boş yer tutucular, blok doğrulama sırasında başarısız/geçersiz sayılır, quorum doğrulama sayımına dahil edilmez ve Aktif Doğrulayıcı ödülünden (`UPGRADE_FAIRNESS` altında blok `200` itibarıyla Model D öncesinde `%75 / 11 ≈ %6,818`, Model D sonrasında **`%60 / 15 = %4,00`** eşit koltuk payı [Dönem 0'da `15/15` çözümlemede `0` satoshi artık ile koltuk başına `0,6324 KRISTA`]; kalan satoshiler yalnızca Model D'de `15/15`, öncesinde `11/11` çözüldüğünde verilir) **%0** alır (sahipsiz paylar **%50 Otomatik Protokol Yakımına** ve **%50 Geliştirici Hazinesine** bölüştürülür).

## Geriye Dönük Uyumluluk
ADAM, blok yüksekliğine bağlı olarak koşullu şekilde etkinleştirilir:
* Testnet/Regtest/Mainnet üzerinde `200`. blok yüksekliğinde aktifleşir.
* Ağın başlangıç (bootstrap) aşamasında, masternode sayısının yetersiz olduğu durumlarda Fallback Mode (Sürüm 11) çalışır.
* Tam masternode seçim döngüsünü zorunlu kılan Standart Mod (Sürüm 12), `Consensus::UPGRADE_POMBL` ile deterministik olarak Mainnet'te `4800`, Testnet'te `400`, Regtest'te `300` blok yüksekliklerinde devreye girerken; Dengeli Model D (`Consensus::UPGRADE_MODELD`) komiteyi Mainnet'te `5.000`, Testnet'te `500`, Regtest'te `200` blok yüksekliğinde `15` koltuğa (`8 + 2 + 5`) genişletir.

## Referans Uygulama
* Konsensüs parametrelerinin yapılandırılması: `src/consensus/params.h` ve `src/chainparams.cpp`.
* Lider seçimi ve bulmaca seçici uygulaması: `src/adam.cpp`.
* Doğrulama mantığı: `src/main.cpp` içindeki `CheckBlock()` ve `src/primitives/block.cpp` içindeki `CBlockHeader::GetHash()`.
