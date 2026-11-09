# KRISTA Blokzinciri: Hard Cap & Ekonomik Güven Analiz Raporu (Revize Edilmiş Nihai Sürüm)

Bu rapor, KRISTA ağının yeni kararlaştırılan **210 Milyon KRISTA** maksimum arz limiti (Hard Cap), **%1.9 üç aylık (~90 günde bir) azalma (decay)** oranı, **4.200 KRISTA** sabit masternode teminatı ve güncellenen bootstrap modeli çerçevesinde revize edilmiştir. Bu yeni yapı, blok ödüllerinin ömrünü maksimum seviyede tutmayı ve ağın kıtlık algısını en güvenli ölçekte sürdürmeyi hedeflemektedir.

---

## 1. Yönetici Özeti

KRISTA blokzinciri için seçilen yeni tokenomics parametreleri, Litecoin (84M) ve Bitcoin (21M) gibi küresel standartların izinden giderek hem yüksek yatırımcı güveni sağlamakta hem de ödül süresini onlarca yıla yaymaktadır:
1. **Seçilen Hard Cap:** **210.000.000 (210 Milyon) KRISTA** (Bitcoin'in tam 10 katı).
2. **Uzatılmış Ödül Ömrü (1.9% Decay):** Üç aylık (~90 günde bir) azalma oranı %5'ten **%1.9** seviyesine düşürülmüştür. Bu sayede blok ödülleri çok daha yavaş azalmakta ve **blok ödülü süresi 50+ yıl boyunca etkin kalmaktadır**.
3. **Masternode Teminatı:** Arz ile orantılı olarak **4.200 KRISTA** flat olarak güncellenmiştir.
4. **Kademeli Bootstrap Güçlendirmesi:** Hem quorum kilitlenmelerini önlemek hem de erken dönem enflasyon balonunu (%72,8 oranında) engellemek amacıyla 10.000 blokluk bootstrap ödülü protokol fazlarıyla uyumlu olarak **40 -> 30 -> 20 KRISTA** kademeli yapısına geçirilmiştir.

---

## 2. Tokenomics Modeli Karşılaştırma Matrisi

Yeni tasarlanan 210M modelinin detayları aşağıda özetlenmiştir:

| Parametre / Metrik | Eski Model (100M Başlangıç) | Yeni Seçilen Model (210M - Uzun Ömürlü) |
| :--- | :---: | :---: |
| **Maksimum Arz (Hard Cap)** | 100.000.000 KRISTA | **210.000.000 KRISTA** |
| **Bootstrap Blok Ödülü** | 50 KRISTA | **40 -> 30 -> 20 KRISTA** (10k blok kademeli) |
| **Bootstrap Toplam Üretim** | 499.900 KRISTA | **271.940 KRISTA** (Ağın ~%0,13'ü) |
| **Başlangıç Blok Ödülü (Post-Bootstrap)** | 19,0 KRISTA | **15 KRISTA** |
| **Dönemlik Azalma (Decay) Oranı** | %5.00 | **%1.90 (Her ~90 günde bir)** |
| **Asimptotik Sınır (Fiili Arz)** | 98.995.900 KRISTA | **204.903.519 KRISTA** |
| **Hard Cap'e Oranı (%)** | %99,00 | **%97,57** |
| **Yedek/Boşluk Rezervi** | 1.004.100 KRISTA | **5.096.481 KRISTA** |
| **Masternode Teminatı** | 1.000 KRISTA | **4.200 KRISTA** |
| **%90 Doyuma Ulaşma Süresi** | 11,5 Yıl | **32,5 Yıl** |
| **%95 Doyuma Ulaşma Süresi** | 15,5 Yıl | **45,5 Yıl** |

> [!TIP]
> **Yedek Rezerv Avantajı:** 204.90M asimptot limiti ile 210M hard cap arasındaki **5,10 Milyon KRISTA**'lık fark, blok ödüllerinin teorik olarak hiçbir zaman sert bir şekilde kesilmeyeceğini ve ağın işlem ücretlerine (fees) yumuşak bir şekilde adapte olacağını garanti eder.

---

## 3. Uzun Vadeli Zaman Doyumu ve Arz Simülasyonu

Yeni 210M parametrelerine göre ağın 100 yıllık dönemdeki arz ve ödül dağılımı:

* **0. Yıl (İlk 10.000 Blok / Kademeli Bootstrap):**
  - Blok Ödülü: **Blok 2–2.200: 40 KRISTA | Blok 2.201–5.000: 30 KRISTA | Blok 5.001–9.999: 20 KRISTA**
  - Dolaşımdaki Arz: **271.940 KRISTA** *(Genesis 15 KRISTA ile 271.955 KRISTA)*
  - *Masternode Etkisi:* Blok 2.200'de (Model D aktivasyonunda) dolaşımda 87.975 KRISTA (net ~81.187 KRISTA) bulunur ve 4.200 KRISTA teminat gereksinimi altında **19 aktif masternode**'u (7 MN komite ve 5 MN LLMQ kvorum eşiğinin ~3 katını), Blok 10.000 sonunda ise **59 masternode** kapasitesini destekler. Quorum kilitlenmeleri ve erken enflasyon baskısı birlikte engellenmiştir.
* **1. Yıl Sonu (Blok 1.046.800):**
  - Blok Ödülü (Dönem 3): **14.16 KRISTA** (yavaş sönümlenme)
  - Dolaşımdaki Arz: **18.987.122 KRISTA** (Hard Cap'in %9,04'ü)
* **2. Yıl Sonu (Blok 2.083.600):**
  - Blok Ödülü (Dönem 7): **13.12 KRISTA**
  - Dolaşımdaki Arz: **32.719.150 KRISTA** (Hard Cap'in %15,58'i)
* **5. Yıl Sonu (Blok 5.194.000):**
  - Blok Ödülü (Dönem 19): **10.42 KRISTA**
  - Dolaşımdaki Arz: **68.123.767 KRISTA** (Hard Cap'in %32,44'ü)
* **10. Yıl Sonu (Blok 10.378.000):**
  - Blok Ödülü (Dönem 39): **7.09 KRISTA**
  - Dolaşımdaki Arz: **111.706.513 KRISTA** (Hard Cap'in %53,19'u)
* **20. Yıl Sonu (Blok 20.746.000):**
  - Blok Ödülü (Dönem 79): **3.30 KRISTA**
  - Dolaşımdaki Arz: **161.635.974 KRISTA** (Hard Cap'in %76,97'si)
* **32.5. Yıl (Blok 33.700.000):**
  - **%90 Doyum Milestone:** Dolaşımdaki arz **188.322.446 KRISTA** seviyesine ulaşır ve blok ödülü **1.23 KRISTA** seviyesine iner.
* **45.5. Yıl (Blok 47.170.000):**
  - **%95 Doyum Milestone:** Dolaşımdaki arz **198.788.454 KRISTA** seviyesine ulaşır ve blok ödülü **0.45 KRISTA** seviyesine iner.
* **100. Yıl Sonu (Blok 103.680.000):**
  - Blok Ödülü: **0.01 KRISTA**
  - Dolaşımdaki Arz: **204.808.332 KRISTA** (Hard Cap'in %97,53'ü)

---

## 4. Ekonomik Güven ve Güvenlik Bütçeleri Gerekçeleri

1. **Aşırı Uzun Vadeli Teşvik (Sustainability):** Ödüllerin sıfırlanma süresi eski modele göre 3 kat uzatılarak madenci ve staker katılımı onlarca yıl garantiye alınmıştır.
2. **Kıtlık ve Saygınlık Korunması:** 210M arz limiti, Bitcoin ve Litecoin gibi saygın projelerin ölçeğindedir. Birim değerde sulandırılma hissi yaratmaz.
3. **Quorum Kararlılığı ve Dump Koruması:** 4.200 KRISTA teminatı, kademeli Bootstrap ödülüyle (`40 -> 30 -> 20 KRISTA`) beslenerek Model D aktivasyonunda (`Blok 2.200`) 19 masternode, Bootstrap sonunda (`Blok 10.000`) ise 59 masternode kapasitesi yaratır; hem LLMQ/Model D yapılarını kararlı hale getirir hem de aşırı likit arz birikimini önler.
