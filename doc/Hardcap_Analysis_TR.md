# KRISTA Blokzinciri: Hard Cap & Ekonomik Güven Analiz Raporu (Revize Edilmiş Nihai Sürüm)

Bu rapor, KRISTA ağının **210 Milyon KRISTA** maksimum arz limiti (Hard Cap), **%2,15 üç aylık (~90 günde bir) azalma (decay)** oranı, **4.200 KRISTA** sabit masternode teminatı ve blok 2'den itibaren tekdüze **17 KRISTA** sabit başlangıç emisyon modeli çerçevesinde revize edilmiştir. Bu yapı, geçici bootstrap basamaklarını kaldırarak erken likiditeyi ve 210M kıtlık dengesini 45+ yıllık aktif emisyon ömrüyle birleştirir.

---

## 1. Yönetici Özeti

KRISTA blokzinciri için seçilen tokenomics parametreleri, Litecoin (84M) ve Bitcoin (21M) gibi küresel standartların izinden giderek hem yüksek yatırımcı güveni sağlamakta hem de ödül süresini onlarca yıla yaymaktadır:
1. **Seçilen Hard Cap:** **210.000.000 (210 Milyon) KRISTA** (Bitcoin'in tam 10 katı).
2. **Uzatılmış Ödül Ömrü (%2,15 Decay):** 2. bloktan itibaren **17 KRISTA** ile başlayarak üç aylık (~90 gün / 259.200 blokta bir) azalma oranı **%2,15** olarak uygulanır. Bu sayede ani basamak düşüşleri yaşanmadan **blok ödülleri 45+ yıl boyunca etkin kalır**.
3. **Masternode Teminatı:** Arz ile orantılı olarak sabit **4.200 KRISTA** şeklindedir.
4. **Tekdüze 17 KRISTA Başlangıcı ve Model D Hizalaması (Blok 5.000):** `40 -> 30 -> 20 KRISTA` kademeli bootstrap düşüşü yerine Blok 2'den itibaren tekdüze **17 KRISTA** ile `UPGRADE_POMBL` (Blok 4.800) ve `UPGRADE_MODELD` / `nAdamStrictHeight` (Blok 5.000) yüksekliğine ulaşılır; Blok 5.000'de dolaşımdaki **84.983 KRISTA** (net `79.034,19 KRISTA`) ile **18,82 masternode** kapasitesi sağlanır.

---

## 2. Tokenomics Modeli Karşılaştırma Matrisi

Yeni tasarlanan 210M modelinin detayları aşağıda özetlenmiştir:

| Parametre / Metrik | Eski Model (100M Başlangıç) | Yeni Seçilen Model (210M - 17 KRISTA Tekdüze) |
| :--- | :---: | :---: |
| **Maksimum Arz (Hard Cap)** | 100.000.000 KRISTA | **210.000.000 KRISTA** |
| **Başlangıç Blok Ödülü (Blok 2+)** | 50 KRISTA | **17 KRISTA** (Premine Yok — Blok 0–1 = 0 KRISTA) |
| **Blok 5.000'e Kadar Toplam Üretim** | 250.000 KRISTA | **84.983 KRISTA** (`79.034,19` net = 18,82 MN) |
| **Dönemlik Azalma (Decay) Oranı** | %5,00 | **%2,15 (Her ~90 günde / 259.200 blokta bir)** |
| **Asimptotik Sınır (Fiili Arz)** | 98.995.900 KRISTA | **204.948.837 KRISTA** |
| **Hard Cap'e Oranı (%)** | %99,00 | **%97,59** |
| **Yedek/Boşluk Rezervi** | 1.004.100 KRISTA | **5.051.163 KRISTA** (`%2,41`) |
| **Masternode Teminatı** | 250 KRISTA | **4.200 KRISTA** |
| **%90 Doyuma Ulaşma Süresi** | 11,5 Yıl | **29,0 Yıl** (`365g`) / **29,4 Yıl** (`4Ç`) *(Asimptotun %90'ı için 26,1 yıl)* |
| **%95 Doyuma Ulaşma Süresi** | 15,5 Yıl | **41,2 Yıl** (`365g`) / **41,7 Yıl** (`4Ç`) *(Asimptotun %95'i için 34,0 yıl)* |

> [!TIP]
> **Yedek Rezerv Avantajı:** 204,95M asimptot limiti ile 210M hard cap arasındaki **5,05 Milyon KRISTA**'lık (`5.051.163 KRISTA`) fark, blok ödüllerinin teorik olarak hiçbir zaman sert bir şekilde kesilmeyeceğini ve ağın işlem ücretlerine (fees) yumuşak bir şekilde adapte olacağını garanti eder.

---

## 3. Uzun Vadeli Zaman Doyumu ve Arz Simülasyonu

Yeni 210M parametrelerine göre ağın 100 yıllık dönemdeki arz ve ödül dağılımı (2. bloktan itibaren `period = (nHeight - 2) / 259200` olmak üzere `CMasternode::GetBlockValue` 128-bit tamsayı sönümlenmesine göre doğrulanmıştır):

* **Başlangıç ve Erken Konsensüs Aktivasyonu (Blok 0 – 5.000 / Dönem 0):**
  - Blok Ödülü: **Blok 0–1: 0 KRISTA | Blok 2–259.201 (Dönem 0): 17 KRISTA**
  - Blok 4.800'de Dolaşımdaki Arz (`UPGRADE_POMBL`): **81.583 KRISTA** (`75.872,19 KRISTA` net, `4.200 KRISTA` teminat altında en fazla **18 aktif masternode** kapasitesi).
  - Blok 5.000'de Dolaşımdaki Arz (`UPGRADE_MODELD` / `nAdamStrictHeight` / `nTreasuryGovernanceStartHeight`): **84.983 KRISTA** (`79.034,19 KRISTA` net — **18,82 masternode** kapasitesi; 7 MN komitesinin ~2,7 katı ve 5 MN LLMQ kvorum eşiğinin 3,8 katı).
* **1. Yıl Sonu (Blok 1.036.801 [4Ç] / Blok 1.051.199 [365g]):**
  - Blok Ödülü: **15,93 KRISTA** (`Dönem 3`, 4Ç) / **15,58 KRISTA** (`Dönem 4`, 365g)
  - Dolaşımdaki Arz: **17.065.278 KRISTA** (`4Ç`, Hard Cap'in %8,13'ü) / **17.289.663 KRISTA** (`365g`, %8,23'ü)
* **2. Yıl Sonu (Blok 2.073.601 [4Ç] / Blok 2.102.399 [365g]):**
  - Blok Ödülü: **14,60 KRISTA** (`Dönem 7`, 4Ç) / **14,29 KRISTA** (`Dönem 8`, 365g)
  - Dolaşımdaki Arz: **32.709.598 KRISTA** (`4Ç`, Hard Cap'in %15,58'i) / **33.121.030 KRISTA** (`365g`, %15,77'si)
* **5. Yıl Sonu (Blok 5.184.001 [4Ç] / Blok 5.255.999 [365g]):**
  - Blok Ödülü: **11,25 KRISTA** (`Dönem 19`, 4Ç) / **11,01 KRISTA** (`Dönem 20`, 365g)
  - Dolaşımdaki Arz: **72.251.546 KRISTA** (`4Ç`, Hard Cap'in %34,41'i) / **73.044.021 KRISTA** (`365g`, %34,78'i)
* **10. Yıl Sonu (Blok 10.368.001 [4Ç] / Blok 10.511.999 [365g]):**
  - Blok Ödülü: **7,28 KRISTA** (`Dönem 39`, 4Ç) / **7,13 KRISTA** (`Dönem 40`, 365g)
  - Dolaşımdaki Arz: **119.031.925 KRISTA** (`4Ç`, Hard Cap'in %56,68'i) / **120.058.141 KRISTA** (`365g`, %57,17'si)
* **20. Yıl Sonu (Blok 20.736.001 [4Ç] / Blok 21.023.999 [365g]):**
  - Blok Ödülü: **3,05 KRISTA** (`Dönem 79`, 4Ç) / **2,92 KRISTA** (`Dönem 81`, 365g)
  - Dolaşımdaki Arz: **168.931.479 KRISTA** (`4Ç`, Hard Cap'in %80,44'ü) / **169.790.038 KRISTA** (`365g`, %80,85'i)
* **29,0. Yıl (`365g`) / 29,4. Yıl (`4Ç`) (Blok 30.451.596):**
  - **%90 Hard Cap Milestone:** Dolaşımdaki arz **189.000.000 KRISTA** (Hard Cap'in %90,00'ı) seviyesine ulaşır ve blok ödülü **1,34 KRISTA** seviyesine iner *(204,95M asimptotunun %90'ı olan `184.453.953 KRISTA`'ya 26,1. Yıl / Blok 27.460.198'de `1,74 KRISTA` blok ödülüyle ulaşılır)*.
* **41,2. Yıl (`365g`) / 41,7. Yıl (`4Ç`) (Blok 43.259.214):**
  - **%95 Hard Cap Milestone:** Dolaşımdaki arz **199.500.000 KRISTA** (Hard Cap'in %95,00'ı) seviyesine ulaşır ve blok ödülü **0,46 KRISTA** seviyesine iner *(204,95M asimptotunun %95'i olan `194.701.395 KRISTA`'ya 34,0. Yıl / Blok 35.726.730'da `0,87 KRISTA` blok ödülüyle ulaşılır)*.
* **100. Yıl Sonu (Blok 103.680.001 [4Ç] / Blok 105.119.999 [365g]):**
  - Blok Ödülü: **0,0029 KRISTA** (`Dönem 399`, 4Ç) / **0,0026 KRISTA** (`Dönem 405`, 365g)
  - Dolaşımdaki Arz: **204.914.483 KRISTA** (`4Ç`, Hard Cap'in %97,58'i, Asimptotun %99,98'i)

---

## 4. Ekonomik Güven ve Güvenlik Bütçeleri Gerekçeleri

1. **Aşırı Uzun Vadeli Teşvik (Sustainability):** Dönem 0'dan itibaren yumuşak `%2,15` üç aylık sönümlenme, ani bootstrap ödül düşüşlerini ortadan kaldırırken madenci ve staker katılımını onlarca yıl garanti altına alır.
2. **Kıtlık ve Saygınlık Korunması:** 210M arz limiti, Bitcoin ve Litecoin gibi saygın projelerin ölçeğindedir ve `5,05M` rezerv boşluğuyla kıtlığı korur.
3. **Quorum Kararlılığı ve Öngörülebilir Başlangıç:** Sabit `4.200 KRISTA` teminatı ve Blok 2'den itibaren tekdüze **17 KRISTA** blok ödülü, `UPGRADE_POMBL` (Blok `4.800`) noktasında **18 masternode** ve Model D aktivasyonunda (Blok `5.000`) **18,82 masternode** kapasitesi sağlayarak LLMQ/Model D geçişini yapay ödül şokları olmadan istikrara kavuşturur.
