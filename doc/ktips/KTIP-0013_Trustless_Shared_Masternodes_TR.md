```
KTIP: 0013
Başlık: Taproot, PSBT ve Çoklu Ödeme (Multi-Payee) Konsensüsü ile Güvensiz (Trustless) Paylaşımlı Masternode
Yazar: KristaTech Core Developers
Durum: Taslak (Draft)
Tür: Standart İz (Konsensüs / Masternode / Cüzdan)
Tarih: 2026-11-17
Gereksinimler: KTIP-0005, KTIP-0006, KTIP-0008, KTIP-0012
```

## Özet (Abstract)
Bu öneri, KristaTech (`KRISTA`) ağı için **emanetsiz / güvensiz (non-custodial / trustless) Paylaşımlı Masternode (Shared Masternode)** protokolünü tanımlar. KristaTech'in mevcut **Taproot (`kt1p...` / BIP341)**, **Schnorr (`BIP340`)**, **BIP68/CSV göreceli zaman kilitleri**, **BIP174 PSBT** ve **protokol seviyesinde Çoklu Ödeme (Multi-Payee) coinbase bölüştürme** altyapısını birleştirerek; en fazla `21` katılımcının `4.200 KRISTA` teminatı tek bir operatöre güvenmeden ortaklaşa kilitlemesini ve ödülleri doğrudan blok ödülünde otomatik almasını sağlar.

## Gerekçe (Motivation)
Mevcut Masternode kuralları (`src/masternode.cpp`) iki tekil sahiplik varsayımına dayanır:
1. **Yalnızca P2PKH Teminat (`L260`, `L640`)**: `4.200 KRISTA` teminat UTXO'su tek bir `CKeyID` adresine (`GetScriptForDestination(pubKeyCollateralAddress.GetID())`) kilitli olmak zorundadır; Taproot (`WitnessV1Taproot`) veya P2SH multisig çıktıları reddedilir.
2. **Tek Ödül Adresi (`L281`, `L329`)**: Tüm Masternode ödülleri (Model D kapsamında `%35` Pasif Havuz, `%20` Doğrulayıcı Payı, `%10` LLMQ Çoğunluk Payı) doğrudan bu tek teminat adresine ödenir. Bu durum, ortak havuzların özel anahtarı tutan merkezi operatörlere güvenmesini zorunlu kılar.

KTIP-0013 her iki emanet (custody) riskini konsensüs katmanında ortadan kaldırır.

## Teknik Şartname (Specification)

### 1. Taproot Teminat Desteği (`UPGRADE_SHARED_MN`)
`UPGRADE_SHARED_MN` yükseltmesi `H_shared` yüksekliğinde etkinleştiğinde:
* **Kabul Edilen Teminat Script'leri**: `CMasternodeBroadcast` (`mnb` v2), `4.200 KRISTA` (`MASTERNODE_COLLATERAL * COIN`) tutarındaki UTXO'lar için aşağıdakileri kabul eder:
  1. Klasik `P2PKH` (`OP_DUP OP_HASH160 <20B> OP_EQUALVERIFY OP_CHECKSIG`), veya
  2. **Taproot (`WitnessV1Taproot`, `OP_1 <32B x-only pubkey>`)** (`kt1p...`).
* **Önerilen Taproot Yapısı**:
  * **Key-Path (Ortak Çıkış / Yenileme)**: Tüm havuz katılımcılarının `BIP340` / `MuSig2` birleşik `N-of-N` x-only açık anahtarı. İşbirlikçi her harcama tüm ortakların PSBT kısmi imzalarını gerektirir.
  * **Script-Path (Güvensiz Kurtarma / Operatör Zaman Aşımı)**: `BIP68` göreceli zaman kilidini (`OP_CHECKSEQUENCEVERIFY`, ör. `43.200` blok ≈ 30 gün) bir `M-of-N` (`OP_CHECKSIGADD`) eşiği veya önceden taahhüt edilmiş iade çıktılarıyla birleştiren `BIP342 Tapscript` dalı. Böylece operatör veya azınlık bir ortak kaybolsa bile katılımcılar ana paralarını tek taraflı kurtarabilir.

### 2. Tekil Anahtar Emaneti Olmadan Broadcast Yetkilendirmesi
Teminat `WitnessV1Taproot` olduğunda `mnb` v2 (`protocolVersion >= SHARED_MN_PROTO_VERSION`) içinde:
* **Yetkilendirme Kanıtı**: Teminat çıktı anahtarı (`CXOnlyPubKey`), `CMasternodeBroadcast` yükünü (`vin`, `addr`, `pubKeyMasternode` ve değiştirilemez `vPayees` tablosu dahil) bir **BIP340 Schnorr imzası** ile imzalar (ortak MuSig2 key-path imzasıyla veya fonlama işlemindeki `OP_RETURN` taahhüdüyle).
* **Sıcak Operatör İzolasyonu**: Yetkilendirme sonrası Masternode canlılık ping'leri (`SendOnChainPing`), ADAM bulmaca çözümü ve LLMQ/PoBLS imzaları mevcut `pubKeyMasternode` sıcak anahtarıyla yürütülür; operatörün `4.200 KRISTA` Taproot teminatı üzerinde **hiçbir harcama yetkisi yoktur**.

### 3. Protokol Seviyesinde Çoklu Ödül Bölüştürme (Multi-Payee)
`CMasternodeBroadcast` ve `CMasternode`, imza korumalı bir ödeme listesi içerir:
```cpp
struct CMasternodePayeeShare {
    CScript scriptPubKey; // P2PKH, P2SH veya WitnessV1Taproot (kt1p...)
    uint16_t nShareBps;   // Baz puan (1..10000; 100 bps = %1.00)
};
```
**Konsensüs Doğrulama Kuralları**:
1. **Sınırlı Dağıtım**: `1 <= vPayees.size() <= MAX_SHARED_MN_PAYEES` (`21`).
2. **Tam Toplam**: $\sum_i \text{nShareBps}_i = 10{,}000$ (`%100.00`).
3. **Dust Önleme Alt Sınırı**: Her pay `nShareBps >= 100` (`>= %1.00`, yani en az `42 KRISTA` eşdeğeri) olmalıdır.
4. **Standart Script Kontrolü**: Her `scriptPubKey` standart `P2PKH`, `P2SH` veya `WitnessV1Taproot` hedefi (`<= 34` bayt) olmalıdır.
5. **Değiştirilemezlik**: `vPayees`, `mnb` teminat imzasıyla korunur; sıcak operatör tarafından değiştirilemez. Payların güncellenmesi için teminatın yeniden oluşturulması veya teminat sahiplerince yeni bir `mnb` imzalanması gerekir.

**Coinbase Oluşturma (`FillBlockPayee` / `IsBlockPayeeValid`)**:
* Toplam Masternode ödülü $R_{\text{mn}}$ (Pasif Havuz, Doğrulayıcı veya LLMQ payı) için her ortak $i$ şu tutarı alır:
  $$R_i = \left\lfloor \frac{R_{\text{mn}} \times \text{nShareBps}_i}{10{,}000} \right\rfloor$$
* Tam sayı bölmeden kalan artık (`<= 20` uKRISTA) `vPayees[0]` adresine eklenir; böylece toplam blok emisyonu konsensüsle birebir eşleşir.
* İsteğe bağlı olarak bir pay doğrudan programlanabilir hak ediş/DAO hazinesi için **MESCAL** (`kt1p...`) kontrat adresine yönlendirilebilir.

### 4. Güvenlik ve DoS Korumaları
* **Coinbase Boyut & SigOp Sınırı**: `MAX_SHARED_MN_PAYEES = 21` sınırı coinbase büyümesini ~`750` bayt ile sınırlar ve ek girdi sigop maliyeti doğurmaz.
* **Ödül Kaçırma Koruması**: `vPayees` listesi Taproot ortakları (`MuSig2`) tarafından imzalandığı için kötü niyetli bir VPS operatörü ödülleri kendi adresine yönlendiremez.
* **Canlılık Denetimi**: VPS operatörü çevrimdışı kalırsa `2.880` blokluk aktivite penceresi / PoSe mekanizması MN'yi düşürerek ödülleri keser; `4.200 KRISTA` ana para ise güvende kalır.

### 5. Cüzdan ve PSBT İş Akışı
1. **Havuz Oluşturma (`createsharedmnpool`)**: `N` katılımcının açık anahtarlarını/ödeme adreslerini, varsa operatör komisyonunu (önerilen `<= 1.500` bps) ve CSV kurtarma süresini tanımlar.
2. **PSBT (BIP174) ile Ortak Fonlama**: Her katılımcı kendi KRISTA girdisini ekleyip `walletprocesspsbt` / `signpsbt` ile imzalar; birleşik işlem `4.200 KRISTA` tutarında tek bir `kt1p...` Taproot çıktısı üretir.
3. **Aktivasyon**: `15` onay sonrası ortaklar `mnb` v2 yetkilendirmesini imzalar ve sıcak sunucu `activemasternode.conf` üzerinden otomatik çalışmaya başlar.

## Geriye Dönük Uyumluluk
* Koordineli bir ağ yükseltmesi (`UPGRADE_SHARED_MN` aktivasyon yüksekliği) gerektirir.
* Mevcut tek sahipli `P2PKH` Masternode'lar `vPayees.empty()` durumunda (varsayılan `%100` `pubKeyCollateralAddress`) %100 geçerli kalmaya devam eder.
