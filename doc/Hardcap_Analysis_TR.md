# KRISTA Blokzinciri: Hard Cap & Ekonomik Güven Analiz Raporu (Revize Edilmiş Nihai Sürüm)

Bu rapor, KRISTA ağının yeni kararlaştırılan **210 Milyon KRISTA** maksimum arz limiti (Hard Cap), **%1.9 üç aylık (~90 günde bir) azalma (decay)** oranı, **2.100 KRISTA** sabit masternode teminatı ve güncellenen bootstrap modeli çerçevesinde revize edilmiştir. Bu yeni yapı, blok ödüllerinin ömrünü maksimum seviyede tutmayı ve ağın kıtlık algısını en güvenli ölçekte sürdürmeyi hedeflemektedir.

---

## 1. Yönetici Özeti

KRISTA blokzinciri için seçilen yeni tokenomics parametreleri, Litecoin (84M) ve Bitcoin (21M) gibi küresel standartların izinden giderek hem yüksek yatırımcı güveni sağlamakta hem de ödül süresini onlarca yıla yaymaktadır:
1. **Seçilen Hard Cap:** **210.000.000 (210 Milyon) KRISTA** (Bitcoin'in tam 10 katı).
2. **Uzatılmış Ödül Ömrü (1.9% Decay):** Üç aylık (~90 günde bir) azalma oranı %5'ten **%1.9** seviyesine düşürülmüştür. Bu sayede blok ödülleri çok daha yavaş azalmakta ve **blok ödülü süresi 50+ yıl boyunca etkin kalmaktadır**.
3. **Masternode Teminatı:** Arz ile orantılı olarak **2.100 KRISTA** flat olarak güncellenmiştir.
4. **Bootstrap Güçlendirmesi:** Quorum kilitlenmelerini önlemek amacıyla 10.000 blokluk bootstrap ödülü **100 KRISTA**'ya yükseltilmiştir.

---

## 2. Tokenomics Modeli Karşılaştırma Matrisi

Yeni tasarlanan 210M modelinin detayları aşağıda özetlenmiştir:

| Parametre / Metrik | Eski Model (100M Başlangıç) | Yeni Seçilen Model (210M - Uzun Ömürlü) |
| :--- | :---: | :---: |
| **Maksimum Arz (Hard Cap)** | 100.000.000 KRISTA | **210.000.000 KRISTA** |
| **Bootstrap Blok Ödülü** | 50 KRISTA | **100 KRISTA** (10k blok) |
| **Bootstrap Toplam Üretim** | 499.900 KRISTA | **999.800 KRISTA** (Ağın ~%0.48'i) |
| **Başlangıç Blok Ödülü (Post-Bootstrap)** | 19,0 KRISTA | **15 KRISTA** |
| **Dönemlik Azalma (Decay) Oranı** | %5.00 | **%1.90 (Her ~90 günde bir)** |
| **Asimptotik Sınır (Fiili Arz)** | 98.995.900 KRISTA | **205.631.379 KRISTA** |
| **Hard Cap'e Oranı (%)** | %99,00 | **%97,92** |
| **Yedek/Boşluk Rezervi** | 1.004.100 KRISTA | **4.368.621 KRISTA** |
| **Masternode Teminatı** | 1.000 KRISTA | **2.100 KRISTA** |
| **%90 Doyuma Ulaşma Süresi** | 11,5 Yıl | **32,5 Yıl** |
| **%95 Doyuma Ulaşma Süresi** | 15,5 Yıl | **45,5 Yıl** |

> [!TIP]
> **Yedek Rezerv Avantajı:** 205.63M asimptot limiti ile 210M hard cap arasındaki **4.36 Milyon KRISTA**'lık fark, blok ödüllerinin teorik olarak hiçbir zaman sert bir şekilde kesilmeyeceğini ve ağın işlem ücretlerine (fees) yumuşak bir şekilde adapte olacağını garanti eder.

---

## 3. Uzun Vadeli Zaman Doyumu ve Arz Simülasyonu

Yeni 210M parametrelerine göre ağın 100 yıllık dönemdeki arz ve ödül dağılımı:

* **0. Yıl (İlk 10.000 Blok / Bootstrap):**
  - Blok Ödülü: **100 KRISTA**
  - Dolaşımdaki Arz: **999.800 KRISTA**
  - *Masternode Etkisi:* Blok 2.000'de (LLMQ aktivasyonunda) dolaşımda 200.000 KRISTA bulunur. 2.100 KRISTA teminat gereksinimi altında bu miktar **95 aktif masternode**'u destekleyebilir. Quorum kilitlenmeleri tamamen engellenmiştir.
* **1. Yıl Sonu (Blok 1.046.800):**
  - Blok Ödülü (Dönem 3): **14.16 KRISTA** (yavaş sönümlenme)
  - Dolaşımdaki Arz: **19.714.982 KRISTA** (Hard Cap'in %9,39'u)
* **2. Yıl Sonu (Blok 2.083.600):**
  - Blok Ödülü (Dönem 7): **13.12 KRISTA**
  - Dolaşımdaki Arz: **33.447.010 KRISTA** (Hard Cap'in %15,93'ü)
* **5. Yıl Sonu (Blok 5.194.000):**
  - Blok Ödülü (Dönem 19): **10.42 KRISTA**
  - Dolaşımdaki Arz: **68.851.627 KRISTA** (Hard Cap'in %32,79'u)
* **10. Yıl Sonu (Blok 10.378.000):**
  - Blok Ödülü (Dönem 39): **7.09 KRISTA**
  - Dolaşımdaki Arz: **112.434.373 KRISTA** (Hard Cap'in %53,54'ü)
* **20. Yıl Sonu (Blok 20.746.000):**
  - Blok Ödülü (Dönem 79): **3.30 KRISTA**
  - Dolaşımdaki Arz: **162.363.834 KRISTA** (Hard Cap'in %77,32'si)
* **32.5. Yıl (Blok 33.700.000):**
  - **%90 Doyum Milestone:** Dolaşımdaki arz **189.050.306 KRISTA** seviyesine ulaşır ve blok ödülü **1.23 KRISTA** seviyesine iner.
* **45.5. Yıl (Blok 47.170.000):**
  - **%95 Doyum Milestone:** Dolaşımdaki arz **199.516.314 KRISTA** seviyesine ulaşır ve blok ödülü **0.45 KRISTA** seviyesine iner.
* **100. Yıl Sonu (Blok 103.680.000):**
  - Blok Ödülü: **0.01 KRISTA**
  - Dolaşımdaki Arz: **205.536.192 KRISTA** (Hard Cap'in %97,87'si)

---

## 4. Ekonomik Güven ve Güvenlik Bütçeleri Gerekçeleri

1. **Aşırı Uzun Vadeli Teşvik (Sustainability):** Ödüllerin sıfırlanma süresi eski modele göre 3 kat uzatılarak madenci ve staker katılımı onlarca yıl garantiye alınmıştır.
2. **Kıtlık ve Saygınlık Korunması:** 210M arz limiti, Bitcoin ve Litecoin gibi saygın projelerin ölçeğindedir. Birim değerde sulandırılma hissi yaratmaz.
3. **Quorum Kararlılığı:** 2.100 KRISTA teminatı, Bootstrap aşamasındaki yüksek ödülle (100 KRISTA) beslenerek ağ başlangıcında 95+ masternode kapasitesi yaratır ve LLMQ yapılarını anında kararlı hale getirir.
