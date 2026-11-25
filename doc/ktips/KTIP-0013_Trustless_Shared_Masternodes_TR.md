```
KTIP: 0013
Başlık: Taproot, PSBT ve KTDIP3 Çoklu Ödeme Konsensüsü ile Paylaşımlı Masternode (Rev 2)
Yazar: KristaTech Core Developers
Durum: Taslak (Ertelendi — Olgunluk Dönemi Yol Haritası)
Tür: Standart İz (Konsensüs / KTDIP3 / Cüzdan)
Oluşturulma: 2026-11-17
Güncelleme: 2026-11-24
Gereksinimler: KTIP-0005, KTIP-0006, KTIP-0008, KTIP-0012, KTIP-0014
```

## Özet (Abstract)
Bu revize teknik şartname; **KTDIP3 (`KTIP-0014`)** deterministik masternode altyapısı, **Taproot (`kt1p...` / BIP341)**, **Schnorr (`BIP340`)**, **BIP68/CSV göreceli zaman kilitleri**, **BIP174 PSBT** ve **konsensüs seviyesinde Çoklu Ödeme (Multi-Payee) ödül bölüştürmesi** üzerine kurulu, en fazla **`5` ortağın** (`>= %20` pay ile) `4.200 KRISTA` teminatı **operatör emaneti olmadan (zero-operator-custody)** ortaklaşa işletmesini sağlayan protokolü tanımlar.

> **Yol Haritası ve Kapsam Notu:** KristaTech hâlihazırda küçük sermayeli katılımcılar için yerleşik **PoS Staking**, **Track 1 Coin-Lock (`250 KRISTA`)**, **Track 2A PoW-Lock (`10 KRISTA`)** ve **Track 2C Zero-Coin / Ücretsiz Giriş (`0 KRISTA`)** yollarını sunduğundan; KTIP-0013 **bilinçli olarak piyasa olgunluğu sonrası döneme ertelenmiştir** (ağ değeri büyüyüp `4.200 KRISTA` tek kişi için pratik bir bariyer haline geldiğinde yeniden ele alınacaktır). Ayrıca Bölüm 4, **Aşama A (Konsorsiyum / DAO / Tanıdık Ortaklar)** ile ek protokol önkoşulları (`txid` malleability koruması veya çıktı-kısıtlı covenant + denetlenmiş `BIP327 MuSig2`) gerektiren **Aşama B (Anonim Tam Güvensiz Havuzlar)** ayrımını açıkça ortaya koyar.

## Gerekçe (Motivation)
Aktif **KTDIP3 (`KTIP-0014` + `UPGRADE_HARDENING_V3`)** kuralları altında (`src/evo/deterministicmns.cpp`, `src/masternode-payments.cpp`) deterministik masternode kaydı iki tekil sahiplik varsayımına dayanır:
1. **Tekil Teminat Anahtarı Sahipliği**: `4.200 KRISTA` teminat UTXO'su tek bir `pubKeyOwner` / `P2PKH` teminat çıktısına bağlıdır ve sahiplik/operatör güncellemeleri (`CProUpRegTx` / `CProUpRevTx`) tek bir sıkıştırılmış `pubKeyOwner` anahtarı (`CPubKey::VerifyStrictDER`) ile imzalanır.
2. **Tek Ödül Çıktısı (`scriptPayout`)**: İsteğe bağlı operatör payı (`nOperatorReward`, `0..10000` bps → `scriptOperatorPayout`) ayrıldıktan sonra kalan sahip ödüllerinin `%100`'ü (Dengeli Model D altında `%5` Pasif Havuz, `%32` Doğrulayıcı Payı [`%60 / 15` içinde `8` MN koltuğu × `%4,00`], `%10` İmzacı-Odaklı LLMQ Çoğunluk Payı — Koordinatör VRF hariç toplam en fazla `%47`) tek bir `scriptPayout` adresine gönderilir. Bu nedenle paylaşımlı havuzlar ya teminat anahtarını tutan bir emanetçiye ya da ödülleri elle dağıtacak tek bir ortağa güvenmek zorunda kalır.

KTIP-0013 Rev 2; operatör emaneti ve elle ödül dağıtımı gereksinimini **KTDIP3 konsensüs katmanında** ortadan kaldırırken UTXO şişmesini sıkı biçimde sınırlar ve çok ortaklı ana para kurtarma varsayımlarını şeffaf biçimde belgeler.

---

## Teknik Şartname (Specification)

### 1. Taproot Teminat ve Ortak Sahiplik (`UPGRADE_SHARED_MN`)
`UPGRADE_SHARED_MN` yükseltmesi `H_shared` yüksekliğinde etkinleştiğinde:
* **`CProRegTx` v3 İçinde Kabul Edilen Teminat Script'leri**: `4.200 KRISTA` (`MASTERNODE_COLLATERAL * COIN`) tutarındaki harcanmamış teminat UTXO'su şunlardan birine kilitlenebilir:
  1. Tek sahipli `P2PKH` (`KT...`), veya
  2. **Taproot (`WitnessV1Taproot`, `OP_1 <32B x-only pubkey>`)** (`kt1p...`).
* **Taproot İç Yapısı**:
  * **Key-Path (Ortak Çıkış / Yenileme)**: Tapscript destekli tek bir kontrol anahtarı veya (Aşama B'de) tüm havuz ortaklarının birleşik `N-of-N` `BIP340` / `MuSig2` x-only çıktı anahtarı.
  * **Script-Path (Kurtarma / Zaman Aşımı)**: `OP_CHECKSEQUENCEVERIFY` (`BIP68`, ör. `43.200` blok ≈ 30 gün) ile `M-of-N` (`OP_CHECKSIGADD`, KristaTech script motorunda hâlihazırda mevcut) eşiğini veya covenant zorunlu iade yollarını birleştiren `BIP342 Tapscript` yaprağı (Aşama A ve Aşama B farkları için Bölüm 4'e bakınız).

### 2. KTDIP3 `CProRegTx` v3 ve `CProUpRegTx` v3 Entegrasyonu
*(Eski `CMasternodeBroadcast` / `mnb` v2 / `SendOnChainPing` yapılarının yerini tamamen KTDIP3 alır.)*

Paylaşımlı Masternode'lar **KTDIP3 (`evodb`)** içine `CProRegTx` ve `CProUpRegTx` için `nVersion = 3` üzerinden yerleşik olarak eklenir:
```cpp
struct CMasternodePayeeShare {
    CScript scriptPubKey; // Standart harcanabilir P2PKH (KT...), P2SH (7...) veya WitnessV1Taproot (kt1p...)
    uint16_t nShareBps;   // Baz puan (2000..10000; 100 bps = %1.00)
};
```
* **Teminat Yetkilendirmesi**:
  * `CProRegTx` v3, KTDIP3'teki `blsPubKeyOperator` + zorunlu `96` baytlık **`blsPoP`** (`UPGRADE_HARDENING_V3` ile zorunlu Proof-of-Possession), `addr`, `keyIDVoting` ve `nOperatorReward` alanlarını aynen korur.
  * Teminat UTXO'su `WitnessV1Taproot` olduğunda ve/veya `vPayees` dolu olduğunda:
    * `CProRegTx` v3, `vPayees` listesini ve Taproot teminat anahtarıyla `CProRegTx::GetSignHash()` üzerinde üretilmiş **BIP340 Schnorr imzasını** (veya Tapscript tanık kanıtını) taşır.
    * `blsPubKeyOperator`, `keyIDVoting` veya `vPayees` güncellemesi yapan `CProUpRegTx` v3 aynı teminat anahtarı/script'i ile yetkilendirilir; `nOperatorReward == 0` iken `scriptOperatorPayout` alanının temiz kalması (`M-5` sertleştirmesi) korunur.
* **Sıcak Operatör İzolasyonu (KTIP-0014 Kural 8 Korunur)**:
  * Sıcak düğümler (hot node) **yalnızca `blsPrivKey`** operatör anahtarını içeren **`activemasternode.conf`** (`Single-IP Multi-MN`) standardıyla çalışmaya devam eder (`kristatech.conf` içine asla `masternodeprivkey=` yazılmaz).
  * VPS operatörü LLMQ çoğunluk imzalarına, PoBLS'e ve ADAM bulmaca çözümüne katılır; ancak `4.200 KRISTA` teminat üzerinde **hiçbir harcama yetkisi** ve `vPayees` üzerinde **hiçbir değiştirme yetkisi** yoktur.

### 3. Sınırlı Çoklu Ödül Bölüştürme ve Dust / UTXO Şişmesi Koruması
Model D bloklarında `chainstate` şişmesini ve mikro-dust çıktıları önlemek için kurallar sıkılaştırılmıştır:
1. **Sıkı Ortak Limiti**: `1 <= vPayees.size() <= MAX_SHARED_MN_PAYEES` (**`5`** — eski `21` sınırı düşürülmüştür).
2. **Anlamlı Minimum Hisse**: Her ortak en az `nShareBps >= MIN_SHARED_MN_SHARE_BPS` (**`2.000` bps = `%20,00`**, yani en az `840 KRISTA` eşdeğeri teminat hissesi) paya sahip olmalıdır.
3. **Tam Toplam**: $\sum_{i=0}^{k-1} \text{nShareBps}_i = 10{,}000$ (`%100,00`).
4. **Standart ve Harcanabilir Script Zorunluluğu**: Her `scriptPubKey` standart `P2PKH`, `P2SH` veya `WitnessV1Taproot` (`<= 34` bayt) olmalı, `!IsUnspendable()` koşulunu sağlamalı ve `mBurnAddresses` içinde yer almamalıdır.
5. **Model D Çıktı Üst Sınırı**: Dengeli Model D altında (`1` Pasif MN + en fazla `8` MN Doğrulayıcı koltuğu + dinamik LLMQ imzacıları [`5` temel LLMQ $\rightarrow$ `14` MN ödülü $\rightarrow$ blok başına **`70` çıktı**; `>= 100` aktif MN ile azami `15` LLMQ imzacısına ölçeklendiğinde `24` MN ödülü $\rightarrow$ blok başına **`120` çıktı**]), `MAX_SHARED_MN_PAYEES = 5` sınırı toplam MN ödül çıktılarını düşük tutar (`21` ortaklı tasarımdaki `294` / `504` çıktı yerine).

**Deterministik 128-Bit Dağıtım (`FillBlockPayee` / `IsBlockPayeeValid`):**
1. Varsa KTDIP3 operatör payı $R_{\text{op}}$ (`ProUpServTx` `scriptOperatorPayout`, yalnızca `nOperatorReward > 0` ve geçerli ödeme script'i varken) önce ayrılır:
   $$R_{\text{op}} = \left\lfloor \frac{\text{unsigned \_\_int128}(R_{\text{mn}}) \times n_{\text{OperatorReward}}}{10{,}000} \right\rfloor, \qquad R_{\text{owner}} = R_{\text{mn}} - R_{\text{op}}$$
2. Kalan sahip ödülü $R_{\text{owner}}$, `L-1` konsensüs standardına uygun `unsigned __int128` tam sayı aritmetiğiyle `vPayees` ortaklarına bölünür:
   $$R_i = \left\lfloor \frac{\text{unsigned \_\_int128}(R_{\text{owner}}) \times \text{nShareBps}_i}{10{,}000} \right\rfloor$$
3. **Dust ve Artık Koruması**: Tam sayı bölme artığı $\left(R_{\text{owner}} - \sum_i R_i\right)$ (`<= 4` satoshi) ile emisyon kuyruğunda `CTxOut::GetDustThreshold(minRelayTxFee)` altına düşebilecek her bireysel pay deterministik olarak `vPayees[0]` adresine eklenir; böylece toplam blok emisyonu konsensüsle birebir uyuşur ve harcanamaz dust UTXO oluşmaz.
4. *(İsteğe Bağlı)* Bir `vPayees[i].scriptPubKey`, programlanabilir DAO hazinesi veya hak ediş takvimi için doğrudan `kt1p...` **MESCAL** Taproot adresini hedefleyebilir.

---

## 4. Güvenlik Tehdit Modeli ve İki Aşamalı Önkoşullar (Şeffaf Mühendislik Değerlendirmesi)

KristaTech'in mevcut script motorunda **Taproot (`BIP340`/`BIP341`)**, **`OP_CHECKSIGADD`**, **`CSV (BIP68/112)`** ve **PSBT (`BIP174`)** hazırdır; **ancak henüz `BIP327 MuSig2` kütüphanesi, SegWit tanık ayrımı (`txid` malleability koruması) ve çıktı-kısıtlı covenant opcode'ları mevcut değildir**.

Bu teknik gerçek nedeniyle KTIP-0013 iki ayrı uygulama aşaması tanımlar:

### Aşama A — Konsorsiyum / DAO / Tanıdık Ortak Havuzları *(Mevcut Script Motoruyla Mümkün)*
* **Çözdüğü Problem**: **Tekil VPS operatörü emanetini** ve **elle ödül dağıtımını** tamamen ortadan kaldırır. En fazla `5` tanıdık ortak, ekip veya DAO; `Taproot M-of-N` (`OP_CHECKSIGADD` + isteğe bağlı `CSV` zaman kilidi) ile `4.200 KRISTA` teminatı ortaklaşa kilitleyip işletimi operatöre bırakabilir.
* **Açık Sınırlama (Neden Anonim Tam Güvensiz Değildir?)**: `txid` malleability koruması veya çıktı-kısıtlı covenant olmadan, `M-of-N` kurtarma yaprağı harcama çıkış adreslerini kısıtlayamaz; kötü niyetli bir `M` çoğunluğu kurtarma anında `4.200 KRISTA`'yı kendi adreslerine çekebilir. Saf `N-of-N` kilitte ise bir ortağın anahtar kaybetmesi veya imza vermeyi reddetmesi fonları kilitleyebilir (rehin/canlılık riski). Bu nedenle Aşama A cüzdan arayüzlerinde açıkça **Konsorsiyum / Güvenilen Çoğunluk Ortaklığı** olarak konumlandırılmalıdır.

### Aşama B — Anonim Yabancılar Arası Tam Güvensiz Havuzlar *(Zorunlu Protokol Önkoşulları)*
Birbirini tanımayan anonim katılımcıların çoğunluğa bile güvenmeden ortak havuz kurabilmesi için **ikisi birden** gereklidir:
1. **Tek Taraflı Ana Para Çıkış Garantisi**: Ya
   * **(a) SegWit / Değiştirilemez `txid`**, böylece fonlama işlemi yayınlanmadan önce ortaklar `CSV` zaman kilitli orantılı iade işlemlerini güvenli biçimde önceden imzalayabilir, **VEYA**
   * **(b) Çıktı-Kısıtlı Tapscript / MESCAL Covenant**, böylece acil kurtarma harcamasının zincir üstünde her ortağın payını kendi taahhüt edilen `scriptPubKey` adresine iade etmesi konsensüsle zorunlu tutulur.
2. **Bağımsız Denetlenmiş `BIP327 MuSig2` ve Çok Taraflı PSBT Koordinasyon Katmanı**: `N` taraflı fonlama ve ortak çıkış imzaları için nonce-güvenli `MuSig2` ve oturum/eşleştirme protokolü.

### Yönetişim Merkezileşmesi ("Lido Etkisi") Önlemi
* Paylaşımlı Masternode arayüzleri, yönetişim oylarının (`keyIDVoting`) merkezi VPS havuz sağlayıcılarına devredilmesi yerine ortakların kendi anahtarı (veya dönüşümlü konsorsiyum anahtarı) altında kalmasını teşvik etmelidir.
* Mevcut KTDIP3 **PoSe ceza puanı ve `nPoseReviveCooldownBlocks`** (`120` blok Mainnet) kuralları `CProRegTx` v3 düğümleri için de geçerlidir: operatör çevrimdışı kalırsa ödüller kesilir, `4.200 KRISTA` Taproot teminatı risk altında kalmaz.

---

## Geriye Dönük Uyumluluk
* Koordineli bir ağ yükseltmesi (`UPGRADE_SHARED_MN` aktivasyon yüksekliği) gerektirir.
* Mevcut tek sahipli **KTDIP3 v1/v2** masternode kayıtları (`vPayees.empty()`, `P2PKH` teminat) %100 geçerli kalmaya ve aynı şekilde çalışmaya devam eder.
