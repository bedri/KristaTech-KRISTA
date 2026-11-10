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

2. **HEADERS ve BLOCK Kriptografik Ön-Doğrulaması (`SEC-04` / `IsFutureBlockPreVerified`):**
   Gelen bir blok veya blok başlığı `"time-too-new"` hatası aldığında ve zaman damgası `GetAdjustedTime() + 300` saniye (5 dakika — hem `HEADERS` hem de `BLOCK` mesajlarında birebir aynı tavan uygulanır) içindeyse akran cezalandırılmaz veya bağlantısı kesilmez; ayrıca reddedilen veya tahliye edilen bir bloğun sonradan tekrar istenebilmesi için `pfrom->AddInventoryKnown` kaydı `ProcessFutureBlocks` aşamasına ertelenir. Kuyruk doldurma (queue stuffing) saldırılarını önlemek amacıyla `mapFutureBlocks` haritasına eklenmeden önce `IsFutureBlockPreVerified` gerçek kriptografik imza ve aday havuzu yetki doğrulaması (`SEC-04`) uygular:
   - Bilinen ebeveyn blok `pindexPrev` (`mapBlockIndex`) ve `block.nTime > pindexPrev->GetMedianTimePast()`.
   - `block.nVersion >= 11` için: `vAdamMiners` / `vAdamSolutions` boyut sınırları, hem `vAdamVRFProof` (`GetAdamSeed(pindexPrev)` üzerinde) hem de `vAdamCoordinatorSig` (`block.GetHash()` üzerinde) için `VerifySelfContainedBLSSignedData` ile gerçek BLS+ECDSA imza doğrulaması, tutarlı `coordPubKey` ve `vrfBlsPk == coordBlsPk` eşleşmesi ve ebeveyn blok aktif uçla eşleştiğinde `coordPubKey` anahtarının zincir üstü `GetAdamMinerPool(nHeight - 1, pcoinsTip)` aday havuzunda yer alması.
   - `block.nVersion >= 12` (`UPGRADE_MODELD` aktifken) için: ebeveyn aktif uçla eşleştiğinde `llmq::GetActiveQuorum(nHeight, pindexPrev)` üzerinde tam `CQuorumSignature::Verify` doğrulaması (yan dallarda ise geçerli boyutlu `CQuorumSignature` ve `qsig.blockHash == blockHash` kontrolü).

3. **Sınırlı Kapasite ve En Uzak Zaman Damgalı Tahliye (`MAX_FUTURE_BLOCKS = 64`):**
   `mapFutureBlocks` kapasitesi kesin olarak **64 kayıt** (`MAX_FUTURE_BLOCKS = 64`) ile sınırlandırılmıştır. Kuyruk doluysa (`mapFutureBlocks.size() >= 64`), yeni gelen bloğun zaman damgası daha yakınsa kuyruktaki en uzak zaman damgalı (`max_element`) kayıt tahliye edilir; aksi halde gelen blok reddedilir.

4. **ProcessFutureBlocks Döngüsü:**
   Periyodik mesaj işleme döngüsünde (`ProcessFutureBlocks`), kuyrukta bekleyen bloklar kontrol edilir. Yerel saat ilerledikçe zaman damgası yasal sınıra giren bloklar (`block.nTime <= MaxFutureBlockTime()`) kuyruktan çıkarılarak işlenir (yalnızca başlık ise `GETDATA` talep edilir, tam blok ise `ProcessNewBlock` ile zincire eklenir).

5. **Eski Kayıt Temizliği:**
   `+300 sn` kabul tavanı ve `MAX_FUTURE_BLOCKS = 64` kapasite sınırına ek olarak, 1 saatten uzun süre kuyrukta kalan yetim blok kayıtları otomatik olarak temizlenir.

---

## 3. Avantajlar

- **Konsensüs Uyumlu:** Blok kabul zamanı konsensüs sınırları içinde kalmaya devam eder, kurallar esnetilmez.
- **Dış Bağımsızlık:** NTP veya saat senkronizasyon servislerinin çalışmadığı veya düzgün yapılandırılmadığı düğümlerde dahi ağın kilitlenmesini engeller.
- **Ağ Dostu:** Gereksiz DoS cezalarını ve düğüm kopmalarını önler.
