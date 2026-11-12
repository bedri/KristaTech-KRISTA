# KRISTA ve DASH: Mimari ve Protokol Karşılaştırması

Bu doküman, **KristaTech (KRISTA)** blok zinciri protokolü ile **Dash Core** protokolü arasındaki ayrıntılı teknik karşılaştırmayı sunar. KRISTA, temel altyapı bileşenlerini Dash'ten miras almış olsa da; konsensüs, madencilik modeli, akıllı sözleşmeler ve hazine yönetimi alanlarında büyük tasarımsal yenilikler getirmiştir.

---

## Teknik Karşılaştırma Tablosu

| Özellik | Dash Core (v23.1.x) | KRISTA (Mevcut Durum) |
| :--- | :--- | :--- |
| **Konsensüs Motoru** | Proof of Work (X11) + LLMQ ChainLocks | ADAM (Cooperative Lightweight PoW) + Proof of Stake (MPA) |
| **Blok Üretim Modeli** | Rekabetçi Madencilik Yarışı (ASIC baskın) | İşbirlikçi Havuzlama ve Seçim (SSLE) |
| **Blok Süresi** | ~2.5 dakika | 30 saniye |
| **ASIC Direnci** | Yok (X11 ASIC'leri ağa hakimdir) | Mimari Düzeyde Tam Koruma (Permütasyonlu Hashing Zinciri) |
| **Masternode Teminatı** | **1000 DASH** (Evonode için 4000 DASH) | **4200 KRISTA** (Ağ genelinde sabit) |
| **Quorum İmza Tipi** | BLS Eşik İmzası (BLS Threshold Signature) | `secp256k1` MN kimliğine sabitlenmiş Birleştirilmiş BLS12-381 (`blst`) 5'te 3 Kurul İmzası (`CQuorumSignature`) |
| **Hazine Dağıtımı** | Blok ödülünün %20'si (Superblock Ödülleri) | **%7,0 `DeveloperFund`** (LLMQ 5'te 3 + Spork Çift İmzalı) |
| **Akıllı Sözleşmeler** | Yok (Yalnızca Platform katmanında veri şemaları) | **MESCAL** (JSON formatlı Güvenli Sözleşme Dili) |
| **Yönetişim Kilidi** | Yok (Oylama kazananı doğrudan harcar) | Enforce start height + Spork ve LLMQ Çift Yetkilendirmesi |
| **Ağ Güncellemeleri** | Sporklar + BIP9/BIP135 Hard Fork | Deterministik blok yüksekliği kapıları (`Consensus::UPGRADE_*`) + idari Sporklar |
| **Enerji Tüketimi** | Yüksek (Sürekli global rekabetçi PoW yarışı) | Çok Düşük (Sadece seçilen madenciler işlem yapar) |

---

## Detaylı Mimari Farklar

### 1. Konsensüs ve Blok Doğrulama (ADAM vs. X11 + ChainLocks)
* **Dash Core:** X11 hashing algoritmasını kullanır (11 farklı hash fonksiyonunun zincirlenmesi). Madencilik tamamen rekabetçidir: Blok başlığı hash'ini zorluk hedefinin altına düşüren ilk madenci blok ödülünü alır. Aktif Masternode Quorum'ları (LLMQ), blokları BLS eşik imzalarıyla kilitleyen **ChainLocks** teknolojisini sunar; bu durum %51 organizasyon saldırılarını önlese de temel katmandaki rekabetçi ve yoğun enerji tüketen yapıyı değiştirmez.
* **KRISTA:** Rekabetçi madencilik yerine **ADAM (A Decentralized Approach Model)** algoritmasını ve **Proof-of-Stake (PoS)** modelini entegre eder. Her blok yüksekliğinde, deterministik VRF/PoBLS seçimiyle zincir üstü aktif masternode ve kayıtlı doğrulayıcı havuzundan **11 Çift Kulvarlı komite doğrulayıcısı (`7 Aktif MN + 1 Coin-Lock + 3 Açık PoW`)** ve **1 PoBLS Koordinatörü** seçilir.
  * Seçilen madenciler hafif paralel bulmacaları çözer.
  * Nihai blok başlığı hash'i, seçilen tüm madencilerin işlem çıktılarının ardışık zincirlenmesiyle hesaplanır (Stateless Hashing Chain).
  * ADAM PoW blokları Koordinatör imzası (`vAdamCoordinatorSig`) ve 5'te 3 LLMQ Kurul İmzası (`UPGRADE_MODELD` sonrasında `vQuorumSig`) ile kilitlenirken, İşbirlikçi PoS blokları üçlü yetkilendirme (`vchBlockSig` + `vAdamCoordinatorSig` + `vQuorumSig`) taşır.

### 2. Madencilik Merkeziyetsizliği ve ASIC Direnci
* **Dash Core:** X11 hashing dizilimi statiktir. Dash'in ekonomik değeri yükseldikçe üreticiler özel ASIC çipleri geliştirmiş ve ağdaki hash gücü endüstriyel madencilik tesislerinde merkezileşmiştir.
* **KRISTA:** Madencilik için gereken hash algoritmaları dinamiktir. **ADAM Standart Modu (Sürüm 12)** altında, madenci bazında blok başına 3'lü permütasyon seçim şeması uygulanır:
  * 18 adet enerji dostu algoritma içinden, bir önceki bloğun hash'ine ve madencinin genel anahtarına göre deterministik olarak 3 farklı algoritma seçilir.
  * Bulmaca çözümü bu algoritmaları ardışık ve özyinelemeli olarak çalıştırır.
  * Hashing boru hattı her madenci ve her blok slotu için dinamik olarak değiştiğinden, statik ASIC mimarileri herhangi bir hız veya verimlilik avantajı elde edemez.

### 3. Masternode Teminatı ve Quorum'lar
* **Dash Core:** Standart masternodlar için **1000 DASH** teminat gerekir. Dash Platform ikinci katman veri ağını çalıştıran yüksek performanslı "Evonode"lar için ise **4000 DASH** gerekir.
* **KRISTA:** Masternode teminatı **4200 KRISTA** olarak belirlenmiştir. Ayrı düğüm katmanları yerine tüm masternodlar birleşik ikinci katmanı çalıştırarak blok konsensüs doğrulamasına, lider seçim havuzuna ve işlem yönlendirmesine katılır.

### 4. Geliştirici Hazinesi Yönetimi (Developer Treasury)
* **Dash Core:** Bütçe ödemeleri teklif oylamalarıyla belirlenir. Masternodlar teklifleri oylar ve onaylanan teklifler Superblock sırasında doğrudan alıcının adresine ödenir.
* **KRISTA:** Sistem düzeyinde güvenli bir **Developer Fund** adresi barındırır. Bu adresten harcama yapılması konsensüs katmanında (`ConnectBlock` ve `CheckInputs` içinde) sıkı bir şekilde denetlenir:
  * **Zaman Kilidi:** Harcamalar Mainnet'te 2880. bloktan (Testnet'te 200) önce yapılamaz.
  * **Çift İmza Yetkilendirmesi:** İşlem, aktif masternode quorum'unun harcamayı onayladığını doğrulayan bir LLMQ Quorum İmzası içermeli ve aynı zamanda ağın yönetimsel Spork Genel Anahtarı ile imzalanmış olmalıdır.

### 5. Akıllı Sözleşmeler (MESCAL)
* **Dash Core:** Dash Core temel katmanda akıllı sözleşmeleri desteklemez. Dash Platform merkeziyetsiz bir belge ve veri deposu sunsa da akıllı sözleşme mantığı yürütmez.
* **KRISTA:** **MESCAL** (Machine-parsable JSON representations of CScript) akıllı sözleşme motoruna sahiptir. Sözleşmeler, Bitcoin benzeri script komutlarını temsil eden JSON şablonları olarak yazılır. Bu sayede geliştiriciler doğrudan ana katman üzerinde kurumsal yönetişim, emanet (escrow), DeFi ve tokenizasyon için güvenli, tekrar giriş (re-entrancy) açıklarından arındırılmış ve durumsuz sözleşmeler oluşturabilir.
