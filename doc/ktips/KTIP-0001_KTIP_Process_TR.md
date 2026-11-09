```
KTIP: 0001
Başlık: KTIP Süreci (KTIP Process)
Yazar: KristaTech Core Developers
Durum: Active
Tür: Process
Oluşturulma Tarihi: 2026-06-16
```

## Özet
Bu belge, KristaTech İyileştirme Önerisi (KTIP) sürecini tanımlamaktadır. Bir KTIP; KristaTech topluluğuna bilgi sağlayan, KristaTech için yeni bir özelliği, süreçlerini veya ortamını açıklayan bir tasarım belgesidir.

## Motivasyon
KristaTech; ağ iyileştirmelerini, protokol değişikliklerini ve konsensüs güncellemelerini tanıtmak, tartışmak ve belgelemek için standartlaştırılmış, şeffaf ve yapılandırılmış bir sürece ihtiyaç duyar. KTIP sürecini kurarak, teknik özelliklerin ayrıntılı bir şekilde sunulmasını, geriye dönük uyumluluğun değerlendirilmesini ve uygulama ayrıntılarının geliştiriciler ile ağ katılımcıları için arşivlenmesini sağlıyoruz.

## KTIP Türleri
KTIP'leri üç farklı kategoride sınıflandırıyoruz:
1. **Standart Yolda (Standard Track)**: Çekirdek protokolü, blok doğrulama kurallarını, konsensüs parametrelerini, kriptografik ilkelere (primitives), P2P ağ serileştirmesini veya RPC arayüzünü etkileyen her türlü değişiklik.
2. **Bilgilendirici (Informational)**: Ekosistem, geliştiriciler ve madencilik havuzları için yararlı yönergeler, tasarım şablonları veya genel bilgiler.
3. **Süreç (Process)**: Geliştirme iş akışını, kod derleme yönergelerini, sürekli entegrasyon (CI) hattını veya belgelendirme yapılarını değiştirmeye yönelik öneriler.

## KTIP Yaşam Döngüsü
Bir KTIP, ömrü boyunca aşağıdaki durumlardan geçer:

```mermaid
stateDiagram-v2
    [*] --> Draft : Yazar taslağı sunar
    Draft --> Proposed : Akran denetimi talep edilir
    Proposed --> Active : Testnet/Regtest üzerinde konuşlandırıldı
    Proposed --> Rejected : Terk edildi veya reddedildi
    Active --> Final : Konsensüs/Spork ile Mainnet'te etkinleştirildi
    Active --> Rejected : Geri alındı/Değiştirildi
```

* **Taslak (Draft)**: Öneri hazırlık aşamasındadır ve yazarlar tarafından aktif olarak düzenlenmektedir.
* **Önerilen (Proposed)**: Öneri tamamlanmış, resmi olarak sunulmuş ve çekirdek geliştiriciler ile topluluk tarafından inceleme sürecindedir.
* **Aktif (Active)**: Öneri onaylanmış ve uygulaması Testnet ve Regtest ortamlarında test edilmek üzere devreye alınmıştır.
* **Nihai (Final)**: Öneri, belirli bir blok yüksekliği konsensüs tetikleyicisi veya bir Spork etkinleştirmesi aracılığıyla Mainnet üzerinde başarıyla etkinleştirilmiştir.
* **Reddedildi (Rejected)**: Güvenlik açıkları, fikir birliği eksikliği veya daha üstün bir öneriyle değiştirilmesi nedeniyle öneri etkinleştirilmeden kapatılmıştır.

## Belge Şablonu
Her KTIP, bir üst bilgi bloğu ile başlamalı (bir metin bloğu içinde) ve aşağıdaki yapılandırılmış bölümleri içermelidir:
1. **Abstract (Özet)**: Önerilen değişikliğin kısa bir teknik özeti (200 kelimenin altında).
2. **Motivation (Motivasyon)**: Mevcut protokolün neden yetersiz olduğunu ve güncellemenin neden gerekli olduğunu açıklayan gerekçe.
3. **Specification (Teknik Özellikler)**: Matematiksel formüller, veri yapıları, durum makineleri ve konsensüs parametreleri dahil olmak üzere önerinin kesin teknik ayrıntıları.
4. **Backward Compatibility (Geriye Dönük Uyumluluk)**: Güncellemenin eski düğümler üzerindeki etkisinin değerlendirilmesi (örn. Soft Fork, Hard Fork veya Spork kontrollü).
5. **Reference Implementation (Referans Uygulama)**: Spesifikasyonu uygulayan pull request'lere, commit'lere veya kaynak kod dosyalarına bağlantılar.

## Aktif KTIP Dizini

| KTIP | Başlık | Tür | Durum | Belgeler |
| :--- | :--- | :--- | :--- | :--- |
| **KTIP-0001** | KTIP Süreci | Process | Active | [EN](KTIP-0001_KTIP_Process.md) / [TR](KTIP-0001_KTIP_Process_TR.md) |
| **KTIP-0002** | ADAM İşbirlikçi Konsensüsü | Standard Track (Consensus) | Active | [EN](KTIP-0002_ADAM.md) / [TR](KTIP-0002_ADAM_TR.md) |
| **KTIP-0003** | PoMPA Ağırlık Modeli ve Çift Madencilik | Standard Track (Consensus) | Active | [EN](KTIP-0003_PoMPA_Dual_Mining.md) / [TR](KTIP-0003_PoMPA_Dual_Mining_TR.md) |
| **KTIP-0004** | PoMBL Çift İmza Doğrulama Kuralları | Standard Track (Consensus) | Active | [EN](KTIP-0004_PoMBL.md) / [TR](KTIP-0004_PoMBL_TR.md) |
| **KTIP-0005** | PoBLS Hibrit ECDSA/BLS12-381 İmza Konsensüsü | Standard Track (Consensus) | Active | [EN](KTIP-0005_PoBLS.md) / [TR](KTIP-0005_PoBLS_TR.md) |
| **KTIP-0006** | Model D Ağ Güncellemesi (LLMQ, DKG ve Ödül Dağılımları) | Standard Track (Consensus) | Active | [EN](KTIP-0006_Model_D.md) / [TR](KTIP-0006_Model_D_TR.md) |
| **KTIP-0007** | Protokol Ölçeklendirme Güncellemesi (8 MB Blok, 30sn Süre, BIP152) | Standard Track (Consensus) | Active | [EN](KTIP-0007_Scaling_Upgrade.md) / [TR](KTIP-0007_Scaling_Upgrade_TR.md) |
| **KTIP-0008** | MESCAL Bildirimsel Akıllı Sözleşmeleri ve Taproot (P2TR / BIP-350) | Standard Track (Consensus / Script) | Active | [EN](KTIP-0008_MESCAL.md) / [TR](KTIP-0008_MESCAL_TR.md) |
| **KTIP-0009** | Çift Yöntemli Madenci Kaydı ve Çift Kulvarlı ($7 + 1 + 3$) Kotalar | Standard Track (Consensus) | Active | [EN](KTIP-0009_Miner_Registration_Dual_Track.md) / [TR](KTIP-0009_Miner_Registration_Dual_Track_TR.md) |
| **KTIP-0010** | Zincir Üstü Heartbeat Pingleri, Yedek Koltuk Devri, Eşik Gevşetme ve Eşit Emek Teşvikleri | Standard Track (Consensus / Economics) | Active | [EN](KTIP-0010_Consensus_Liveness_Incentives.md) / [TR](KTIP-0010_Consensus_Liveness_Incentives_TR.md) |
| **KTIP-0011** | Sentry Düğüm DDoS Kalkanı, Sınırlı Gelecek Blok Kuyruğu ve LLMQ Hazine Yönetişimi | Standard Track (Network / Governance) | Active | [EN](KTIP-0011_Sentry_FutureBlocks_Treasury.md) / [TR](KTIP-0011_Sentry_FutureBlocks_Treasury_TR.md) |
