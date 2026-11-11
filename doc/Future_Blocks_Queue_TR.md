# Gelecek Blok Kuyruğu (Future Blocks Queue)

Bu doküman, KRISTA blockchain ağında saat sapmalarından (clock drift) kaynaklanan blok ilerleme kilitlenmelerini (deadlock) önlemek amacıyla geliştirilen **Gelecek Blok Kuyruğu (Future Blocks Queue)** mekanizmasını açıklamaktadır.

---

## 1. Arka Plan ve Problem Tanımı

KRISTA, blok zamanlaması için **Time Protocol V2** standardını kullanmaktadır. Bu protokolde:
- Blok üretimi 15 saniyelik sabit zaman slotlarına yuvarlanır.
- Bir bloğun ağdaki diğer düğümler tarafından kabul edilebilmesi için zaman damgasının (timestamp), doğrulayan düğümün kendi yerel saati + 14 saniyeden (`nTimeSlotLength - 1`) daha ileri bir tarihte olmaması gerekir.
- Eğer bir blok veya blok başlığı bu sınırın üzerindeyse, doğrulama sırasında `time-too-new` hatasıyla reddedilir.

### Kilitlenme (Deadlock) Senaryosu
Düğümler arasında 15 saniyeden uzun süreli doğal saat sapmaları oluştuğunda, saati geride olan bir düğüm, saati ileride olan bir koordinatörün ürettiği geçerli bir bloğu geçici olarak `time-too-new` gerekçesiyle reddeder. 

Bitcoin/PIVX P2P protokol standartlarına göre, bir düğüm bir blok başlığını bir kez reddettiğinde, gönderici akran (peer) bu başlığı tekrar göndermez. Düğüm de eksik bloğu otomatik olarak yeniden talep etmez. Eğer bir sonraki blok için koordinatör seçilen düğüm bu şekilde kilitlenip geride kalırsa, yeni blok üretemez ve ağ kalıcı olarak kilitlenir.

---

## 2. Mimari Çözüm: Gelecek Blok Kuyruğu

Ağın saat senkronizasyonuna bağımlı kalmadan kendi kendini onarabilmesi (self-healing) için, doğrulama sırasında `time-too-new` alan bloklar tamamen reddedilmek yerine bellek içi geçici bir kuyruğa alınır.

### Bileşenler

1. **CFutureBlock Yapısı ve mapFutureBlocks:**
   Reddedilen blok veya blok başlıkları, bloğu gönderen akranın adres bilgisiyle birlikte `mapFutureBlocks` haritasında saklanır:
   ```cpp
   struct CFutureBlock {
       CBlock block;
       std::string peerAddr;
   };
   std::map<uint256, CFutureBlock> mapFutureBlocks;
   ```

2. **HEADERS ve BLOCK Yapısal Ön-Doğrulaması (`SEC-04` / `IsFutureBlockPreVerified`):**
   Gelen bir blok veya blok başlığı `"time-too-new"` hatası aldığında ve zaman damgası `GetAdjustedTime() + 300` saniye (5 dakika — hem `HEADERS` hem de `BLOCK` mesajlarında birebir aynı tavan uygulanır) içindeyse akran cezalandırılmaz veya bağlantısı kesilmez. `mapFutureBlocks` haritasına eklenmeden önce `ProcessMessage`, `mapBlockIndex` içinde bilinen bir ebeveyn blok (`nCandidateHeight = pindexPrev->nHeight + 1` türetimi için) arar ve bozuk veya kimliksiz spam başlıklarla kuyruk doldurulmasını önlemek üzere `IsFutureBlockPreVerified(header, nCandidateHeight)` (`src/main.cpp`) yapısal sınırlarını uygular:
   - `block.nVersion >= 11` için: boş olmayan ve her biri en fazla `512` bayt boyutunda olan `vAdamCoordinatorSig` ve `vAdamVRFProof` alanları.
   - `block.nVersion >= 12` için: en fazla `4096` bayt boyutunda `vQuorumSig` alanı ve `IsModelDActive(nCandidateHeight)` aktifken boş olmama zorunluluğu.
   - `mapFutureBlocks` kuyruğuna kabul edilen tam `BLOCK` mesajlarında `pfrom->AddInventoryKnown(inv)` kaydı kuyruğa alındığı anda işlenir (ön-doğrulamadan geçemeyen veya kapasite sınırı nedeniyle düşürülen bloklarda ise sonradan yeniden istenebilmesi için atlanır). Tam kriptografik BLS/ECDSA/VRF/LLMQ ve bağlamsal blok doğrulaması, `ProcessFutureBlocks` tarafından `ProcessNewBlock` çağrıldığında yürütülür.

3. **Sınırlı Kapasite ve En Uzak Zaman Damgalı Tahliye (`64` Kayıt):**
   `mapFutureBlocks` kapasitesi kesin olarak **64 kayıt** (`mapFutureBlocks.size() >= 64`) ile sınırlandırılmıştır. Kuyruk doluysa, yeni gelen bloğun zaman damgası daha yakınsa kuyruktaki en uzak zaman damgalı (`max_element`) kayıt tahliye edilir; aksi halde gelen blok düşürülür.

4. **ProcessFutureBlocks Döngüsü:**
   Periyodik mesaj işleme döngüsünde (`ProcessFutureBlocks`), kuyrukta bekleyen bloklar kontrol edilir. Yerel saat ilerledikçe zaman damgası yasal sınıra giren bloklar (`block.nTime <= MaxFutureBlockTime()`) kuyruktan çıkarılarak işlenir (yalnızca başlık ise `GETDATA` talep edilir, tam blok ise `ProcessNewBlock` ile zincire eklenir).

5. **Eski Kayıt Temizliği:**
   `+300 sn` kabul tavanı ve `64` kayıtlık kapasite sınırına ek olarak, ebeveyn bloğu `mapBlockIndex` içinde bulunmayan ve blok zaman damgası 1 saatten fazla geçmişte kalan (`GetAdjustedTime() - block.nTime > 3600`) kayıtlar otomatik olarak temizlenir.

---

## 3. Avantajlar

- **Konsensüs Uyumlu:** Blok kabul zamanı konsensüs sınırları içinde kalmaya devam eder, kurallar esnetilmez.
- **Dış Bağımsızlık:** NTP veya saat senkronizasyon servislerinin çalışmadığı veya düzgün yapılandırılmadığı düğümlerde dahi ağın kilitlenmesini engeller.
- **Ağ Dostu:** Gereksiz DoS cezalarını ve düğüm kopmalarını önler.
