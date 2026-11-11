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

2. **HEADERS ve BLOCK Kriptografik Ön-Doğrulaması (`SEC-04` / `R42-F1` / `IsFutureBlockPreVerified`):**
   Gelen bir blok veya blok başlığı `"time-too-new"` hatası aldığında ve zaman damgası `GetAdjustedTime() + 300` saniye (5 dakika — hem `HEADERS` hem de `BLOCK` mesajlarında birebir aynı tavan uygulanır) içindeyse akran cezalandırılmaz veya bağlantısı kesilmez. `mapFutureBlocks` haritasına eklenmeden önce `ProcessMessage`, `mapBlockIndex` içinde bilinen bir ebeveyn `pindexPrev` zorunlu kılar (ebeveynsiz yetim gelecek blok/başlıklar kuyruğa alınmaz) ve kimliksiz spam ile kuyruk boşaltılmasını önlemek üzere `IsFutureBlockPreVerified(header, pindexPrev)` (`src/main.cpp`) üzerinden tam kriptografik ön-doğrulama çalıştırır:
   - `header.GetBlockTime() > pindexPrev->MinPastBlockTime()` koşulunu zorunlu tutar.
   - `block.nVersion >= 11` için: `vAdamCoordinatorSig` ve `vAdamVRFProof` alanlarının `1..512` bayt aralığında olmasını, `GetAdamSeed(pindexPrev)` ve `header.GetHash()` üzerinde `VerifySelfContainedBLSSignedData` ile `vrfBlsPk == coordBlsPk` eşleşmesini ve `GetAdamMinerPool(nCandidateHeight - 1, ...)` içindeki yetkili bir doğrulayıcıya karşı secp256k1 sahiplik doğrulamasını (`VerifyAdamVRFProof` + `VerifyAdamCoordinatorSig`) denetler.
   - `block.nVersion >= 12` için: `vQuorumSig <= 4096` bayt sınırını ve `IsModelDActive(nCandidateHeight)` aktifken `llmq::CQuorumSignature` (`qsig.blockHash == header.GetHash()`) ile aktif LLMQ kurulu üzerinde 5'te 3 `qsig.Verify(llmq::GetActiveQuorum(...))` doğrulamasını zorunlu tutar.
   - Yalnızca başlık içeren `HEADERS` mesajları kuyrukta bekleyen tam bir `CBlock` (`!vtx.empty()`) kaydının üzerine asla yazmaz; gelen tam bir `BLOCK` mesajı ise mevcut başlık kaydını yerinde tam bloğa yükseltir. Kuyruktaki tam bloklar için `AlreadyHave(MSG_BLOCK)` `true` dönerken, `pfrom->AddInventoryKnown(inv)` çağrısı `ProcessFutureBlocks` içinde `ProcessNewBlock` çalıştırılana kadar ertelenir; böylece süresi gelmeden tahliye edilen bir blok eşler tarafından yeniden duyurulabilir.

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
