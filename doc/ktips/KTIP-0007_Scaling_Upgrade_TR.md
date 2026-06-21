```
KTIP: 0007
Başlık: Protokol Ölçeklendirme Güncellemesi (8 MB Blok Limiti, 30sn Blok Süresi, 15sn Staking Zaman Dilimi ve BIP152 Compact Blocks)
Yazar: KristaTech Çekirdek Geliştiricileri
Durum: Aktif
Tür: Standart Takip (Konsensüs)
Oluşturulma Tarihi: 2026-06-19
```

## Özet
Bu teklif, KRISTA ağının işlem hacmini ve blok yayılım verimliliğini artıran **Protokol Ölçeklendirme Güncellemesi**'ni açıklamaktadır. Bu güncelleme, maksimum blok boyutu sınırını 8 MB'a çıkarır, hedef blok süresini 30 saniye olarak belirler, PoS staking zaman dilimini (time slot) 15 saniyeye düşürür ve ağ bant genişliği kullanımını ile blok yayılım gecikmesini en aza indirmek için BIP152 Compact Blocks protokolünü (özel olarak eşlenmiş envanter türü `MSG_CMPCT_BLOCK = 20` ile) ağa entegre eder.

## Motivasyon
KRISTA ağı olgunlaştıkça, kurumsal işlemleri, yüksek akıllı sözleşme (MESCAL) yoğunluğunu ve varlık tokenizasyonu kullanım durumlarını desteklemek için Katman 1 işlem hacminin ölçeklendirilmesi gerekmektedir.
Ancak blok boyutlarını doğrudan artırmak şu sorunlara yol açabilir:
1. **Ağ Yayılım Gecikmesi (Network Propagation Latency)**: Daha büyük blok verileri P2P ağı üzerinde yavaş yayılır, bu da yüksek yetim blok (orphan block) oranlarına ve ağ bölünmelerine (fork splits) neden olur.
2. **Bant Genişliği İsrafı**: Düğümler, yerel bellek havuzlarında (mempool) zaten bulunan işlemleri blok yayılımı sırasında tekrar tekrar indirirler.
3. **Veritabanı Yazma Duraklamaları (Database Write Stalls)**: Büyük blok boyutlarında aşırı hızlı blok süreleri (örn. 10 saniyenin altında), LevelDB veritabanı birleştirmeleri (compactions) ve durum temizlemeleri (state flushes) için yeterli zaman sağlamaz ve donanım düzeyinde yazma duraklamalarına neden olur.

Bu sorunları çözmek için dengeli bir yaklaşım uyguluyoruz:
- Büyük işlem partilerini desteklemek için blok boyutu sınırını **8 MB**'a ölçeklendiriyoruz.
- Düğümlere doğrulama, ağ yönlendirmesi ve SSD birleştirme/temizleme işlemleri için yeterli zaman tanımak amacıyla hedef blok süresini **30 saniye**, PoS zaman dilimlerini ise **15 saniye** olarak belirliyoruz.
- Blok yayılım bant genişliğinden %99'a kadar tasarruf sağlayacak şekilde, tam blok verisi yerine blok başlıklarını ve 6 baytlık kısa işlem kimliklerini (short IDs) ileten ve blokları anında yeniden oluşturmak için yerel bellek havuzlarını kullanan **BIP152 Compact Blocks** yapısını entegre ediyoruz.

## Teknik Özellikler (Specification)

### 1. Blok Boyutu ve Mesaj Sınırları
* **Maksimum Blok Boyutu (`MAX_BLOCK_SIZE_CURRENT`)**: `src/consensus/consensus.h` dosyasında 2 MB'tan (`2.000.000` bayt) **8 MB**'a (`8.000.000` bayt) çıkarılmıştır.
* **Maksimum Protokol Mesaj Uzunluğu (`MAX_PROTOCOL_MESSAGE_LENGTH`)**: Yeniden oluşturma başarısız olduğunda tam blokların iletimine izin vermek ve mesaj boyutu sınırı nedeniyle düğüm bağlantılarının kesilmesini önlemek için `src/net.h` dosyasında 2 MB'tan **10 MB**'a (`10 * 1024 * 1024` bayt) çıkarılmıştır.
* **Varsayılan Maksimum Blok Oluşturma Boyutu (`DEFAULT_BLOCK_MAX_SIZE`)**: Madencilerin varsayılan olarak 6 MB'a kadar bloklar oluşturabilmesini sağlamak amacıyla `src/policy/policy.h` dosyasında 750 KB'tan **6 MB**'a (`6.000.000` bayt) çıkarılmıştır.

### 2. Süreler ve Zaman Dilimleri
* **Hedef Blok Süresi (`nTargetSpacing`)**: `src/chainparams.cpp` dosyasında hem Mainnet hem de Testnet için **30 saniye** olarak yapılandırılmıştır.
* **Staking Zaman Dilimi Uzunluğu (`nTimeSlotLength`)**: `src/chainparams.cpp` dosyasında hem Mainnet hem de Testnet için **15 saniye** olarak yapılandırılmıştır.
* **Azalma Hızı Değişimi (Decay Speed Shift)**: Ödül azalma aralığı `259.200` blokta sabit kalmıştır. 30 saniyelik blok süresiyle, azalma periyodu takvim zamanı olarak **~90 gün** (üç aylık) düzeyinde kalmakta ve hedeflenen tokenomik yapısını korumaktadır.

### 3. BIP152 Compact Blocks Protokol Entegrasyonu
Bant genişliğini optimize etmek amacıyla ağ, BIP152 "Compact Blocks" (Kısa Kimlik ile blok yeniden oluşturma) protokolünü uygular.

#### A. Protokol Komut Sabitleri
* **Envanter Mesaj Türü (Inventory Message Type)**: Dash'e özgü eski komutlarla (örneğin InstantSend `MSG_TXLOCK_REQUEST = 4`) çakışmayı önlemek için `src/protocol.h` dosyasında `MSG_CMPCT_BLOCK` envanter türü **`20`** olarak eşlenmiştir.
* **Mesaj Komutları**:
  - `sendcmpct`: Peers arasında compact block yeteneğini ve yüksek/düşük bant genişliği modlarını müzakere eder.
  - `cmpctblock`: Blok başlığını, 32 bitlik bir nonce değerini, önceden doldurulmuş işlemler için diferansiyel indeksleri, kısa işlem kimliklerini ve (eğer PoS ise) PoS blok imzasını içeren serileştirilmiş bir `CBlockHeaderAndShortTxIDs` yapısını iletir.
  - `getblocktxn`: Blok indekslerine göre eksik olan işlemleri talep eder.
  - `blocktxn`: Talep edilen işlemleri sunar.

#### B. Kısa İşlem Kimliği (Short Transaction ID) Türetilmesi
Kısa kimlikler, SipHash-2-4 algoritması ile türetilen 6 baytlık (48 bit) hash değerleridir.
SipHash anahtarları ($k_0, k_1$), serileştirilmiş blok başlığının bağlantıya özel 64 bitlik bir nonce değeriyle birleştirilerek hash edilmesiyle elde edilir:
1. Blok başlığını ve bağlantı nonce değerini serileştirin.
2. SHA256 hash değerini hesaplayın.
3. Hash değerinin ilk 8 baytını $k_0$, sonraki 8 baytını ise $k_1$ olarak kullanın.
4. Her işlem ($TX$) için `SipHash-2-4(txhash, k0, k1)` değerini hesaplayın ve `0x0000ffffffffffff` ile maskeleyin.

#### C. Yeniden Oluşturma (Reconstruction) ve Geri Çekilme (Fallback)
Bir `cmpctblock` mesajı alındığında:
1. Önceden doldurulmuş işlemler (Coinbase/Coinstake gibi) doğrudan çözülür.
2. Geriye kalan kısa kimlikler, yerel bellek havuzunda (`mempool`) bulunan işlem hash'leriyle eşleştirilir.
3. Tüm işlemler başarıyla eşleştirilirse, blok anında yeniden oluşturulur ve `ProcessNewBlock` işlevine aktarılır.
4. Herhangi bir işlem eksikse, düğüm kısmen indirilmiş bloğu `mapPartiallyDownloadedBlocks` içinde saklar, eksik indeksler için peer'a bir `getblocktxn` isteği gönderir ve `blocktxn` mesajını aldığında bloğu tamamlar.

## Geriye Dönük Uyumluluk
* Compact block yayılımı düğümler arasında `sendcmpct` aracılığıyla dinamik olarak müzakere edilir. BIP152 protokolünü desteklemeyen düğümler, blokları geleneksel `block` mesajlarıyla almaya devam eder.
* 8 MB blok boyutu sınırı, 30sn hedef süresi ve 15sn staking zaman dilimi parametreleri konsensüs açısından kritik olup, tüm ağ düğümlerinin güncellenmiş yazılımı çalıştırmasını gerektirir.

## Referans Kod Konumları
* Konsensüs parametre ayarları: `src/consensus/consensus.h`, `src/net.h`, `src/policy/policy.h`, `src/chainparams.cpp`.
* Compact Block veri yapıları: `src/blockencodings.h`, `src/blockencodings.cpp`.
* P2P mesaj işleme mantığı: `src/main.cpp` içindeki `ProcessMessage` ve `SendMessages`.
* Birim testleri: `src/test/blockencodings_tests.cpp`.
