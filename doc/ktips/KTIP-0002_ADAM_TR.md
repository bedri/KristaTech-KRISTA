```
KTIP: 0002
Başlık: ADAM İşbirlikçi Konsensüsü (ADAM Cooperative Consensus)
Yazar: KristaTech Core Developers
Durum: Active
Tür: Standard Track (Consensus)
Oluşturulma Tarihi: 2026-06-16
```

## Özet
Bu öneri, **ADAM (A Decentralized Approach Model)** işbirlikçi konsensüs mekanizmasını tanıtmaktadır. ADAM, blok doğrulama ve oluşturma süreçlerini üç ayrı işbirlikçi aşamaya böler: lider seçimi (leader election), hafif bulmaca çözme (lightweight puzzle solving) ve blok imza birleştirme (block signature aggregation). Her blok için seçilen birden fazla düğümün (11 madenci ve 1 koordinatör) işbirlikçi çalışmasını zorunlu kılarak madencilik havuzu merkezileşmesini, bencil madenciliği ve blok liderlerine yönelik hedefli DoS saldırılarını önler.

## Motivasyon
Geleneksel Proof-of-Work (PoW) protokolleri, birkaç büyük oluşumun blok üretimine hakim olduğu madencilik havuzu merkezileşmesinden muzdariptir. Ayrıca, bencil madencilik (selfish mining) saldırılarına ve kimlikleri önceden öngörülebilen blok üreticilerine yönelik hedefli Denial-of-Service (DoS) saldırılarına karşı savunmasızdırlar. Standart Proof-of-Stake (PoS) protokolleri ise "nothing-at-stake" (risksiz stake etme) çatallanmalarından etkilenir.

ADAM, bir grup onaylayıcı düğümü birlikte çalışmaya zorlayarak bu güvenlik açıklarını çözer:
* Blok üreticisinin (Koordinatör) kimliği, blok üretilene kadar gizli ve öngörülemez kalır.
* Blok hash değeri, seçilen birden fazla bağımsız madencinin sıralı kriptografik çalışmasına bağlı olduğundan bencil madenciliği etkisiz hale getirir.
* Masternode'lar temel seçim havuzunu oluşturarak Sybil saldırılarını engeller.

## Teknik Özellikler

### 1. Deterministik Lider Seçimi (SSLE)
Her $H$ blok yüksekliğinde, aktif onaylayıcı havuzu (masternode'lar ve kayıtlı madenciler) sorgulanır. Ağ, hareketli bir rastgele tohuma (rolling seed) dayalı olarak $N = 11$ Madenci ve $1$ Koordinatör seçmek için bir Tekli Gizli Lider Seçimi (SSLE) protokolü kullanır:

$$\text{Seed}_H = \text{Hash}\left(\text{Seed}_{H-1} \mathbin{\Vert} \text{VRFProof}_{H-1}\right)$$

Burada $\text{VRFProof}_{H-1}$, $H-1$ blokunun Koordinatörü tarafından oluşturulan belirlenimci hibrit imzadır.

Aktif havuzdaki her $i$ düğümü sıralanır:

$$\text{Rank}_i = \text{Hash}\left(\text{Seed}_H \mathbin{\Vert} \text{PubKey}_i\right)$$

Havuz, rank değerlerine göre artan düzende sıralanır. İlk $N$ düğüm Madenci (Miners), bir sonraki düğüm ise Koordinatör (Coordinator) olarak seçilir.

---

### 2. Hafif PoW Bulmaca Çözümü
Seçilen madenciler, hafif bir kriptografik bulmacayı çözerek iş yaptıklarını kanıtlamalıdır. Hedef zorluk limiti, bir bit kaydırma (bit-shift) çarpanı kullanılarak gevşetilir:

$$\text{scaledTarget} = \text{Target} \ll \text{activeShift}$$

Kaydırma değeri blok yüksekliğine bağlıdır:
* Blok yüksekliği $< 705$ ise $\text{activeShift} = 10$ (veya $12$).
* Blok yüksekliği $\ge 705$ ise $\text{activeShift} = 6$.

Madenciler aşağıdaki hedef denklemi çözer:

$$\text{PuzzleHash}_i \le \text{scaledTarget}$$

Burada $\text{PuzzleHash}_i$, enerji tasarruflu 18 hash fonksiyonunun X11/X16R benzeri 3-permütasyon seçici şemasıyla hesaplanır:

$$\text{PuzzleHash}_i = \text{algo1}\left( (i + 1) \times \text{algo2}\left( (i + 1) \times \text{algo3}(\text{Challenge}) \right) \right) \pmod{2^{256}}$$

Mücadele (Challenge) verisi ise şu şekilde tanımlanır:

$$\text{Challenge} = \text{Seed}_H \mathbin{\Vert} \text{MinerPubKey}_i \mathbin{\Vert} \text{Nonce}_i$$

---

### 3. Blok Hashing Zinciri (Durumsuz Hashing)
Blok başlığını, seçilen tüm madencilerin işlerine kriptografik olarak bağlamak için, blok hash'i ($H_{\text{block}}$), seçilen tüm madencilerin hashing işlemlerini sıralı olarak zincirleyerek hesaplanır.

Serileştirilmiş bir blok başlığı $S$ için:
1. Her $i \in \{0, \dots, M-1\}$ turu için (burada $M$ madenci sayısıdır), hash entropisini korumak için tek bir aralarında asal çarpan $m_i$ hesaplarız:

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

### 4. Quorum Dirençli Yanıt Verme
Çevrimdışı veya yavaş madencilerin blok üretimini dondurmasını önlemek amacıyla, minimum bir quorum eşiği ($T$) kadar çözüm alındığı sürece blok şablonları oluşturulabilir ve ağa yayılabilir.
* **Mainnet / Regtest**: $11$ madenciden en az $T = 7$'si.
* **Testnet**: $11$ madenciden en az $T = 3$'ü.

Seçilen bir madencinin çözümü eksikse ancak quorum eşiği karşılanmışsa, Koordinatör eksik madencinin çözümünü boş bir vektör yer tutucusu ile değiştirir:

$$\text{vAdamSolutions}[i] = \text{std::vector<unsigned char>()}$$

Bu, blok başlığı içindeki madenciler ile çözümler arasındaki 1:1 konumsal hizalamayı korur. Boş yer tutucular, blok doğrulama sırasında başarısız/geçersiz sayılır ve quorum doğrulama sayımına dahil edilmez.

## Geriye Dönük Uyumluluk
ADAM, blok yüksekliğine bağlı olarak koşullu şekilde etkinleştirilir:
* Testnet/Regtest/Mainnet üzerinde `200`. blok yüksekliğinde aktifleşir.
* Ağın başlangıç (bootstrap) aşamasında, masternode sayısının yetersiz olduğu durumlarda Fallback Mode (Sürüm 11) çalışır.
* Tam masternode seçim döngüsünü zorunlu kılan Standart Mod (Sürüm 12), Mainnet'te `2000`, Testnet'te `400`, Regtest'te `300` blok yüksekliklerinde veya `SPORK_21_ADAM_STANDARD_MODE` (`10020`) spork'u aktif olduğunda devreye girer.

## Referans Uygulama
* Konsensüs parametrelerinin yapılandırılması: `src/consensus/params.h` ve `src/chainparams.cpp`.
* Lider seçimi ve bulmaca seçici uygulaması: `src/adam.cpp`.
* Doğrulama mantığı: `src/main.cpp` içindeki `CheckBlock()` ve `src/primitives/block.cpp` içindeki `CBlockHeader::GetHash()`.
