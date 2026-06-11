# MESCAL Use Cases & Smart Contract Templates

**Minimalistically Envisioned Smart Contract Assembling Language**  
*A Compendium of Practical, Enterprise, and Security Smart Contracts*

---

## 1. Introduction

A common misconception regarding declarative, JSON-based smart contract specifications is that they are too rigid or narrow in scope compared to Turing-complete programming languages. In reality, MESCAL (Minimalistically Envisioned Smart Contract Assembling Language) provides an incredibly rich, expressive, and secure framework for defining transactions. 

By leveraging Bitcoin-style stack operators (`CScript`) via structured, machine-parsable JSON representations, MESCAL can express complex conditional branches, multi-signature governance, time-locked covenants, and cryptographic hash locks. Because MESCAL avoids state-based loop iterations, all contracts are immune to re-entrancy bugs, out-of-gas failures, and compiler-level optimization side effects.

This document serves as a repository of production-ready MESCAL smart contract use cases, proving the language's utility across Decentralized Finance (DeFi), Personal Custody, Corporate Governance, Gaming, and Supply Chain Management.

---

## 2. Use Cases

### 2.1. Use Case 1: Time-Delayed Vault with Emergency Recovery (Anti-Theft)
* **Domain**: Personal Security & Self-Custody
* **Problem**: If an attacker gains access to a user's mobile (hot) wallet, they can instantly drain all funds. The user wants a safety net.
* **Solution**: Funds are locked in a vault. A withdrawal can be initiated using the daily hot wallet key, but it is locked for a 72-hour delay. During this 72-hour window, the user can use their offline cold wallet key (stored securely on paper/hardware) to cancel the withdrawal and reclaim the funds immediately.
* **CScript Equivalent**: 
  `OP_IF <cold_pubkey> OP_CHECKSIGVERIFY OP_ELSE <72h_delay> OP_CHECKLOCKTIMEVERIFY OP_DROP <hot_pubkey> OP_CHECKSIGVERIFY OP_ENDIF`

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
      "description": "Cold wallet can recover funds instantly at any time. Hot wallet can withdraw only after a 72-hour delay.",
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
* **Domain**: Personal Security & Self-Custody
* **Problem**: If the owner loses their private key, the funds are lost forever. The owner does not want to trust a centralized custodian.
* **Solution**: The owner's signature can spend funds at any time. If the owner's key is lost, a group of 5 designated "Guardians" (trusted friends, hardware devices, or institutions) can sign a recovery transaction. If 3 of the 5 guardians sign, the funds can be moved, but only after a 30-day delay, giving the owner time to cancel the recovery if a subset of guardians colludes maliciously.
* **CScript Equivalent**: 
  `OP_IF <owner_pubkey> OP_CHECKSIGVERIFY OP_ELSE 3 <guardian1> <guardian2> <guardian3> <guardian4> <guardian5> 5 OP_CHECKMULTISIGVERIFY <30d_delay> OP_CHECKLOCKTIMEVERIFY OP_DROP OP_ENDIF`

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

### 2.3. Use Case 3: Milestone-Based Project Vesting Escrow
* **Domain**: Corporate Finance & Tokenomics
* **Problem**: An investor wants to fund a startup, but wishes to release funds gradually based on milestones or time-lock periods to prevent the startup from running away with the entire sum.
* **Solution**: The investment capital is locked in a contract. The startup can withdraw 30% of the funds after 3 months, 30% after 6 months, and the remaining 40% after 9 months, but only if a multi-signature board (containing investor and developer keys) signs off on the milestone completion. If the project fails to meet milestones, the investor can claw back the remaining locked capital after a designated duration.
* **CScript Equivalent (Milestone 1 example)**: 
  `OP_IF <milestone_1_expiry> OP_CHECKLOCKTIMEVERIFY OP_DROP 2 <startup_pubkey> <investor_pubkey> 2 OP_CHECKMULTISIG OP_ELSE <investor_clawback_pubkey> OP_CHECKSIGVERIFY OP_ENDIF`

```json
{
  "basic": {
    "Startup-Investor-Multisig": {
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
    "Milestone-1-Lock": {
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
        { "type": "basic", "name": "Milestone-1-Lock" }
      ],
      "true": [
        { "type": "basic", "name": "Startup-Investor-Multisig" }
      ],
      "false": [
        { "type": "basic", "name": "Investor-Clawback-Sig" }
      ]
    }
  },
  "contract": {
    "Vesting-Milestone-Contract": {
      "description": "After milestone lock date, startup and investor can co-sign to withdraw. Otherwise, investor can claw back after failure.",
      "actions": [
        { "type": "condition", "name": "Milestone-Release-Branch" }
      ]
    }
  },
  "active_contract": "Vesting-Milestone-Contract"
}
```

---

### 2.4. Use Case 4: Pay-on-Delivery Supply Chain Escrow
* **Domain**: Enterprise & Supply Chain Management
* **Problem**: A buyer wants to pay a supplier for a shipment of goods, but only wants the payment to clear once the shipping carrier delivers the goods and provides a receipt hash.
* **Solution**: The buyer locks the payment in a contract. The supplier can claim the payment immediately by presenting a secret proof-of-delivery preimage matching a target Hash160 generated by the carrier upon delivery. If delivery does not occur within a 15-day window, the buyer can reclaim the funds.
* **CScript Equivalent**: 
  `OP_IF <proof_hash> OP_HASH160 OP_EQUALVERIFY <supplier_pubkey> OP_CHECKSIGVERIFY OP_ELSE <15d_expiry> OP_CHECKLOCKTIMEVERIFY OP_DROP <buyer_pubkey> OP_CHECKSIGVERIFY OP_ENDIF`

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
      "description": "Supplier can claim funds by providing carrier receipt preimage. Buyer gets refund if delivery timeout passes.",
      "actions": [
        { "type": "condition", "name": "Delivery-Execution-Branch" }
      ]
    }
  },
  "active_contract": "Pay-On-Delivery-Escrow"
}
```

---

### 2.5. Use Case 5: Decentralized Prediction Market / P2P Betting
* **Domain**: Gaming & Entertainment
* **Problem**: Alice and Bob want to bet on the outcome of a sports match or election without trusting a centralized betting platform.
* **Solution**: Alice and Bob both contribute equal funds to a 2-of-2 multisig output. They agree on a neutral Oracle who will sign off on the correct winner. If Alice wins, the Oracle and Alice sign to release the funds to Alice. If Bob wins, the Oracle and Bob sign to release them to Bob. If the Oracle fails to report the result after a timeout, the contract permits both parties to retrieve their initial deposits.
* **CScript Equivalent**: 
  `OP_IF 2 <alice_pubkey> <bob_pubkey> <oracle_pubkey> 3 OP_CHECKMULTISIG OP_ELSE <refund_timeout> OP_CHECKLOCKTIMEVERIFY OP_DROP 2 <alice_pubkey> <bob_pubkey> 2 OP_CHECKMULTISIG OP_ENDIF`

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
      "description": "Allows payout to winner based on Oracle validation. Permits split refund if Oracle fails to report.",
      "actions": [
        { "type": "condition", "name": "Oracle-Payout-Branch" }
      ]
    }
  },
  "active_contract": "P2P-Prediction-Market"
}
```

---

### 2.6. Use Case 6: Subscriptions & Recurring Merchant Pull Authorization
* **Domain**: Merchant Payments & E-Commerce
* **Problem**: A customer wants to sign up for a monthly subscription. They do not want the merchant to have unlimited access to their wallet, but they want the convenience of automatic monthly payments.
* **Solution**: The customer deposits a fixed amount of coins into a subscription vault. Every month, the merchant can pull a fixed billing amount by presenting a customer signature bound to the specific time-lock interval. The customer can cancel and withdraw the remaining balance at any time if the merchant fails to provide the service.
* **CScript Equivalent**: 
  `OP_IF <merchant_pubkey> OP_CHECKSIGVERIFY <monthly_interval_lock> OP_CHECKLOCKTIMEVERIFY OP_DROP <customer_interval_sig> OP_CHECKSIGVERIFY OP_ELSE <customer_refund_pubkey> OP_CHECKSIGVERIFY OP_ENDIF`

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
      "description": "Merchant can withdraw subscription amounts after lock dates with customer signature. Customer can refund remaining balance anytime.",
      "actions": [
        { "type": "condition", "name": "Pull-Payment-Branch" }
      ]
    }
  },
  "active_contract": "Subscription-Pull-Auth"
}
```

---

### 2.7. Use Case 7: Joint Venture Corporate Spend Authorization (3-of-5 Board Approval)
* **Domain**: Corporate Governance & Multi-Sig Custody
* **Problem**: A corporate treasury has five board members. Any expenditure must be approved by a majority of the board to prevent rogue spending.
* **Solution**: The corporate treasury UTXO is locked with a 3-of-5 multisig template. If three board members sign, they can release the funds. 
* **CScript Equivalent**: 
  `3 <board_1> <board_2> <board_3> <board_4> <board_5> 5 OP_CHECKMULTISIG`

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

### 2.8. Use Case 8: Collateralized Loan Liquidation Contract
* **Domain**: Decentralized Finance (DeFi) & Lending
* **Problem**: A borrower wants to borrow stablecoins by locking up KRISTA as collateral. The lender wants to be guaranteed that if the borrower does not repay the loan within 90 days, the lender can liquidate the collateral.
* **Solution**: The borrower locks KRISTA collateral in a contract. If the borrower repays the lender, the lender co-signs a release to refund the collateral to the borrower. If the 90-day time lock passes and the loan is unpaid, the lender can liquidate the collateral unilaterally.
* **CScript Equivalent**: 
  `OP_IF 2 <borrower_pubkey> <lender_pubkey> 2 OP_CHECKMULTISIG OP_ELSE <90d_expiry> OP_CHECKLOCKTIMEVERIFY OP_DROP <lender_pubkey> OP_CHECKSIGVERIFY OP_ENDIF`

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
      "description": "Cooperative release refunds collateral to borrower. After 90 days, lender can unilaterally liquidate collateral.",
      "actions": [
        { "type": "condition", "name": "Settlement-Branch" }
      ]
    }
  },
  "active_contract": "Collateral-Liquidation-Vault"
}
```

---

## 3. Summary of MESCAL Utility

As shown by these diverse use cases, MESCAL is far from a restrictive language. By combining the deterministic and secure nature of stack-based script with the structured clarity of JSON:

1. **Security-by-Design**: Deletes re-entrancy vectors entirely, since execution paths are statically declared and validated off-chain.
2. **Infinite Logic Composability**: Developers can chain basic blocks and nested conditions together to model escrow systems, trust estates, payment streams, recovery networks, and supply chains.
3. **Frictionless Integration**: The declarativeness of JSON makes it easy for client wallets to compile scripts on the fly, prompt users for signatures, and submit templates directly to the KRISTA network.
