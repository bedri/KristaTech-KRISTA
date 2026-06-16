# MESCAL Kullanım Senaryoları & Akıllı Sözleşme Şablonları

**Minimalistically Envisioned Smart Contract Assembling Language**  
*Pratik, Kurumsal ve Güvenlik Odaklı Akıllı Sözleşmeler Rehberi*

---

## 1. Giriş

Bildirimsel (declarative), JSON tabanlı akıllı sözleşme spesifikasyonlarına ilişkin yaygın bir yanlış kanı, bunların Turing-complete programlama dillerine kıyasla çok katı veya dar kapsamlı olduğudur. Gerçekte MESCAL (Minimalistically Envisioned Smart Contract Assembling Language), işlemleri tanımlamak için inanılmaz derecede zengin, ifade gücü yüksek ve güvenli bir çerçeve sunar.

MESCAL; yapılandırılmış, makine tarafından ayrıştırılabilir JSON gösterimleri aracılığıyla Bitcoin tarzı yığın operatörlerinden (`CScript`) yararlanarak karmaşık koşullu dallanmaları, çoklu imza (multi-signature) yönetişimini, zaman kilitli antlaşmaları (time-locked covenants) ve kriptografik hash kilitlerini (hash locks) ifade edebilir. MESCAL durum tabanlı döngü yinelemelerinden (state-based loop iterations) kaçındığı için, tüm sözleşmeler yeniden giriş (re-entrancy) hatalarına, gas tükenmesi (out-of-gas) başarısızlıklarına ve derleyici düzeyindeki optimizasyon yan etkilerine karşı bağışıktır.

Bu belge; Kişisel Güvenlik, Merkeziyetsiz Finans (DeFi), E-Ticaret, Kurumsal Yönetişim, Oyun ve IoT/Oracle güdümlü otomasyon alanlarında dilin kullanışlılığını kanıtlayan, üretime hazır 20 adet MESCAL akıllı sözleşme kullanım senaryosundan oluşan kapsamlı bir havuz niteliğindedir.

---

## 2. Kategori 1: Kişisel Güvenlik & Emanet (Custody)

### 2.1. Kullanım Senaryosu 1: Acil Durum Kurtarmalı Zaman Gecikmeli Kasa (Hırsızlık Önleme)
* **Sorun**: Eğer bir saldırgan kullanıcının mobil (sıcak) cüzdanını çalarsa, tüm fonları anında boşaltabilir. Kullanıcı bir güvenlik ağı istemektedir.
* **Çözüm**: Fonlar bir kasada kilitlenir. Günlük sıcak cüzdan anahtarı kullanılarak bir çekim işlemi başlatılabilir, ancak bu işlem 72 saatlik bir gecikmeyle kilitlenir. Bu 72 saatlik zaman dilimi boyunca kullanıcı, çekimi iptal etmek ve fonları hemen geri almak için çevrimdışı soğuk cüzdan anahtarını kullanabilir.
* **CScript**: `OP_IF <cold_pubkey> OP_CHECKSIGVERIFY OP_ELSE <72h_delay> OP_CHECKLOCKTIMEVERIFY OP_DROP <hot_pubkey> OP_CHECKSIGVERIFY OP_ENDIF`

```json
{
  "basic": {
    "Cold-Wallet-Sig": {
      "role": "check-signature-verification",
      "inputs": [
        {
          "name": "Pubkey",
          "type": "pubkey",
          "value": "03bb9f1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234a1"
        }
      ]
    },
    "Hot-Wallet-Sig": {
      "role": "check-signature-verification",
      "inputs": [
        {
          "name": "Pubkey",
          "type": "pubkey",
          "value": "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f"
        }
      ]
    },
    "Withdraw-Delay": {
      "role": "lock-time",
      "inputs": [
        {
          "name": "Lock-Until",
          "type": "timestamp-or-block-height",
          "value": 1780977600
        }
      ]
    }
  },
  "condition": {
    "Recovery-Branch": {
      "role": "if-condition",
      "expressions": [
        { "type": "basic", "name": "Cold-Wallet-Sig" }
      ],
      "true": [],
      "false": [
        { "type": "basic", "name": "Withdraw-Delay" },
        { "type": "basic", "name": "Hot-Wallet-Sig" }
      ]
    }
  },
  "contract": {
    "Anti-Theft-Vault": {
      "description": "Cold wallet can recover funds instantly. Hot wallet can withdraw only after a 72-hour delay.",
      "actions": [
        { "type": "condition", "name": "Recovery-Branch" }
      ]
    }
  },
  "active_contract": "Anti-Theft-Vault"
}
```

---

### 2.2. Kullanım Senaryosu 2: Sosyal Kurtarma Cüzdanı (Ortak Muhafızlar)
* **Sorun**: Eğer cüzdan sahibi gizli anahtarını (private key) kaybederse, fonlar sonsuza dek kaybolur. Sahibi, merkezi bir emanetçiye (custodian) güvenmek istememektedir.
* **Çözüm**: Cüzdan sahibinin imzası fonları her an harcayabilir. Eğer sahibinin anahtarı kaybolursa, belirlenen 5 "Muhafızdan" (güvenilir arkadaşlar veya cihazlar) oluşan bir grup bir kurtarma işlemi imzalayabilir. Eğer 5 muhafızdan 3'ü imzalarsa, fonlar taşınabilir; ancak bu yalnızca 30 günlük bir gecikmeden sonra gerçekleşebilir. Bu sayede, muhafızların bir kısmı kötü niyetle iş birliği yaparsa sahibine kurtarma işlemini iptal etmesi için zaman tanınır.
* **CScript**: `OP_IF <owner_pubkey> OP_CHECKSIGVERIFY OP_ELSE 3 <guardian1> <guardian2> <guardian3> <guardian4> <guardian5> 5 OP_CHECKMULTISIGVERIFY <30d_delay> OP_CHECKLOCKTIMEVERIFY OP_DROP OP_ENDIF`

```json
{
  "basic": {
    "Owner-Sig": {
      "role": "check-signature-verification",
      "inputs": [
        {
          "name": "Pubkey",
          "type": "pubkey",
          "value": "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f"
        }
      ]
    },
    "Guardian-Multisig": {
      "role": "multi-signature",
      "inputs": [
        { "name": "m", "type": "number", "value": 3 },
        { "name": "n", "type": "number", "value": 5 },
        {
          "name": "Signatures",
          "type": "array",
          "value": [
            "03ab89ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1235",
            "02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1236",
            "03de76ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1237",
            "02ef54ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1238",
            "03fa32ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1239"
          ]
        }
      ]
    },
    "Recovery-Delay-Lock": {
      "role": "lock-time",
      "inputs": [
        {
          "name": "Lock-Until",
          "type": "timestamp-or-block-height",
          "value": 1783310400
        }
      ]
    }
  },
  "condition": {
    "Owner-Spend-Branch": {
      "role": "if-condition",
      "expressions": [
        { "type": "basic", "name": "Owner-Sig" }
      ],
      "true": [],
      "false": [
        { "type": "basic", "name": "Guardian-Multisig" },
        { "type": "basic", "name": "Recovery-Delay-Lock" }
      ]
    }
  },
  "contract": {
    "Social-Recovery-Wallet": {
      "description": "Owner can spend immediately. 3-of-5 Guardians can recover funds after a 30-day security lock delay.",
      "actions": [
        { "type": "condition", "name": "Owner-Spend-Branch" }
      ]
    }
  },
  "active_contract": "Social-Recovery-Wallet"
}
```

---

### 2.3. Kullanım Senaryosu 3: Zaman Sınırlı Miras Anahtarı (Dead Man's Switch)
* **Sorun**: Bir kullanıcı, vefat etmesi veya uzun süre ortalıkta olmaması durumunda kripto varlıklarının mirasçılarına kalmasını istemekte, ancak kendisi aktifken mirasçıların fonlara erişmesini istemenektedir.
* **Çözüm**: Mirasçının imzası, ancak uzun vadeli bir zaman kilidinin (örneğin 1 yıl) süresi dolduktan sonra fonları harcayabilir. Cüzdan sahibi, cüzdanı sıfırlayarak bu anahtarı yeniden başlatmak üzere fonları istediği zaman harcayabilir veya taşıyabilir.
* **CScript**: `<heir_pubkey> OP_CHECKSIGVERIFY OP_IF <expiry> OP_CHECKLOCKTIMEVERIFY OP_DROP OP_ELSE <owner_pubkey> OP_CHECKSIGVERIFY OP_ENDIF`

```json
{
  "basic": {
    "Heir-Sig": {
      "role": "check-signature-verification",
      "inputs": [
        {
          "name": "Pubkey",
          "type": "pubkey",
          "value": "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f"
        }
      ]
    },
    "Lock-Time": {
      "role": "lock-time",
      "inputs": [
        {
          "name": "Lock-Until",
          "type": "timestamp-or-block-height",
          "value": 1780718400
        }
      ]
    },
    "Owner-Sig": {
      "role": "check-signature-verification",
      "inputs": [
        {
          "name": "Pubkey",
          "type": "pubkey",
          "value": "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"
        }
      ]
    }
  },
  "condition": {
    "Inheritance-Condition": {
      "role": "if-condition",
      "expressions": [
        { "type": "basic", "name": "Heir-Sig" }
      ],
      "true": [
        { "type": "basic", "name": "Lock-Time" }
      ],
      "false": [
        { "type": "basic", "name": "Owner-Sig" }
      ]
    }
  },
  "contract": {
    "Inheritance-Switch": {
      "description": "Allows heir to spend after lock time passes, otherwise owner can spend anytime.",
      "actions": [
        { "type": "condition", "name": "Inheritance-Condition" }
      ]
    }
  },
  "active_contract": "Inheritance-Switch"
}
```

---

### 2.4. Kullanım Senaryosu 4: Çok Faktörlü Kimlik Doğrulama (MFA) Harcama Kasası
* **Sorun**: Kullanıcı, yüksek değerli bir cüzdanı, tek bir cihazın ele geçirilmesine karşı ikinci bir faktör kimlik doğrulama imzası kullanarak güvence altına almak istemektedir.
* **Çözüm**: Küçük işlemler yalnızca kullanıcının birincil mobil cüzdan anahtarını gerektirir. Büyük işlemler veya yeni alıcılara yapılan işlemler, hem birincil mobil anahtardan hem de ikincil bir MFA sunucu anahtarından (2FA ortak imzalayıcısı olarak işlev görür) imza gerektirir.
* **CScript**: `2 <mobile_pubkey> <mfa_server_pubkey> 2 OP_CHECKMULTISIG`

```json
{
  "basic": {
    "MFA-CoSign-Multisig": {
      "role": "multi-signature",
      "inputs": [
        { "name": "m", "type": "number", "value": 2 },
        { "name": "n", "type": "number", "value": 2 },
        {
          "name": "Signatures",
          "type": "array",
          "value": [
            "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f",
            "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"
          ]
        }
      ]
    }
  },
  "contract": {
    "MFA-Spend-Vault": {
      "description": "Requires signatures from both primary mobile wallet and MFA security server to execute spends.",
      "actions": [
        { "type": "basic", "name": "MFA-CoSign-Multisig" }
      ]
    }
  },
  "active_contract": "MFA-Spend-Vault"
}
```

---

## 3. Kategori 2: Merkeziyetsiz Finans (DeFi) & Borç Verme (Lending)

### 3.1. Kullanım Senaryosu 5: Milat/Kilometre Taşı Tabanlı Proje Hakediş Emaneti (Vesting Escrow)
* **Sorun**: Bir yatırımcı bir geliştiriciyi fonlamak istemekte, ancak geliştiricinin tüm tutarla kaçmasını önlemek için fonları kilometre taşlarına göre kademeli olarak serbest bırakmak istemektedir.
* **Çözüm**: Sermaye bir sözleşmede kilitlenir. Geliştirici, belirli kilometre taşı tarihlerinden sonra fonları çekebilir, ancak bu yalnızca hem geliştiricinin hem de yatırımcının ortak imzası (co-sign) ile mümkündür. Bir kilometre taşı karşılanmazsa yatırımcı, geri alma (clawback) tarihinden sonra iade alabilir.
* **CScript**: `OP_IF <milestone_date> OP_CHECKLOCKTIMEVERIFY OP_DROP 2 <dev_pubkey> <investor_pubkey> 2 OP_CHECKMULTISIG OP_ELSE <investor_clawback_pubkey> OP_CHECKSIGVERIFY OP_ENDIF`

```json
{
  "basic": {
    "Dev-Investor-Multisig": {
      "role": "multi-signature",
      "inputs": [
        { "name": "m", "type": "number", "value": 2 },
        { "name": "n", "type": "number", "value": 2 },
        {
          "name": "Signatures",
          "type": "array",
          "value": [
            "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f",
            "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"
          ]
        }
      ]
    },
    "Milestone-Lock": {
      "role": "lock-time",
      "inputs": [
        {
          "name": "Lock-Until",
          "type": "timestamp-or-block-height",
          "value": 1780718400
        }
      ]
    },
    "Investor-Clawback-Sig": {
      "role": "check-signature-verification",
      "inputs": [
        {
          "name": "Pubkey",
          "type": "pubkey",
          "value": "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"
        }
      ]
    }
  },
  "condition": {
    "Milestone-Release-Branch": {
      "role": "if-condition",
      "expressions": [
        { "type": "basic", "name": "Milestone-Lock" }
      ],
      "true": [
        { "type": "basic", "name": "Dev-Investor-Multisig" }
      ],
      "false": [
        { "type": "basic", "name": "Investor-Clawback-Sig" }
      ]
    }
  },
  "contract": {
    "Vesting-Milestone-Contract": {
      "description": "After milestone lock date, developer and investor co-sign to release. Otherwise, investor claws back after failure.",
      "actions": [
        { "type": "condition", "name": "Milestone-Release-Branch" }
      ]
    }
  },
  "active_contract": "Vesting-Milestone-Contract"
}
```

---

### 3.2. Kullanım Senaryosu 6: Teminatlı Kredi Tasfiye Sözleşmesi
* **Sorun**: Bir borçlu, varlık ödünç almak için KRISTA teminatını kilitler. Kredi zamanında geri ödenmezse, borç veren teminatı tasfiye edebilmelidir.
* **Çözüm**: Borçlu geri ödeme yaparsa, hem borçlu hem de borç veren teminatı iade etmek üzere imza atar. Süre sınırı geçerse ve borçlu geri ödeme yapmamışsa, borç veren teminatı tek taraflı olarak tasfiye edebilir.
* **CScript**: `OP_IF 2 <borrower_pubkey> <lender_pubkey> 2 OP_CHECKMULTISIG OP_ELSE <expiry> OP_CHECKLOCKTIMEVERIFY OP_DROP <lender_pubkey> OP_CHECKSIGVERIFY OP_ENDIF`

```json
{
  "basic": {
    "Cooperative-Release-Multisig": {
      "role": "multi-signature",
      "inputs": [
        { "name": "m", "type": "number", "value": 2 },
        { "name": "n", "type": "number", "value": 2 },
        {
          "name": "Signatures",
          "type": "array",
          "value": [
            "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f",
            "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"
          ]
        }
      ]
    },
    "Lender-Liquidation-Sig": {
      "role": "check-signature-verification",
      "inputs": [
        {
          "name": "Pubkey",
          "type": "pubkey",
          "value": "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"
        }
      ]
    },
    "Loan-Duration-Lock": {
      "role": "lock-time",
      "inputs": [
        {
          "name": "Lock-Until",
          "type": "timestamp-or-block-height",
          "value": 1788489600
        }
      ]
    }
  },
  "condition": {
    "Settlement-Branch": {
      "role": "if-condition",
      "expressions": [
        { "type": "basic", "name": "Cooperative-Release-Multisig" }
      ],
      "true": [],
      "false": [
        { "type": "basic", "name": "Loan-Duration-Lock" },
        { "type": "basic", "name": "Lender-Liquidation-Sig" }
      ]
    }
  },
  "contract": {
    "Collateral-Liquidation-Vault": {
      "description": "Cooperative release refunds collateral. After 90 days, lender can unilaterally liquidate collateral.",
      "actions": [
        { "type": "condition", "name": "Settlement-Branch" }
      ]
    }
  },
  "active_contract": "Collateral-Liquidation-Vault"
}
```

---

### 3.3. Kullanım Senaryosu 7: Atomik Zincirler Arası Takas (HTLC)
* **Sorun**: Alice, güven ilişkisine ihtiyaç duymadan veya merkezi bir borsa kullanmadan KRISTA'yı Bob'un başka bir zincirdeki jetonları (tokens) ile takas etmek istemektedir.
* **Çözüm**: Alice coin'leri kilitler. Bob, bir hash hedefiyle eşleşen gizli ön görseli (preimage) sunarak bunları talep edebilir. Bob zaman aşımından önce bunları talep etmezse, Alice iade alır.
* **CScript**: `<hash> OP_HASH160 OP_IF <recipient_pubkey> OP_CHECKSIGVERIFY OP_ELSE <expiry> OP_CHECKLOCKTIMEVERIFY OP_DROP <sender_pubkey> OP_CHECKSIGVERIFY OP_ENDIF`

```json
{
  "basic": {
    "Preimage-Check": {
      "role": "hash160",
      "inputs": [
        {
          "name": "Hash160",
          "type": "string-or-number",
          "value": "b5a9c9f285d893ce71ab9de8f5c09d765ee982ba"
        }
      ]
    },
    "Recipient-Sig": {
      "role": "check-signature-verification",
      "inputs": [
        {
          "name": "Pubkey",
          "type": "pubkey",
          "value": "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f"
        }
      ]
    },
    "Timeout-Check": {
      "role": "lock-time",
      "inputs": [
        {
          "name": "Lock-Until",
          "type": "timestamp-or-block-height",
          "value": 1780718400
        }
      ]
    },
    "Sender-Sig": {
      "role": "check-signature-verification",
      "inputs": [
        {
          "name": "Pubkey",
          "type": "pubkey",
          "value": "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"
        }
      ]
    }
  },
  "condition": {
    "HTLC-Branch": {
      "role": "if-condition",
      "expressions": [
        { "type": "basic", "name": "Preimage-Check" }
      ],
      "true": [
        { "type": "basic", "name": "Recipient-Sig" }
      ],
      "false": [
        { "type": "basic", "name": "Timeout-Check" },
        { "type": "basic", "name": "Sender-Sig" }
      ]
    }
  },
  "contract": {
    "Atomic-Swap-HTLC": {
      "description": "Claimable immediately with secret preimage, or refundable to sender after timeout.",
      "actions": [
        { "type": "condition", "name": "HTLC-Branch" }
      ]
    }
  },
  "active_contract": "Atomic-Swap-HTLC"
}
```

---

### 3.4. Kullanım Senaryosu 8: Çift Depozitolu Güvenli Eşler Arası (P2P) Ticaret Emaneti
* **Sorun**: İki taraf mal ticareti yapmak istemekte ancak teslimat veya ödeme konusunda birbirine güvenmemektedir.
* **Çözüm**: Hem Alice hem de Bob, ürün maliyetini ve ek bir güvenlik teminatını yatırır. Fonları serbest bırakmak için her ikisinin de imzalaması gerekir. Biri hile yaparsa, her ikisi de depozitolarını kaybeder (dürüst davranışı teşvik eder). Sözleşmenin süresi dolarsa, bir arabulucu (mediator) konuyu çözene kadar teminat kilitlenir.
* **CScript**: `OP_IF 2 <alice_pubkey> <bob_pubkey> 2 OP_CHECKMULTISIG OP_ELSE <expiry> OP_CHECKLOCKTIMEVERIFY OP_DROP <mediator_pubkey> OP_CHECKSIGVERIFY OP_ENDIF`

```json
{
  "basic": {
    "Trade-Partners-Multisig": {
      "role": "multi-signature",
      "inputs": [
        { "name": "m", "type": "number", "value": 2 },
        { "name": "n", "type": "number", "value": 2 },
        {
          "name": "Signatures",
          "type": "array",
          "value": [
            "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f",
            "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"
          ]
        }
      ]
    },
    "Mediator-Refund-Sig": {
      "role": "check-signature-verification",
      "inputs": [
        {
          "name": "Pubkey",
          "type": "pubkey",
          "value": "02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234"
        }
      ]
    },
    "Escrow-Timeout": {
      "role": "lock-time",
      "inputs": [
        {
          "name": "Lock-Until",
          "type": "timestamp-or-block-height",
          "value": 1782273600
        }
      ]
    }
  },
  "condition": {
    "P2P-Trade-Branch": {
      "role": "if-condition",
      "expressions": [
        { "type": "basic", "name": "Trade-Partners-Multisig" }
      ],
      "true": [],
      "false": [
        { "type": "basic", "name": "Escrow-Timeout" },
        { "type": "basic", "name": "Mediator-Refund-Sig" }
      ]
    }
  },
  "contract": {
    "Double-Deposit-Escrow": {
      "description": "Cooperative release requires signatures from both trading partners. Disputes resolved by mediator after timeout.",
      "actions": [
        { "type": "condition", "name": "P2P-Trade-Branch" }
      ]
    }
  },
  "active_contract": "Double-Deposit-Escrow"
}
```

---

## 4. Kategori 3: E-Ticaret & Tedarik Zinciri

### 4.1. Kullanım Senaryosu 9: Teslimatta Ödemeli Tedarik Zinciri Emaneti (Escrow)
* **Sorun**: Bir alıcı, mal sevkiyatı için tedarikçiye ödeme yapmak istemekte; ancak ödemenin yalnızca nakliye taşıyıcısı malları teslim ettiğinde ve bir makbuz hash'i sağladığında gerçekleşmesini istemektedir.
* **Çözüm**: Alıcı ödemeyi kilitler. Tedarikçi, teslimat ön görselini (preimage) sunarak ödemeyi talep edebilir. Teslimat gerçekleşmezse, alıcı zaman aşımından sonra fonları geri alabilir.
* **CScript**: `OP_IF <proof_hash> OP_HASH160 OP_EQUALVERIFY <supplier_pubkey> OP_CHECKSIGVERIFY OP_ELSE <expiry> OP_CHECKLOCKTIMEVERIFY OP_DROP <buyer_pubkey> OP_CHECKSIGVERIFY OP_ENDIF`

```json
{
  "basic": {
    "Carrier-Receipt-Hash": {
      "role": "hash160",
      "inputs": [
        {
          "name": "Hash160",
          "type": "string-or-number",
          "value": "a5c9f285d893ce71ab9de8f5c09d765ee982ba34"
        }
      ]
    },
    "Supplier-Sig": {
      "role": "check-signature-verification",
      "inputs": [
        {
          "name": "Pubkey",
          "type": "pubkey",
          "value": "02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234"
        }
      ]
    },
    "Buyer-Refund-Sig": {
      "role": "check-signature-verification",
      "inputs": [
        {
          "name": "Pubkey",
          "type": "pubkey",
          "value": "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"
        }
      ]
    },
    "Delivery-Timeout": {
      "role": "lock-time",
      "inputs": [
        {
          "name": "Lock-Until",
          "type": "timestamp-or-block-height",
          "value": 1782014400
        }
      ]
    }
  },
  "condition": {
    "Delivery-Execution-Branch": {
      "role": "if-condition",
      "expressions": [
        { "type": "basic", "name": "Carrier-Receipt-Hash" }
      ],
      "true": [
        { "type": "basic", "name": "Supplier-Sig" }
      ],
      "false": [
        { "type": "basic", "name": "Delivery-Timeout" },
        { "type": "basic", "name": "Buyer-Refund-Sig" }
      ]
    }
  },
  "contract": {
    "Pay-On-Delivery-Escrow": {
      "description": "Supplier claims funds with delivery receipt. Buyer gets refund if delivery timeout passes.",
      "actions": [
        { "type": "condition", "name": "Delivery-Execution-Branch" }
      ]
    }
  },
  "active_contract": "Pay-On-Delivery-Escrow"
}
```

---

### 4.2. Kullanım Senaryosu 10: Abonelik & Tekrarlayan Üye İşyeri Çekim Yetkisi (Merchant Pull Authorization)
* **Sorun**: Bir müşteri, üye işyerine (merchant) sınırsız cüzdan erişimi vermeden, tekrarlayan aylık abonelik ödemeleri yapmak istemektedir.
* **Çözüm**: Müşteri fonları bir abonelik kasasına yatırır. Üye işyeri, müşteri imzasını sunarak belirli aylık aralıklarla abonelik tutarlarını çekebilir.
* **CScript**: `OP_IF <merchant_pubkey> OP_CHECKSIGVERIFY <monthly_lock> OP_CHECKLOCKTIMEVERIFY OP_DROP <customer_sig> OP_CHECKSIGVERIFY OP_ELSE <customer_refund_pubkey> OP_CHECKSIGVERIFY OP_ENDIF`

```json
{
  "basic": {
    "Merchant-Sig": {
      "role": "check-signature-verification",
      "inputs": [
        {
          "name": "Pubkey",
          "type": "pubkey",
          "value": "02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234"
        }
      ]
    },
    "Customer-Interval-Sig": {
      "role": "check-signature-verification",
      "inputs": [
        {
          "name": "Pubkey",
          "type": "pubkey",
          "value": "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f"
        }
      ]
    },
    "Interval-Time-Lock": {
      "role": "lock-time",
      "inputs": [
        {
          "name": "Lock-Until",
          "type": "timestamp-or-block-height",
          "value": 1783310400
        }
      ]
    },
    "Customer-Refund-Sig": {
      "role": "check-signature-verification",
      "inputs": [
        {
          "name": "Pubkey",
          "type": "pubkey",
          "value": "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f"
        }
      ]
    }
  },
  "condition": {
    "Pull-Payment-Branch": {
      "role": "if-condition",
      "expressions": [
        { "type": "basic", "name": "Merchant-Sig" },
        { "type": "basic", "name": "Interval-Time-Lock" }
      ],
      "true": [
        { "type": "basic", "name": "Customer-Interval-Sig" }
      ],
      "false": [
        { "type": "basic", "name": "Customer-Refund-Sig" }
      ]
    }
  },
  "contract": {
    "Subscription-Pull-Auth": {
      "description": "Merchant can withdraw subscription amounts after lock dates. Customer can refund balance anytime.",
      "actions": [
        { "type": "condition", "name": "Pull-Payment-Branch" }
      ]
    }
  },
  "active_contract": "Subscription-Pull-Auth"
}
```

---

### 4.3. Kullanım Senaryosu 11: Gayrimenkul Tapu Devri Emanet Hesabı (Escrow)
* **Sorun**: Bir alıcı gayrimenkul satın almak istemektedir. Alıcı parayı kilitleyecektir ancak paranın satıcıya yalnızca tapu dairesi VE bir noter tapu devrini onayladığında serbest bırakılmasını istemektedir.
* **Çözüm**: Alıcı fonları kilitler. Serbest bırakma işlemi; satıcının imzası VE noterin imzası VE tapu dairesinin imzasını (3'te 3 çoklu imza) gerektirir. Tapu devri başarısız olursa, alıcı 30 günlük bir zaman aşımından sonra iade alır.
* **CScript**: `OP_IF 3 <seller> <notary> <registry> 3 OP_CHECKMULTISIG OP_ELSE <expiry> OP_CHECKLOCKTIMEVERIFY OP_DROP <buyer> OP_CHECKSIGVERIFY OP_ENDIF`

```json
{
  "basic": {
    "Deed-Release-Multisig": {
      "role": "multi-signature",
      "inputs": [
        { "name": "m", "type": "number", "value": 3 },
        { "name": "n", "type": "number", "value": 3 },
        {
          "name": "Signatures",
          "type": "array",
          "value": [
            "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f",
            "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a",
            "02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234"
          ]
        }
      ]
    },
    "Buyer-Refund-Sig": {
      "role": "check-signature-verification",
      "inputs": [
        {
          "name": "Pubkey",
          "type": "pubkey",
          "value": "03fa32ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1239"
        }
      ]
    },
    "Purchase-Timeout": {
      "role": "lock-time",
      "inputs": [
        {
          "name": "Lock-Until",
          "type": "timestamp-or-block-height",
          "value": 1782273600
        }
      ]
    }
  },
  "condition": {
    "Deed-Transfer-Branch": {
      "role": "if-condition",
      "expressions": [
        { "type": "basic", "name": "Deed-Release-Multisig" }
      ],
      "true": [],
      "false": [
        { "type": "basic", "name": "Purchase-Timeout" },
        { "type": "basic", "name": "Buyer-Refund-Sig" }
      ]
    }
  },
  "contract": {
    "Real-Estate-Escrow": {
      "description": "Releases funds on notary + registry + seller co-signatures. Refunds buyer if transaction fails.",
      "actions": [
        { "type": "condition", "name": "Deed-Transfer-Branch" }
      ]
    }
  },
  "active_contract": "Real-Estate-Escrow"
}
```

---

### 4.4. Kullanım Senaryosu 12: Anlaşmazlık Çözümlü Kira Depozitosu Güvencesi
* **Sorun**: Kiracı bir teminat yatırır. Ev sahibi bunu çalamamalı, kiracı da hasar taleplerinden kaçamamalıdır.
* **Çözüm**: Güvence depozitosunun serbest bırakılması kiracı + ev sahibi ortak imzasını gerektirir. Bir anlaşmazlık durumunda, sertifikalı bir gayrimenkul arabulucusu serbest bırakmayı yetkilendirmek için taraflardan biriyle birlikte imza atabilir.
* **CScript**: `2 <tenant> <landlord> <mediator> 3 OP_CHECKMULTISIG`

```json
{
  "basic": {
    "Rental-Deposit-Quorum": {
      "role": "multi-signature",
      "inputs": [
        { "name": "m", "type": "number", "value": 2 },
        { "name": "n", "type": "number", "value": 3 },
        {
          "name": "Signatures",
          "type": "array",
          "value": [
            "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f",
            "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a",
            "02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234"
          ]
        }
      ]
    }
  },
  "contract": {
    "Rental-Security-Deposit": {
      "description": "Security deposit locked under 2-of-3 multisig between tenant, landlord, and mediator.",
      "actions": [
        { "type": "basic", "name": "Rental-Deposit-Quorum" }
      ]
    }
  },
  "active_contract": "Rental-Security-Deposit"
}
```

---

## 5. Kategori 4: Kurumsal Yönetişim & DAO

### 5.1. Kullanım Senaryosu 13: Kurumsal Harcama Yetkilendirmesi (5'te 3 Yönetim Kurulu Onayı)
* **Sorun**: Bir kurumsal hazine, fonların yalnızca yönetim kurulu üyelerinin çoğunluğu tarafından onaylandığında harcanabilmesini sağlamalıdır.
* **Çözüm**: Hazine fonları kilitlenir. Harcamalar, 5 yönetim kurulu üyesinden en az 3'ünün imzasını gerektirir.
* **CScript**: `3 <board_1> <board_2> <board_3> <board_4> <board_5> 5 OP_CHECKMULTISIG`

```json
{
  "basic": {
    "Board-Quorum-Multisig": {
      "role": "multi-signature",
      "inputs": [
        { "name": "m", "type": "number", "value": 3 },
        { "name": "n", "type": "number", "value": 5 },
        {
          "name": "Signatures",
          "type": "array",
          "value": [
            "03ab89ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1235",
            "02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1236",
            "03de76ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1237",
            "02ef54ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1238",
            "03fa32ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1239"
          ]
        }
      ]
    }
  },
  "contract": {
    "Corporate-Board-Approval": {
      "description": "Requires signatures from at least 3 out of 5 board members to approve treasury expenditures.",
      "actions": [
        { "type": "basic", "name": "Board-Quorum-Multisig" }
      ]
    }
  },
  "active_contract": "Corporate-Board-Approval"
}
```

---

### 5.2. Kullanım Senaryosu 14: Ağırlıklı Yönetim Kurulu Oylaması (Kademeli Kurumsal Yönetişim)
* **Sorun**: Farklı kurumsal yönetim kurulu üyeleri farklı oy ağırlıklarına sahiptir (örneğin Kurucular 3 oy, Yatırımcılar 2, Direktörler 1 oy hakkına sahiptir). Harcama yapmak için toplamda en az 5 oy eşiğinin sağlanması gerekmektedir.
* **Çözüm**: Anahtarları birden fazla çoklu imza (multisig) grubunda yapılandırırız veya oy ağırlığını modelleriz. MESCAL'de ağırlıklı bir hazine, oy ağırlığını karşılayan herhangi bir anahtar kombinasyonunun (iki Kurucu veya bir Kurucu + bir Yatırımcı + bir Direktör gibi) onaylayabildiği bir dizi koşullu çoklu imza ile temsil edilir.
* **CScript**: `OP_IF 2 <founder1> <founder2> 2 OP_CHECKMULTISIG OP_ELSE 3 <founder1> <investor> <director> 3 OP_CHECKMULTISIG OP_ENDIF`

```json
{
  "basic": {
    "Founders-Only-Sig": {
      "role": "multi-signature",
      "inputs": [
        { "name": "m", "type": "number", "value": 2 },
        { "name": "n", "type": "number", "value": 2 },
        {
          "name": "Signatures",
          "type": "array",
          "value": [
            "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f",
            "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"
          ]
        }
      ]
    },
    "Mixed-Governance-Quorum": {
      "role": "multi-signature",
      "inputs": [
        { "name": "m", "type": "number", "value": 3 },
        { "name": "n", "type": "number", "value": 3 },
        {
          "name": "Signatures",
          "type": "array",
          "value": [
            "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f",
            "03de76ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1237",
            "03fa32ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1239"
          ]
        }
      ]
    }
  },
  "condition": {
    "Weighted-Governance-Branch": {
      "role": "if-condition",
      "expressions": [
        { "type": "basic", "name": "Founders-Only-Sig" }
      ],
      "true": [],
      "false": [
        { "type": "basic", "name": "Mixed-Governance-Quorum" }
      ]
    }
  },
  "contract": {
    "Tiered-Corporate-Vault": {
      "description": "Requires two Founders, or a combined Founder + Investor + Director signature set to release funds.",
      "actions": [
        { "type": "condition", "name": "Weighted-Governance-Branch" }
      ]
    }
  },
  "active_contract": "Tiered-Corporate-Vault"
}
```

---

### 5.3. Kullanım Senaryosu 15: DAO Ragequit (Ödemesiz Dönem Çekim Koşulu)
* **Sorun**: Bir DAO üyesi, DAO kurulu tarafından kabul edilen bir teklife katılmamaktadır. Önerilen proje finansman işlemi gerçekleşmeden önce cüzdandaki payını çekmek ("ragequit" yapmak) istemektedir.
* **Çözüm**: DAO fonlama işlemleri 7 günlük bir ek süreyle kilitlenir. Bir DAO teklifi kabul edilirse, hazine fonları DAO kurulunun çoklu imzasıyla harcanabilir; ancak bu ancak 7 günlük bir gecikmeden sonra gerçekleşebilir. Bu 7 günlük zaman dilimi boyunca herhangi bir üye, kendi payını tek taraflı olarak çekmek için kendi kilitli token imzasını sunabilir.
* **CScript**: `OP_IF <member_withdrawal_sig> OP_CHECKSIGVERIFY OP_ELSE <7d_proposal_lock> OP_CHECKLOCKTIMEVERIFY OP_DROP <dao_board_multisig> OP_CHECKSIGVERIFY OP_ENDIF`

```json
{
  "basic": {
    "Member-Withdrawal-Sig": {
      "role": "check-signature-verification",
      "inputs": [
        {
          "name": "Pubkey",
          "type": "pubkey",
          "value": "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f"
        }
      ]
    },
    "DAO-Board-Multisig": {
      "role": "multi-signature",
      "inputs": [
        { "name": "m", "type": "number", "value": 3 },
        { "name": "n", "type": "number", "value": 5 },
        {
          "name": "Signatures",
          "type": "array",
          "value": [
            "03ab89ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1235",
            "02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1236",
            "03de76ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1237",
            "02ef54ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1238",
            "03fa32ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1239"
          ]
        }
      ]
    },
    "Proposal-Grace-Period": {
      "role": "lock-time",
      "inputs": [
        {
          "name": "Lock-Until",
          "type": "timestamp-or-block-height",
          "value": 1780977600
        }
      ]
    }
  },
  "condition": {
    "Ragequit-Branch": {
      "role": "if-condition",
      "expressions": [
        { "type": "basic", "name": "Member-Withdrawal-Sig" }
      ],
      "true": [],
      "false": [
        { "type": "basic", "name": "Proposal-Grace-Period" },
        { "type": "basic", "name": "DAO-Board-Multisig" }
      ]
    }
  },
  "contract": {
    "DAO-Grace-Period-Vault": {
      "description": "Allows individual members to ragequit and withdraw funds during proposal grace period. Otherwise, DAO executes spend.",
      "actions": [
        { "type": "condition", "name": "Ragequit-Branch" }
      ]
    }
  },
  "active_contract": "DAO-Grace-Period-Vault"
}
```

---

### 5.4. Kullanım Senaryosu 16: Denetçi Geçersiz Kılmalı (Auditor Override) Çift Emanetçili Hazine
* **Sorun**: Bir şirket hazinesini kilitler. Harcamalar CFO ve harici bir Denetçinin imzasını gerektirir. Denetçi çevrimdışı olursa veya meşru denetimleri imzalamayı reddederse hazine kilitli kalır.
* **Çözüm**: Standart harcamalar CFO + Denetçi ortak imzasını gerektirir. Ancak, denetim faaliyeti olmadan 6 aylık bir zaman aşımı süresi geçerse, CFO, Denetçiyi devre dışı bırakmak için CEO ile birlikte ortak imza atabilir.
* **CScript**: `OP_IF 2 <cfo> <auditor> 2 OP_CHECKMULTISIG OP_ELSE <6m_timeout> OP_CHECKLOCKTIMEVERIFY OP_DROP 2 <cfo> <ceo> 2 OP_CHECKMULTISIG OP_ENDIF`

```json
{
  "basic": {
    "CFO-Auditor-Multisig": {
      "role": "multi-signature",
      "inputs": [
        { "name": "m", "type": "number", "value": 2 },
        { "name": "n", "type": "number", "value": 2 },
        {
          "name": "Signatures",
          "type": "array",
          "value": [
            "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f",
            "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"
          ]
        }
      ]
    },
    "CFO-CEO-Bypass-Multisig": {
      "role": "multi-signature",
      "inputs": [
        { "name": "m", "type": "number", "value": 2 },
        { "name": "n", "type": "number", "value": 2 },
        {
          "name": "Signatures",
          "type": "array",
          "value": [
            "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f",
            "02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234"
          ]
        }
      ]
    },
    "Auditor-Timeout-Lock": {
      "role": "lock-time",
      "inputs": [
        {
          "name": "Lock-Until",
          "type": "timestamp-or-block-height",
          "value": 1793310400
        }
      ]
    }
  },
  "condition": {
    "Auditor-Bypass-Branch": {
      "role": "if-condition",
      "expressions": [
        { "type": "basic", "name": "CFO-Auditor-Multisig" }
      ],
      "true": [],
      "false": [
        { "type": "basic", "name": "Auditor-Timeout-Lock" },
        { "type": "basic", "name": "CFO-CEO-Bypass-Multisig" }
      ]
    }
  },
  "contract": {
    "Custodian-Audit-Lock": {
      "description": "Standard release requires CFO and Auditor signatures. Auditor can be bypassed by CEO + CFO after 6 months.",
      "actions": [
        { "type": "condition", "name": "Auditor-Bypass-Branch" }
      ]
    }
  },
  "active_contract": "Custodian-Audit-Lock"
}
```

---

## 6. Kategori 5: Oyun, Tahmin & Bahis

### 6.1. Kullanım Senaryosu 17: Eşler Arası (P2P) Tahmin Pazarı / Spor Bahisleri Emaneti
* **Sorun**: Alice ve Bob, merkezi bir bahis şirketi olmadan bir maçın sonucu üzerine bahis oynamak istemektedir.
* **Çözüm**: Alice ve Bob fonları 2'de 2 bir çıktıya yatırır. Kazananı bir Oracle belirler. Kazanan + Oracle ortak imzası fonları serbest bırakır. Oracle bir zaman aşımından sonra bildirimde bulunamazsa, depozitolar iade edilir.
* **CScript**: `OP_IF 2 <alice> <bob> <oracle> 3 OP_CHECKMULTISIG OP_ELSE <expiry> OP_CHECKLOCKTIMEVERIFY OP_DROP 2 <alice> <bob> 2 OP_CHECKMULTISIG OP_ENDIF`

```json
{
  "basic": {
    "Cooperative-Oracle-Multisig": {
      "role": "multi-signature",
      "inputs": [
        { "name": "m", "type": "number", "value": 2 },
        { "name": "n", "type": "number", "value": 3 },
        {
          "name": "Signatures",
          "type": "array",
          "value": [
            "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f",
            "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a",
            "02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234"
          ]
        }
      ]
    },
    "Player-Refund-Multisig": {
      "role": "multi-signature",
      "inputs": [
        { "name": "m", "type": "number", "value": 2 },
        { "name": "n", "type": "number", "value": 2 },
        {
          "name": "Signatures",
          "type": "array",
          "value": [
            "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f",
            "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"
          ]
        }
      ]
    },
    "Bet-Refund-Timeout": {
      "role": "lock-time",
      "inputs": [
        {
          "name": "Lock-Until",
          "type": "timestamp-or-block-height",
          "value": 1781102400
        }
      ]
    }
  },
  "condition": {
    "Oracle-Payout-Branch": {
      "role": "if-condition",
      "expressions": [
        { "type": "basic", "name": "Cooperative-Oracle-Multisig" }
      ],
      "true": [],
      "false": [
        { "type": "basic", "name": "Bet-Refund-Timeout" },
        { "type": "basic", "name": "Player-Refund-Multisig" }
      ]
    }
  },
  "contract": {
    "P2P-Prediction-Market": {
      "description": "Oracle validates and signs with winner to release funds. Split refund if Oracle fails to report.",
      "actions": [
        { "type": "condition", "name": "Oracle-Payout-Branch" }
      ]
    }
  },
  "active_contract": "P2P-Prediction-Market"
}
```

---

### 6.2. Kullanım Senaryosu 18: Zaman Kilitli Oyun Turnuvası Ödül Havuzu Emaneti
* **Sorun**: Bir oyun turnuvası organizatörü ödül havuzunu kilitler. Oyunculara kazananın ödeme alacağı garanti edilmeli ve turnuva başladıktan sonra organizatörün fonlarla kaçması engellenmelidir.
* **Çözüm**: Fonlar kilitlenir. Turnuva bittiğinde, turnuva hakemi anahtarı ve kazananın anahtarı fonları serbest bırakmak için imza atar. Hakem kazananı ilan edemezse, oyuncular ödül havuzunu bölüşmek için ortak imza (4'te 3 çoklu imza) atabilir.
* **CScript**: `OP_IF 2 <referee> <winner> 2 OP_CHECKMULTISIG OP_ELSE <tournament_end> OP_CHECKLOCKTIMEVERIFY OP_DROP 3 <player1> <player2> <player3> <player4> 4 OP_CHECKMULTISIG OP_ENDIF`

```json
{
  "basic": {
    "Referee-Winner-Multisig": {
      "role": "multi-signature",
      "inputs": [
        { "name": "m", "type": "number", "value": 2 },
        { "name": "n", "type": "number", "value": 2 },
        {
          "name": "Signatures",
          "type": "array",
          "value": [
            "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f",
            "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"
          ]
        }
      ]
    },
    "Player-Resolution-Multisig": {
      "role": "multi-signature",
      "inputs": [
        { "name": "m", "type": "number", "value": 3 },
        { "name": "n", "type": "number", "value": 4 },
        {
          "name": "Signatures",
          "type": "array",
          "value": [
            "03ab89ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1235",
            "02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1236",
            "03de76ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1237",
            "02ef54ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1238"
          ]
        }
      ]
    },
    "Tournament-End-Lock": {
      "role": "lock-time",
      "inputs": [
        {
          "name": "Lock-Until",
          "type": "timestamp-or-block-height",
          "value": 1782273600
        }
      ]
    }
  },
  "condition": {
    "Tournament-Payout-Branch": {
      "role": "if-condition",
      "expressions": [
        { "type": "basic", "name": "Referee-Winner-Multisig" }
      ],
      "true": [],
      "false": [
        { "type": "basic", "name": "Tournament-End-Lock" },
        { "type": "basic", "name": "Player-Resolution-Multisig" }
      ]
    }
  },
  "contract": {
    "Gaming-Prize-Pool-Escrow": {
      "description": "Releases prize pool to winner with referee signature. Players resolve and split if referee fails to report after tournament end.",
      "actions": [
        { "type": "condition", "name": "Tournament-Payout-Branch" }
      ]
    }
  },
  "active_contract": "Gaming-Prize-Pool-Escrow"
}
```

---

## 7. Kategori 6: Akıllı Altyapı & Otomatik Sistemler (IoT/Oracles)

### 7.1. Kullanım Senaryosu 19: Elektrikli Araç (EV) Şarj İstasyonu / IoT Kullandıkça Öde Kilidi
* **Sorun**: Bir elektrikli araç (EV) şarj istasyonu, araca yalnızca ödeme kanıtı olarak bir ödeme hash ön görseli (preimage) sunulduğunda güç vermelidir.
* **Çözüm**: Şarj istasyonunun hedef bir hash kilidi vardır. Kullanıcı, ödeme coin'lerini sözleşmede kilitler. EV şarj istasyonu gücü serbest bırakır ve gizli ön görseli blockchain üzerinde yayınlayarak (aynı zamanda makbuz görevi görür) coin'leri tek taraflı olarak çeker.
* **CScript**: `<invoice_hash> OP_HASH160 OP_EQUALVERIFY <charging_station_pubkey> OP_CHECKSIGVERIFY`

```json
{
  "basic": {
    "Paid-Invoice-Hash": {
      "role": "hash160",
      "inputs": [
        {
          "name": "Hash160",
          "type": "string-or-number",
          "value": "b5a9c9f285d893ce71ab9de8f5c09d765ee982ba"
        }
      ]
    },
    "Charging-Station-Sig": {
      "role": "check-signature-verification",
      "inputs": [
        {
          "name": "Pubkey",
          "type": "pubkey",
          "value": "02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234"
        }
      ]
    }
  },
  "contract": {
    "IoT-EV-Pay-Per-Use": {
      "description": "Locks funds. Charging station sweeps the payment unilaterally upon providing the invoice preimage.",
      "actions": [
        { "type": "basic", "name": "Paid-Invoice-Hash" },
        { "type": "basic", "name": "Charging-Station-Sig" }
      ]
    }
  },
  "active_contract": "IoT-EV-Pay-Per-Use"
}
```

---

### 7.2. Kullanım Senaryosu 20: Parametrik Uçuş Gecikme Sigortası Talebi
* **Sorun**: Bir yolcu uçuş gecikme sigortası satın alır. Yolcu, uçuş ertelenirse manuel talepler veya bürokrasi olmaksızın otomatik, anında bir ödeme almak ister.
* **Çözüm**: Sigorta havuzu ödemeyi kilitler. Bağımsız uçuş verisi Oracle'ı uçuş durumunu imzalar. Eğer Oracle gecikmeyi kanıtlayan bir sertifika yayınlarsa, yolcu sigorta ödemesini tek taraflı olarak çeker. Uçuş zamanında gerçekleşirse, sigorta şirketi uçuş tarihinden sonra iade alır.
* **CScript**: `OP_IF <oracle_delay_hash> OP_HASH160 OP_EQUALVERIFY <passenger_pubkey> OP_CHECKSIGVERIFY OP_ELSE <flight_date> OP_CHECKLOCKTIMEVERIFY OP_DROP <insurance_pubkey> OP_CHECKSIGVERIFY OP_ENDIF`

```json
{
  "basic": {
    "Flight-Delay-Oracle-Hash": {
      "role": "hash160",
      "inputs": [
        {
          "name": "Hash160",
          "type": "string-or-number",
          "value": "b5a9c9f285d893ce71ab9de8f5c09d765ee982ba"
        }
      ]
    },
    "Passenger-Sig": {
      "role": "check-signature-verification",
      "inputs": [
        {
          "name": "Pubkey",
          "type": "pubkey",
          "value": "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f"
        }
      ]
    },
    "Flight-Time-Lock": {
      "role": "lock-time",
      "inputs": [
        {
          "name": "Lock-Until",
          "type": "timestamp-or-block-height",
          "value": 1782273600
        }
      ]
    },
    "Insurance-Refund-Sig": {
      "role": "check-signature-verification",
      "inputs": [
        {
          "name": "Pubkey",
          "type": "pubkey",
          "value": "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"
        }
      ]
    }
  },
  "condition": {
    "Insurance-Claim-Branch": {
      "role": "if-condition",
      "expressions": [
        { "type": "basic", "name": "Flight-Delay-Oracle-Hash" }
      ],
      "true": [
        { "type": "basic", "name": "Passenger-Sig" }
      ],
      "false": [
        { "type": "basic", "name": "Flight-Time-Lock" },
        { "type": "basic", "name": "Insurance-Refund-Sig" }
      ]
    }
  },
  "contract": {
    "Parametric-Insurance": {
      "description": "Passenger claims payout immediately if delay certificate is posted. Insurer receives refund if flight is on schedule.",
      "actions": [
        { "type": "condition", "name": "Insurance-Claim-Branch" }
      ]
    }
  },
  "active_contract": "Parametric-Insurance"
}
```

---

## 8. MESCAL Kullanışlılığının Özeti

Bu 20 kapsamlı kullanım senaryosunun gösterdiği gibi, MESCAL kısıtlayıcı bir dil olmaktan çok uzaktır. Yığın tabanlı (stack-based) script'in deterministik ve güvenli yapısı ile JSON'un yapılandırılmış netliği birleştirilerek şunlar sağlanır:

1. **Tasarım Gereği Güvenlik (Security-by-Design)**: Yürütme yolları statik olarak bildirildiği ve zincir dışında (off-chain) doğrulandığı için re-entrancy vektörlerini tamamen ortadan kaldırır.
2. **Sonsuz Mantıksal Birleştirilebilirlik (Infinite Logic Composability)**: Geliştiriciler; emanet sistemlerini, güvene dayalı mülkleri, ödeme akışlarını, kurtarma ağlarını ve tedarik zincirlerini modellemek için temel blokları ve iç içe geçmiş koşulları birbirine bağlayabilir.
3. **Sorunsuz Entegrasyon (Frictionless Integration)**: JSON'un bildirimsel yapısı, istemci cüzdanlarının çalışma anında (on the fly) script'leri derlemesini, kullanıcılardan imza istemesini ve şablonları doğrudan KRISTA ağına göndermesini kolaylaştırır.
