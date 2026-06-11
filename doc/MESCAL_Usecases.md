# MESCAL Use Cases & Smart Contract Templates

**Minimalistically Envisioned Smart Contract Assembling Language**  
*A Compendium of Practical, Enterprise, and Security Smart Contracts*

---

## 1. Introduction

A common misconception regarding declarative, JSON-based smart contract specifications is that they are too rigid or narrow in scope compared to Turing-complete programming languages. In reality, MESCAL (Minimalistically Envisioned Smart Contract Assembling Language) provides an incredibly rich, expressive, and secure framework for defining transactions. 

By leveraging Bitcoin-style stack operators (`CScript`) via structured, machine-parsable JSON representations, MESCAL can express complex conditional branches, multi-signature governance, time-locked covenants, and cryptographic hash locks. Because MESCAL avoids state-based loop iterations, all contracts are immune to re-entrancy bugs, out-of-gas failures, and compiler-level optimization side effects.

This document serves as a comprehensive repository of 20 production-ready MESCAL smart contract use cases, proving the language's utility across Personal Security, Decentralized Finance (DeFi), E-Commerce, Corporate Governance, Gaming, and IoT/Oracle-driven automation.

---

## 2. Category 1: Personal Security & Custody

### 2.1. Use Case 1: Time-Delayed Vault with Emergency Recovery (Anti-Theft)
* **Problem**: If an attacker steals a user's mobile (hot) wallet, they can instantly drain all funds. The user wants a safety net.
* **Solution**: Funds are locked in a vault. A withdrawal can be initiated using the daily hot wallet key, but it is locked for a 72-hour delay. During this 72-hour window, the user can use their offline cold wallet key to cancel the withdrawal and reclaim the funds immediately.
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

### 2.2. Use Case 2: Social Recovery Wallet (Cooperative Guardians)
* **Problem**: If the owner loses their private key, the funds are lost forever. The owner does not want to trust a centralized custodian.
* **Solution**: The owner's signature can spend funds at any time. If the owner's key is lost, a group of 5 designated "Guardians" (trusted friends or devices) can sign a recovery transaction. If 3 of the 5 guardians sign, the funds can be moved, but only after a 30-day delay, giving the owner time to cancel the recovery if a subset of guardians colludes maliciously.
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

### 2.3. Use Case 3: Time-Bounded Inheritance Switch (Dead Man's Switch)
* **Problem**: A user wants their heirs to receive their crypto assets in the event of their passing or long-term absence, but does not want heirs to access the funds while the user is active.
* **Solution**: The heir's signature can spend funds only after a long-term time-lock (e.g. 1 year) has expired. The owner can spend or move the funds at any time, resetting the switch.
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

### 2.4. Use Case 4: Multi-Factor Authentication (MFA) Spend Vault
* **Problem**: The user wants to secure a high-value wallet against single-device compromise using a second-factor authentication signature.
* **Solution**: Small transactions require only the user's primary mobile wallet key. Large transactions, or transactions to new recipients, require signatures from both the primary mobile key and a secondary MFA server key (acting as a 2FA co-signer).
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

## 3. Category 2: Decentralized Finance (DeFi) & Lending

### 3.1. Use Case 5: Milestone-Based Project Vesting Escrow
* **Problem**: An investor wants to fund a developer, but wishes to release funds gradually based on milestones to prevent the developer from running away with the entire sum.
* **Solution**: The capital is locked in a contract. The developer can withdraw funds after specific milestone deadlines, but only if both developer and investor co-sign. If a milestone is not met, the investor can retrieve a refund after a clawback date.
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

### 3.2. Use Case 6: Collateralized Loan Liquidation Contract
* **Problem**: A borrower locks KRISTA collateral to borrow assets. If the loan is not repaid on time, the lender must be able to liquidate the collateral.
* **Solution**: If the borrower repays, both borrower and lender sign to return the collateral. If the time limit passes and the borrower has not repaid, the lender can liquidate the collateral unilaterally.
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

### 3.3. Use Case 7: Atomic Cross-Chain Swap (HTLC)
* **Problem**: Alice wants to swap KRISTA with Bob's tokens on another chain without trust or a centralized exchange.
* **Solution**: Alice locks coins. Bob can claim them by presenting the secret preimage matching a hash target. If Bob does not claim them before the timeout, Alice gets a refund.
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

### 3.4. Use Case 8: Double-Deposit Secure Peer-to-Peer Trade Escrow
* **Problem**: Two parties want to trade goods but do not trust each other to deliver or pay.
* **Solution**: Both Alice and Bob deposit the item cost plus a security collateral. Both must sign to release the funds. If one cheats, both lose their deposits (incentivizing honest behavior). If the contract expires, the collateral is locked until a mediator resolves it.
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

## 4. Category 3: E-Commerce & Supply Chain

### 4.1. Use Case 9: Pay-on-Delivery Supply Chain Escrow
* **Problem**: A buyer wants to pay a supplier for a shipment of goods, but only wants the payment to clear once the shipping carrier delivers the goods and provides a receipt hash.
* **Solution**: The buyer locks the payment. The supplier can claim it by presenting the delivery preimage. If delivery does not occur, the buyer can reclaim the funds after timeout.
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

### 4.2. Use Case 10: Subscription & Recurring Merchant Pull Authorization
* **Problem**: A customer wants recurring monthly subscription payments without giving the merchant unlimited wallet access.
* **Solution**: The customer deposits funds into a subscription vault. The merchant can pull subscription amounts after specific monthly intervals by presenting a customer signature.
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

### 4.3. Use Case 11: Real Estate Title Deed Transfer Escrow
* **Problem**: A buyer wants to purchase real estate. The buyer will lock the money, but wants it released to the seller only when a land registry office AND a notary confirm the deed transfer.
* **Solution**: Buyer locks funds. Release requires the seller's signature AND the notary's signature AND the land registry's signature (a 3-of-3 multisig). If title deed transfer fails, the buyer receives a refund after a 30-day timeout.
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

### 4.4. Use Case 12: Rental Security Deposit Trust with Dispute Resolution
* **Problem**: A tenant deposits collateral. Landlord should not be able to steal it, nor tenant run away from damage claims.
* **Solution**: Release of security deposit requires tenant + landlord co-signature. If there is a dispute, a certified property mediator can sign with either party to authorize release.
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

## 5. Category 4: Corporate Governance & DAO

### 5.1. Use Case 13: Corporate Spend Authorization (3-of-5 Board Approval)
* **Problem**: A corporate treasury must ensure that funds can only be spent when approved by a majority of the board members.
* **Solution**: The treasury funds are locked. Releases require signatures from at least 3 out of the 5 board members.
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

### 5.2. Use Case 14: Weighted Board Voting (Tiered Corporate Governance)
* **Problem**: Different corporate board members hold different voting weights (e.g. Founders hold 3 votes, Investors hold 2, Directors hold 1). Spending requires a cumulative voting threshold of 5.
* **Solution**: We configure keys in multiple multisig groups or model voting weight. A weighted treasury in MESCAL is represented by a set of conditional multisigs, where any combination of keys satisfying the voting weight (such as two Founders, or one Founder + one Investor + one Director) can sign off.
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

### 5.3. Use Case 15: DAO Ragequit (Grace Period Withdrawal Covenant)
* **Problem**: A DAO member disagrees with a proposal passed by the DAO board. They want to "ragequit" and withdraw their locked share of treasury assets before the proposed project funding transaction goes through.
* **Solution**: DAO funding transactions are locked with a 7-day grace period. If a DAO proposal passes, the treasury funds can be spent by the DAO board's multisig, but only after a 7-day delay. During this 7-day window, any member can present their individual locked token signature to withdraw their proportional share unilaterally.
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

### 5.4. Use Case 16: Dual-Custodian Treasury with Auditor Override
* **Problem**: A corporation locks its treasury. Spends require signatures from the CFO and an external Auditor. If the auditor goes offline or refuses to sign legitimate audits, the treasury is locked.
* **Solution**: Standard spends require CFO + Auditor co-signature. However, if a 6-month timeout passes without auditing activity, the CFO can co-sign with the CEO to bypass the Auditor.
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

## 6. Category 5: Gaming, Prediction & Betting

### 6.1. Use Case 17: P2P Prediction Market / Sports Betting Escrow
* **Problem**: Alice and Bob want to bet on the outcome of a match without a centralized bookmaker.
* **Solution**: Alice and Bob deposit funds into a 2-of-2 output. An Oracle decides the winner. Winner + Oracle co-signature releases the funds. If the Oracle fails to report after a timeout, the deposits are refunded.
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

### 6.2. Use Case 18: Time-Locked Gaming Tournament Prize Pool Escrow
* **Problem**: A gaming tournament organizer locks a prize pool. Players must be guaranteed that the winner gets paid, and the organizer cannot run away with the funds once the tournament starts.
* **Solution**: Funds are locked. Once the tournament ends, the tournament referee key and the winner key sign to release the funds. If the referee fails to declare a winner, the players can co-sign (3-of-4 multisig) to split the prize pool.
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

## 7. Category 6: Smart Infrastructure & Automated Systems (IoT/Oracles)

### 7.1. Use Case 19: EV Charging Station / IoT Pay-per-Use Lock
* **Problem**: An electric vehicle (EV) charging station should release power to a vehicle only when a payment hash preimage is presented as proof of payment.
* **Solution**: The charging station has a target hash lock. The user locks payment coins in the contract. The EV charging station releases power and retrieves the coins unilaterally by publishing the secret preimage to the blockchain, serving as a receipt.
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

### 7.2. Use Case 20: Parametric Flight Delay Insurance Claim
* **Problem**: A passenger purchases flight delay insurance. The passenger wants an automated, instant payout if the flight is delayed, without manual claims or bureaucracy.
* **Solution**: The insurance pool locks the payout. An independent flight data Oracle signs flight status. If the Oracle publishes a certificate proving delay, the passenger sweeps the insurance payout unilaterally. If the flight operates on time, the insurance company retrieves a refund after the flight date.
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

## 8. Summary of MESCAL Utility

As demonstrated by these 20 comprehensive use cases, MESCAL is far from a restrictive language. By combining the deterministic and secure nature of stack-based script with the structured clarity of JSON:

1. **Security-by-Design**: Deletes re-entrancy vectors entirely, since execution paths are statically declared and validated off-chain.
2. **Infinite Logic Composability**: Developers can chain basic blocks and nested conditions together to model escrow systems, trust estates, payment streams, recovery networks, and supply chains.
3. **Frictionless Integration**: The declarativeness of JSON makes it easy for client wallets to compile scripts on the fly, prompt users for signatures, and submit templates directly to the KRISTA network.
