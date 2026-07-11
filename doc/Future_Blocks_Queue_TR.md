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

2. **HEADERS Mesaj İşleyicisi Değişikliği:**
   Gelen blok başlığı doğrulanamadığında, eğer reddetme nedeni `"time-too-new"` ise akran cezalandırılmaz veya bağlantı kesilmez; başlık kuyruğa eklenir.

3. **BLOCK Mesaj İşleyicisi Değişikliği:**
   Gelen tam blok doğrulanamadığında, nedeni `"time-too-new"` ise blok verisi kuyruğa eklenir.

4. **ProcessFutureBlocks Döngüsü:**
   Mesaj işleme döngüsünün (`ProcessMessages`) her adımında, kuyrukta bekleyen bloklar kontrol edilir. Yerel saat ilerledikçe zaman damgası yasal sınıra giren bloklar (`block.nTime <= MaxFutureBlockTime()`) kuyruktan çıkarılarak normal doğrulama sürecine alınır.
   - Eğer kuyruktan çıkarılan veri sadece bir blok başlığı ise (header), doğrulama sonrası akrandan tam blok verisi (`GETDATA`) talep edilir.
   - Eğer tam blok ise, doğrudan `ProcessNewBlock` ile zincire eklenir.

5. **Bellek Sızıntısı Koruması:**
   Farklı bir çataldan (fork) gelen veya hiçbir zaman geçerli olmayacak yetim blokların belleği doldurmasını önlemek için, 1 saatten daha uzun süre kuyrukta bekleyen bloklar otomatik olarak temizlenir.

---

## 3. Avantajlar

- **Konsensüs Uyumlu:** Blok kabul zamanı konsensüs sınırları içinde kalmaya devam eder, kurallar esnetilmez.
- **Dış Bağımsızlık:** NTP veya saat senkronizasyon servislerinin çalışmadığı veya düzgün yapılandırılmadığı düğümlerde dahi ağın kilitlenmesini engeller.
- **Ağ Dostu:** Gereksiz DoS cezalarını ve düğüm kopmalarını önler.
