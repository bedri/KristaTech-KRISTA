**YENİ BİR KANIT ALGORİTMASI İÇİN FİKİRLER**

**Mevcut kanıt konseptleri:**

1) Proof of Work (PoW)  
2) Proof of Stake (PoS)  
3) Proof of Content (PoC)  
4) Proof of Storage (PoSt veya benzeri)  
5) Proof of Burn (PoB)  
6) Proof of Service (PoSE)

**Kullanılabilecek yeni kanıt konseptleri:**

1) Proof of Full Node (PoFN): Bu aslında PoSE tarafından sağlanmakta ve gereksinim duyulmaktadır, ancak yine de yeni bir kanıt / çoklu kanıt (multi-proof) konsepti için temel olarak kullanılabilir.  
     
2) Proof of Many Transactions (PoMT): Bu konsept, kullanıcının belirli sayıda işlem yaptığının veya belki de belirli sayıda işlemde belirli miktarda coin transfer ettiğinin bir kanıtını arar. İşlem özelliklerine göre başka kriterler de belirlenebilir.  
     
**Multi-Proof-Algorithm (MPA) Madencilik Sistemleri:**

1) PoS + PoM + PoB + PoL (MPA / PoMBL): Bu sistem, Proof of Stake (PoS) konseptini taban çizgisi (baseline) olarak kullanır ve bir blok staking kernel'inin konsensüs ağırlığını belirlemek için PoM, PoB ve PoL katmanlarını ekler. Toplam madencilik ağırlığı, `src/kernel.cpp` içindeki `CalculateMPAWeight()` aracılığıyla dinamik olarak hesaplanır:
   
   - **Proof of Stake (PoS - Taban Çizgisi):**
     $$W_{\text{PoS}} = \text{Amount}$$
     Standart staking UTXO'larının temel ağırlık çarpanıdır.
     
   - **Proof of Lock (PoL):**
     En fazla $L_{\text{MAX}}$ olacak şekilde $L$ blok süresince, mutlak (`OP_CHECKLOCKTIMEVERIFY`) veya göreceli (`OP_CHECKSEQUENCEVERIFY`) zaman kilitleri (timelock) kullanılarak kilitlenen UTXO'lar için geçerlidir:
     $$W_{\text{PoL}} = \text{Amount} \times \left(1 + \gamma \cdot \min\left(\frac{L}{L_{\text{MAX}}}, 1.0\right)\right)$$
     Burada $\gamma = 2.0$ (3 kata kadar ağırlık bonusu verir) ve $L_{\text{MAX}} = 50.000$ bloktur.
     
   - **Proof of Burn (PoB):**
     Kayıtlı ve harcanamaz bir yakım adresine (burn address) gönderilen coin'ler, zaman $T$ (yakım bloğundan bu yana geçen blok sayısı) geçtikçe doğrusal olarak azalan yüksek bir başlangıç çarpanı alır:
     $$W_{\text{PoB}} = \text{Amount} + \text{BurnAmount} \times \beta \times \left(1 - \frac{T}{T_{\text{MAX}}}\right)$$
     Burada $\beta = 5.0$ ve $T_{\text{MAX}} = 10.000$ bloktur. $T \ge T_{\text{MAX}}$ olduğunda, yakım ağırlığı 0'a düşer (geriye temel staking UTXO miktarı kalır).
     
   - **Proof of Masternode (PoM):**
     Teminatı $C$ ve aktif ömrü $t_{\text{active}}$ (durumu `ENABLED` olarak değiştikten sonraki blok sayısı) olan aktif Masternode'lar:
     $$W_{\text{PoM}} = C \times \left(1 + \alpha \cdot \min\left(\frac{t_{\text{active}}}{T_{\text{MAX}}}, 1.0\right)\right)$$
     Burada $\alpha = 1.0$ (2 kata kadar ağırlık sağlar) ve $T_{\text{MAX}} = 10.000$ bloktur. `ENABLED` durumu dışındaki herhangi bir durum değişikliği, aktif ömrü 0'a sıfırlar.

2) LLMQ Quorum Signatures:
   Konsensüs, doğrudan BLS anahtarlarını kullanmak yerine, secp256k1 üzerinde oluşturulmuş deterministik bir Uzun Ömürlü Masternode Quorum'undan (Long-Living Masternode Quorum - LLMQ) yararlanır. Üyeler, rolling VRF seed kullanılarak deterministik olarak seçilir ve blok başlıklarını imzalamak için DKG'yi koordine eder (sürüm 12 bloklarındaki `vQuorumSig` alanını doldurur).

**Genel Prosedür/Yaklaşım (ADAM + MPA)**

1) Aktif Masternode havuzu, seçim temeli olarak işlev görür (`GetAdamMinerPool`).
2) Her blok yüksekliği için deterministik olarak bir rolling VRF seed hesaplanır.
3) Seçilen madenciler hafif PoW bulmacalarını çözer.
4) Coordinator, quorum eşiğini (konsensüs parametresi `nAdamThreshold` tarafından tanımlanan) karşılayan çözümleri bir araya getirir.
5) Staker'lar blok şablonları oluşturur, doğrulanmış madenci çözümlerini önbellekten toplar, Coordinator'ın VRF kanıtını (VRF proof) ve blok imzasını alır ve staking UTXO anahtarı (`vchBlockSig`) ile bloğu imzalayarak çift imzalı konsensüse ulaşır.

REFERANSLAR:

[1] https://en.bitcoin.it/wiki/Timelock
