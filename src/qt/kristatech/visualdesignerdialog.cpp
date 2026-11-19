// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/visualdesignerdialog.h"
#include "qt/kristatech/qtutils.h"

#include "hash.h"
#include "main.h"
#include "script/mescal.h"
#include "utilstrencodings.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QFrame>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegExp>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace {

static const qreal NODE_WIDTH = 340.0;
static const qreal CANVAS_WIDTH = 520.0;
static const qreal FLOW_X = (CANVAS_WIDTH - NODE_WIDTH) * 0.5;
static const qreal NODE_V_GAP = 36.0;

CPubKey GetDefaultWalletPubkeyCached()
{
    static CPubKey cachedKey;
    if (cachedKey.IsValid()) return cachedKey;
#ifdef ENABLE_WALLET
    if (pwalletMain) {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        CPubKey pubKey;
        if (pwalletMain->GetKeyFromPool(pubKey) && pubKey.IsValid()) {
            cachedKey = pubKey;
            return cachedKey;
        }
    }
#endif
    return CPubKey();
}

QString GetDefaultWalletPubkeyHex()
{
    CPubKey pubKey = GetDefaultWalletPubkeyCached();
    if (pubKey.IsValid()) {
        return QString::fromStdString(HexStr(pubKey.begin(), pubKey.end()));
    }
    return QStringLiteral("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
}

QString GetDefaultWalletPubkeyHashHex()
{
    CPubKey pubKey = GetDefaultWalletPubkeyCached();
    if (pubKey.IsValid()) {
        CKeyID id = pubKey.GetID();
        return QString::fromStdString(HexStr(id.begin(), id.end()));
    }
    return QStringLiteral("b5a9c9f285d893ce71ab9de8f5c09d765ee982ba");
}

int GetCurrentChainHeight()
{
    LOCK(cs_main);
    return std::max(0, chainActive.Height());
}

QString GetTipHashHex()
{
    LOCK(cs_main);
    if (chainActive.Tip()) {
        return QString::fromStdString(chainActive.Tip()->GetBlockHash().GetHex());
    }
    return QStringLiteral("0000000000000000000000000000000000000000000000000000000000000000");
}

int64_t GetDefaultLockHeight()
{
    int h = GetCurrentChainHeight();
    return (h > 0) ? (h + 100) : 150;
}

QString ComputeRawHash160Hex(const QString& utf8Secret)
{
    const QByteArray bytes = utf8Secret.toUtf8();
    std::vector<unsigned char> vch(bytes.begin(), bytes.end());
    uint160 h = Hash160(vch);
    return QString::fromStdString(HexStr(h.begin(), h.end()));
}

QString ShortenDisplayValue(const QString& s)
{
    if (s.length() > 26 && !s.contains(' ') && !s.contains('\n')) {
        return s.left(11) + QStringLiteral("...") + s.right(9);
    }
    return s;
}

void RoleMetadata(const QString& role, QString& titleOut, QString& categoryOut, QString& descOut)
{
    if (role == "lock-time") {
        titleOut = QObject::tr("Time-Locked");
        categoryOut = QStringLiteral("guard");
        descOut = QObject::tr("Lock spending until a target block height or UNIX timestamp.");
    } else if (role == "check-signature-verification") {
        titleOut = QObject::tr("Signature Check");
        categoryOut = QStringLiteral("guard");
        descOut = QObject::tr("Require a valid ECDSA/Schnorr signature from the specified public key.");
    } else if (role == "equalverify-checksig") {
        titleOut = QObject::tr("Address / PubKeyHash");
        categoryOut = QStringLiteral("guard");
        descOut = QObject::tr("Standard P2PKH-style verification matching a 20-byte PubKeyHash.");
    } else if (role == "multi-signature") {
        titleOut = QObject::tr("Multi-Signature (m-of-n)");
        categoryOut = QStringLiteral("crypto");
        descOut = QObject::tr("Require m valid signatures out of n authorized public keys.");
    } else if (role == "hash160") {
        titleOut = QObject::tr("Hash-Locked (HTLC)");
        categoryOut = QStringLiteral("crypto");
        descOut = QObject::tr("Require revealing a secret preimage whose Hash160 matches this digest.");
    } else if (role == "if-condition") {
        titleOut = QObject::tr("IF / ELSE Branch");
        categoryOut = QStringLiteral("flow");
        descOut = QObject::tr("Execute THEN path when condition holds, or optional ELSE fallback path.");
    } else if (role == "coin-lock-miner") {
        titleOut = QObject::tr("ADAM Coin-Lock Miner");
        categoryOut = QStringLiteral("mining");
        descOut = QObject::tr("Register an Independent Miner Track identity via UTXO timelock.");
    } else if (role == "pow-miner") {
        titleOut = QObject::tr("ADAM PoW-Lock Miner");
        categoryOut = QStringLiteral("mining");
        descOut = QObject::tr("Register a PoW-validated Independent Miner identity with nonce.");
    } else if (role == "number") {
        titleOut = QObject::tr("Push Integer Constant");
        categoryOut = QStringLiteral("mining");
        descOut = QObject::tr("Push a small integer or boolean constant (-1..16) onto the script stack.");
    } else {
        titleOut = role.isEmpty() ? QObject::tr("Contract Block") : role;
        categoryOut = QStringLiteral("guard");
        descOut = QObject::tr("Custom MESCAL execution block.");
    }
}

QColor CategoryAccentColor(const QString& category, bool isLight)
{
    if (category == "crypto") {
        return isLight ? QColor("#5B5FC7") : QColor("#8B7FD6");
    }
    if (category == "flow") {
        return isLight ? QColor("#D97757") : QColor("#F5897C");
    }
    if (category == "mining") {
        return isLight ? QColor("#64748B") : QColor("#94A3B8");
    }
    return isLight ? QColor("#3E5B94") : QColor("#7695CD");
}

QString DescribeSingleActionList(const UniValue& actOrArr)
{
    QStringList parts;
    auto pushAct = [&parts](const UniValue& obj) {
        if (!obj.isObject() || !obj.exists("role") || !obj["role"].isStr()) return;
        QString role = QString::fromStdString(obj["role"].get_str());
        QString t, c, d;
        RoleMetadata(role, t, c, d);
        parts << t;
    };
    if (actOrArr.isArray()) {
        for (unsigned int k = 0; k < actOrArr.size(); ++k) pushAct(actOrArr[k]);
    } else if (actOrArr.isObject()) {
        pushAct(actOrArr);
    }
    return parts.isEmpty() ? QObject::tr("None") : parts.join(QStringLiteral(" + "));
}

QString FormatNodeDetails(const UniValue& act)
{
    if (!act.isObject()) return QString();
    const std::string role = (act.exists("role") && act["role"].isStr()) ? act["role"].get_str() : "";
    if (role == "if-condition") {
        const UniValue expr = act.exists("expression") ? act["expression"]
            : (act.exists("expressions") ? act["expressions"] : UniValue());
        const UniValue trueAct = act.exists("true_action") ? act["true_action"]
            : (act.exists("true") ? act["true"] : UniValue());
        const UniValue falseAct = act.exists("false_action") ? act["false_action"]
            : (act.exists("false") ? act["false"] : UniValue());

        QString out = QObject::tr("IF: %1\nTHEN: %2")
            .arg(DescribeSingleActionList(expr), DescribeSingleActionList(trueAct));
        if (!falseAct.isNull() && (!falseAct.isArray() || !falseAct.empty())) {
            out += QStringLiteral("\n") + QObject::tr("ELSE: %1").arg(DescribeSingleActionList(falseAct));
        }
        return out;
    }

    QStringList lines;
    if (act.exists("inputs") && act["inputs"].isArray()) {
        const UniValue& inputs = act["inputs"];
        for (unsigned int j = 0; j < inputs.size(); ++j) {
            if (!inputs[j].isObject()) continue;
            const QString nameStr = (inputs[j].exists("name") && inputs[j]["name"].isStr())
                ? QString::fromStdString(inputs[j]["name"].get_str()) : QString();
            if (nameStr.isEmpty()) continue;

            QString valStr;
            if (inputs[j].exists("value")) {
                const UniValue& v = inputs[j]["value"];
                if (v.isStr() || v.isNum() || v.isBool()) {
                    valStr = ShortenDisplayValue(QString::fromStdString(v.getValStr()));
                } else if (v.isArray()) {
                    valStr = QObject::tr("%1 keys").arg(v.size());
                } else {
                    valStr = ShortenDisplayValue(QString::fromStdString(v.write()));
                }
            }
            lines << QString("%1: %2").arg(nameStr, valStr);
        }
    }
    return lines.join('\n');
}

UniValue BuildDefaultNodeForRole(const QString& role)
{
    UniValue node(UniValue::VOBJ);
    UniValue inputs(UniValue::VARR);
    const std::string pubkey = GetDefaultWalletPubkeyHex().toStdString();
    const std::string pkh = GetDefaultWalletPubkeyHashHex().toStdString();
    const int64_t lockHeight = GetDefaultLockHeight();

    if (role == "lock-time") {
        node.pushKV("role", "lock-time");
        UniValue inp(UniValue::VOBJ);
        inp.pushKV("name", "Lock-Until");
        inp.pushKV("type", "timestamp-or-block-height");
        inp.pushKV("value", lockHeight);
        inputs.push_back(inp);
        node.pushKV("inputs", inputs);
    } else if (role == "check-signature-verification") {
        node.pushKV("role", "check-signature-verification");
        UniValue inp(UniValue::VOBJ);
        inp.pushKV("name", "Pubkey");
        inp.pushKV("type", "pubkey");
        inp.pushKV("value", pubkey);
        inputs.push_back(inp);
        node.pushKV("inputs", inputs);
    } else if (role == "equalverify-checksig") {
        node.pushKV("role", "equalverify-checksig");
        UniValue inp(UniValue::VOBJ);
        inp.pushKV("name", "PubKeyHash");
        inp.pushKV("type", "pubkeyhash");
        inp.pushKV("value", pkh);
        inputs.push_back(inp);
        node.pushKV("inputs", inputs);
    } else if (role == "multi-signature") {
        node.pushKV("role", "multi-signature");
        UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", 2);
        UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", 3);
        UniValue keys(UniValue::VARR);
        keys.push_back(pubkey);
        keys.push_back("02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234");
        keys.push_back("03ab89ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1235");
        UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keys);
        inputs.push_back(inpM);
        inputs.push_back(inpN);
        inputs.push_back(inpSigs);
        node.pushKV("inputs", inputs);
    } else if (role == "hash160") {
        node.pushKV("role", "hash160");
        UniValue inp(UniValue::VOBJ);
        inp.pushKV("name", "Hash160");
        inp.pushKV("type", "hash160");
        inp.pushKV("value", ComputeRawHash160Hex(QStringLiteral("krista-secret")).toStdString());
        inputs.push_back(inp);
        node.pushKV("inputs", inputs);
    } else if (role == "if-condition") {
        node.pushKV("role", "if-condition");
        node.pushKV("expression", BuildDefaultNodeForRole(QStringLiteral("hash160")));

        UniValue trueBranch(UniValue::VARR);
        trueBranch.push_back(BuildDefaultNodeForRole(QStringLiteral("check-signature-verification")));
        node.pushKV("true_action", trueBranch);

        UniValue falseBranch(UniValue::VARR);
        falseBranch.push_back(BuildDefaultNodeForRole(QStringLiteral("lock-time")));
        falseBranch.push_back(BuildDefaultNodeForRole(QStringLiteral("check-signature-verification")));
        node.pushKV("false_action", falseBranch);
    } else if (role == "coin-lock-miner") {
        const int64_t minerLock = static_cast<int64_t>(GetCurrentChainHeight()) + 2900;
        node.pushKV("role", "coin-lock-miner");
        UniValue inp1(UniValue::VOBJ); inp1.pushKV("name", "Pubkey"); inp1.pushKV("type", "pubkey"); inp1.pushKV("value", pubkey);
        UniValue inp2(UniValue::VOBJ); inp2.pushKV("name", "Lock-Until"); inp2.pushKV("type", "timestamp-or-block-height"); inp2.pushKV("value", minerLock);
        UniValue inp3(UniValue::VOBJ); inp3.pushKV("name", "PubkeyHash"); inp3.pushKV("type", "pubkeyhash"); inp3.pushKV("value", pkh);
        inputs.push_back(inp1);
        inputs.push_back(inp2);
        inputs.push_back(inp3);
        node.pushKV("inputs", inputs);
    } else if (role == "pow-miner") {
        const int64_t minerLock = static_cast<int64_t>(GetCurrentChainHeight()) + 2900;
        node.pushKV("role", "pow-miner");
        UniValue inp1(UniValue::VOBJ); inp1.pushKV("name", "Nonce"); inp1.pushKV("type", "nonce"); inp1.pushKV("value", "00000000");
        UniValue inp2(UniValue::VOBJ); inp2.pushKV("name", "Challenge"); inp2.pushKV("type", "challenge"); inp2.pushKV("value", GetTipHashHex().toStdString());
        UniValue inp3(UniValue::VOBJ); inp3.pushKV("name", "Pubkey"); inp3.pushKV("type", "pubkey"); inp3.pushKV("value", pubkey);
        UniValue inp4(UniValue::VOBJ); inp4.pushKV("name", "Lock-Until"); inp4.pushKV("type", "timestamp-or-block-height"); inp4.pushKV("value", minerLock);
        UniValue inp5(UniValue::VOBJ); inp5.pushKV("name", "PubkeyHash"); inp5.pushKV("type", "pubkeyhash"); inp5.pushKV("value", pkh);
        inputs.push_back(inp1);
        inputs.push_back(inp2);
        inputs.push_back(inp3);
        inputs.push_back(inp4);
        inputs.push_back(inp5);
        node.pushKV("inputs", inputs);
    } else if (role == "number") {
        node.pushKV("role", "number");
        UniValue inp(UniValue::VOBJ);
        inp.pushKV("name", "Value");
        inp.pushKV("type", "number");
        inp.pushKV("value", 1);
        inputs.push_back(inp);
        node.pushKV("inputs", inputs);
    }
    return node;
}

UniValue BuildMescalDocFromActions(const UniValue& customActions)
{
    UniValue doc(UniValue::VOBJ);
    UniValue basic(UniValue::VOBJ);
    UniValue conditions(UniValue::VOBJ);
    UniValue contract(UniValue::VOBJ);
    UniValue actions(UniValue::VARR);

    int stepIndex = 1;
    int condIndex = 1;

    auto registerBasicList = [&basic, &stepIndex](const UniValue& val, UniValue& refsOut) {
        auto pushOne = [&basic, &stepIndex, &refsOut](const UniValue& bAct) {
            if (!bAct.isObject() || !bAct.exists("role") || !bAct["role"].isStr()) return;
            const std::string step = "Step-" + std::to_string(stepIndex++);
            UniValue spec(UniValue::VOBJ);
            spec.pushKV("role", bAct["role"].get_str());
            if (bAct.exists("inputs")) spec.pushKV("inputs", bAct["inputs"]);
            basic.pushKV(step, spec);

            UniValue ref(UniValue::VOBJ);
            ref.pushKV("type", "basic");
            ref.pushKV("name", step);
            refsOut.push_back(ref);
        };
        if (val.isArray()) {
            for (unsigned int k = 0; k < val.size(); ++k) pushOne(val[k]);
        } else if (val.isObject()) {
            pushOne(val);
        }
    };

    for (unsigned int i = 0; i < customActions.size(); ++i) {
        const UniValue& act = customActions[i];
        if (!act.isObject() || !act.exists("role") || !act["role"].isStr()) continue;
        const std::string role = act["role"].get_str();

        if (role == "if-condition") {
            const std::string condName = "Cond-" + std::to_string(condIndex++);
            UniValue condSpec(UniValue::VOBJ);
            condSpec.pushKV("role", "if-condition");

            UniValue exprRefs(UniValue::VARR);
            const UniValue exprVal = act.exists("expression") ? act["expression"]
                : (act.exists("expressions") ? act["expressions"] : UniValue());
            registerBasicList(exprVal, exprRefs);
            condSpec.pushKV("expressions", exprRefs);

            UniValue trueRefs(UniValue::VARR);
            const UniValue trueVal = act.exists("true_action") ? act["true_action"]
                : (act.exists("true") ? act["true"] : UniValue());
            registerBasicList(trueVal, trueRefs);
            condSpec.pushKV("true", trueRefs);

            const UniValue falseVal = act.exists("false_action") ? act["false_action"]
                : (act.exists("false") ? act["false"] : UniValue());
            if (!falseVal.isNull()) {
                UniValue falseRefs(UniValue::VARR);
                registerBasicList(falseVal, falseRefs);
                if (!falseRefs.empty()) {
                    condSpec.pushKV("false", falseRefs);
                }
            }

            conditions.pushKV(condName, condSpec);
            UniValue condRef(UniValue::VOBJ);
            condRef.pushKV("type", "condition");
            condRef.pushKV("name", condName);
            actions.push_back(condRef);
        } else {
            const std::string stepName = "Step-" + std::to_string(stepIndex++);
            UniValue stepSpec(UniValue::VOBJ);
            stepSpec.pushKV("role", role);
            if (act.exists("inputs")) stepSpec.pushKV("inputs", act["inputs"]);
            basic.pushKV(stepName, stepSpec);

            UniValue actRef(UniValue::VOBJ);
            actRef.pushKV("type", "basic");
            actRef.pushKV("name", stepName);
            actions.push_back(actRef);
        }
    }

    UniValue cSpec(UniValue::VOBJ);
    cSpec.pushKV("description", "Custom smart contract built using Visual Designer.");
    cSpec.pushKV("actions", actions);
    contract.pushKV("CustomContract", cSpec);

    doc.pushKV("basic", basic);
    if (!conditions.empty()) {
        doc.pushKV("condition", conditions);
    }
    doc.pushKV("contract", contract);
    doc.pushKV("active_contract", "CustomContract");
    return doc;
}

bool ValidateHexField(const QString& hexStr, int expectedLenA, int expectedLenB = -1)
{
    const QString s = hexStr.trimmed();
    if (!IsHex(s.toStdString())) return false;
    const int len = s.length();
    if (expectedLenB > 0) return (len == expectedLenA || len == expectedLenB);
    return len == expectedLenA;
}

QString ValidateActionsSanity(const UniValue& actions)
{
    auto checkBasic = [](const UniValue& act) -> QString {
        if (!act.isObject() || !act.exists("role") || !act["role"].isStr()) {
            return QObject::tr("Invalid block without role.");
        }
        const std::string role = act["role"].get_str();
        const UniValue& inputs = act.exists("inputs") ? act["inputs"] : UniValue();
        if (!inputs.isArray()) return QString();

        for (unsigned int j = 0; j < inputs.size(); ++j) {
            if (!inputs[j].isObject()) continue;
            const std::string name = inputs[j].exists("name") ? inputs[j]["name"].get_str() : "";
            const std::string type = inputs[j].exists("type") ? inputs[j]["type"].get_str() : "";
            if (!inputs[j].exists("value")) continue;
            const UniValue& val = inputs[j]["value"];

            if ((type == "pubkey" || name == "Pubkey") && val.isStr()) {
                if (!ValidateHexField(QString::fromStdString(val.get_str()), 66, 130)) {
                    return QObject::tr("%1 expects a 66-char compressed (or 130-char uncompressed) hex public key.")
                        .arg(QString::fromStdString(name));
                }
            } else if ((type == "pubkeyhash" || name == "PubKeyHash" || name == "PubkeyHash") && val.isStr()) {
                if (!ValidateHexField(QString::fromStdString(val.get_str()), 40)) {
                    return QObject::tr("%1 expects a 40-char hex PubKeyHash.").arg(QString::fromStdString(name));
                }
            } else if ((type == "hash160" || name == "Hash160") && val.isStr()) {
                if (!ValidateHexField(QString::fromStdString(val.get_str()), 40)) {
                    return QObject::tr("Hash160 expects a 40-char hex digest.");
                }
            } else if (name == "Challenge" && val.isStr()) {
                if (!ValidateHexField(QString::fromStdString(val.get_str()), 64)) {
                    return QObject::tr("Challenge expects a 64-char block hash.");
                }
            } else if (name == "Nonce" && val.isStr()) {
                if (!ValidateHexField(QString::fromStdString(val.get_str()), 8)) {
                    return QObject::tr("Nonce expects an 8-char (4-byte) hex value.");
                }
            } else if (name == "Signatures" && val.isArray()) {
                if (val.empty()) return QObject::tr("Multi-Signature requires at least 1 public key.");
                for (unsigned int k = 0; k < val.size(); ++k) {
                    if (!val[k].isStr() || !ValidateHexField(QString::fromStdString(val[k].get_str()), 66, 130)) {
                        return QObject::tr("Multi-Signature key #%1 must be a 66 or 130-char hex public key.").arg(k + 1);
                    }
                }
            }
        }
        if (role == "multi-signature") {
            int m = 0, n = 0;
            unsigned int sigCount = 0;
            for (unsigned int j = 0; j < inputs.size(); ++j) {
                const std::string name = inputs[j].exists("name") ? inputs[j]["name"].get_str() : "";
                if (name == "m" && inputs[j].exists("value")) m = inputs[j]["value"].get_int();
                if (name == "n" && inputs[j].exists("value")) n = inputs[j]["value"].get_int();
                if (name == "Signatures" && inputs[j]["value"].isArray()) sigCount = inputs[j]["value"].size();
            }
            if (m <= 0 || n <= 0 || m > n) {
                return QObject::tr("Multi-Signature requires 1 <= m <= n.");
            }
            if (sigCount < static_cast<unsigned int>(n)) {
                return QObject::tr("Multi-Signature lists %1 key(s), but n = %2 requires at least %2.").arg(sigCount).arg(n);
            }
        }
        return QString();
    };

    for (unsigned int i = 0; i < actions.size(); ++i) {
        const UniValue& act = actions[i];
        if (!act.isObject() || !act.exists("role")) continue;
        if (act["role"].get_str() == "if-condition") {
            auto checkList = [&checkBasic](const UniValue& v) -> QString {
                if (v.isArray()) {
                    for (unsigned int k = 0; k < v.size(); ++k) {
                        const QString err = checkBasic(v[k]);
                        if (!err.isEmpty()) return err;
                    }
                } else if (v.isObject()) {
                    return checkBasic(v);
                }
                return QString();
            };
            const UniValue exprVal = act.exists("expression") ? act["expression"]
                : (act.exists("expressions") ? act["expressions"] : UniValue());
            const UniValue trueVal = act.exists("true_action") ? act["true_action"]
                : (act.exists("true") ? act["true"] : UniValue());
            const UniValue falseVal = act.exists("false_action") ? act["false_action"]
                : (act.exists("false") ? act["false"] : UniValue());

            QString err = checkList(exprVal);
            if (!err.isEmpty()) return err;
            err = checkList(trueVal);
            if (!err.isEmpty()) return err;
            err = checkList(falseVal);
            if (!err.isEmpty()) return err;
        } else {
            const QString err = checkBasic(act);
            if (!err.isEmpty()) return err;
        }
    }
    return QString();
}

void ClearLayoutItems(QLayout* layout)
{
    if (!layout) return;
    QLayoutItem* child = nullptr;
    while ((child = layout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            child->widget()->deleteLater();
        } else if (child->layout()) {
            ClearLayoutItems(child->layout());
        }
        delete child;
    }
}

void BuildBasicBlockInputsForm(QWidget* host,
                               const UniValue& initialAct,
                               const std::function<void(const UniValue&)>& onChanged)
{
    QVBoxLayout* layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    UniValue sharedAct = initialAct;
    if (!sharedAct.isObject() || !sharedAct.exists("inputs") || !sharedAct["inputs"].isArray()) {
        return;
    }

    auto state = std::make_shared<UniValue>(sharedAct);
    const UniValue& inputs = (*state)["inputs"];

    auto addFieldLabel = [host, layout](const QString& text) {
        QLabel* lbl = new QLabel(text, host);
        lbl->setProperty("vdFieldLabel", true);
        layout->addWidget(lbl);
    };

    for (unsigned int j = 0; j < inputs.size(); ++j) {
        if (!inputs[j].isObject() || !inputs[j].exists("name")) continue;
        const QString nameStr = QString::fromStdString(inputs[j]["name"].get_str());
        const QString typeStr = (inputs[j].exists("type") && inputs[j]["type"].isStr())
            ? QString::fromStdString(inputs[j]["type"].get_str()) : QStringLiteral("string");

        if (typeStr == "array" || (inputs[j].exists("value") && inputs[j]["value"].isArray())) {
            addFieldLabel(QObject::tr("%1 (one public key hex per line)").arg(nameStr));

            QPlainTextEdit* txtKeys = new QPlainTextEdit(host);
            txtKeys->setProperty("vdTextArea", true);
            txtKeys->setFixedHeight(92);

            QStringList keyLines;
            if (inputs[j].exists("value") && inputs[j]["value"].isArray()) {
                for (unsigned int k = 0; k < inputs[j]["value"].size(); ++k) {
                    keyLines << QString::fromStdString(inputs[j]["value"][k].getValStr());
                }
            }
            txtKeys->setPlainText(keyLines.join('\n'));
            layout->addWidget(txtKeys);

            QHBoxLayout* btnRow = new QHBoxLayout();
            btnRow->setContentsMargins(0, 0, 0, 0);
            btnRow->setSpacing(6);
            QPushButton* btnAddMyKey = new QPushButton(QObject::tr("+ Append My PubKey"), host);
            btnAddMyKey->setProperty("vdMiniBtn", true);
            btnAddMyKey->setFixedHeight(26);
            btnAddMyKey->setCursor(Qt::PointingHandCursor);
            btnAddMyKey->setFocusPolicy(Qt::NoFocus);
            btnRow->addWidget(btnAddMyKey);
            btnRow->addStretch(1);
            layout->addLayout(btnRow);

            QObject::connect(btnAddMyKey, &QPushButton::clicked, host, [txtKeys]() {
                const QString cur = txtKeys->toPlainText().trimmed();
                const QString myKey = GetDefaultWalletPubkeyHex();
                txtKeys->setPlainText(cur.isEmpty() ? myKey : (cur + QStringLiteral("\n") + myKey));
            });

            QObject::connect(txtKeys, &QPlainTextEdit::textChanged, host, [state, j, txtKeys, onChanged]() {
                UniValue newInputs(UniValue::VARR);
                const UniValue& curInputs = (*state)["inputs"];
                for (unsigned int idx = 0; idx < curInputs.size(); ++idx) {
                    if (idx == j) {
                        UniValue item = curInputs[idx];
                        UniValue arr(UniValue::VARR);
                        const QString raw = txtKeys->toPlainText();
                        const QStringList parts = raw.split(QRegExp(QStringLiteral("[\\s,;]+")), QString::SkipEmptyParts);
                        for (const QString& p : parts) {
                            arr.push_back(p.trimmed().toStdString());
                        }
                        item.pushKV("value", arr);
                        newInputs.push_back(item);
                    } else {
                        newInputs.push_back(curInputs[idx]);
                    }
                }
                state->pushKV("inputs", newInputs);
                onChanged(*state);
            });
            continue;
        }

        if (typeStr == "number" && (nameStr == "Value" || nameStr == "m" || nameStr == "n")) {
            addFieldLabel(nameStr);
            QSpinBox* spin = new QSpinBox(host);
            spin->setProperty("vdSpin", true);
            spin->setFixedHeight(34);
            if (nameStr == "Value") {
                spin->setRange(-1, 16);
            } else {
                spin->setRange(1, 20);
            }
            const int curVal = inputs[j].exists("value")
                ? (inputs[j]["value"].isNum() ? inputs[j]["value"].get_int() : atoi(inputs[j]["value"].getValStr().c_str()))
                : 1;
            spin->setValue(curVal);
            layout->addWidget(spin);

            QObject::connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), host, [state, j, onChanged](int v) {
                UniValue newInputs(UniValue::VARR);
                const UniValue& curInputs = (*state)["inputs"];
                for (unsigned int idx = 0; idx < curInputs.size(); ++idx) {
                    if (idx == j) {
                        UniValue item = curInputs[idx];
                        item.pushKV("value", v);
                        newInputs.push_back(item);
                    } else {
                        newInputs.push_back(curInputs[idx]);
                    }
                }
                state->pushKV("inputs", newInputs);
                onChanged(*state);
            });
            continue;
        }

        if (typeStr == "timestamp-or-block-height" || nameStr == "Lock-Until") {
            addFieldLabel(QObject::tr("Lock Until (Block Height or UNIX Timestamp)"));
            QLineEdit* editLock = new QLineEdit(host);
            editLock->setProperty("vdInput", true);
            editLock->setFixedHeight(34);
            const QString curVal = inputs[j].exists("value") ? QString::fromStdString(inputs[j]["value"].getValStr()) : QStringLiteral("0");
            editLock->setText(curVal);
            layout->addWidget(editLock);

            QHBoxLayout* presetRow = new QHBoxLayout();
            presetRow->setContentsMargins(0, 0, 0, 0);
            presetRow->setSpacing(6);
            auto addLockPreset = [host, presetRow, editLock](const QString& label, const std::function<QString()>& fn) {
                QPushButton* b = new QPushButton(label, host);
                b->setProperty("vdMiniBtn", true);
                b->setFixedHeight(26);
                b->setCursor(Qt::PointingHandCursor);
                b->setFocusPolicy(Qt::NoFocus);
                QObject::connect(b, &QPushButton::clicked, host, [editLock, fn]() {
                    editLock->setText(fn());
                });
                presetRow->addWidget(b);
            };
            addLockPreset(QObject::tr("+100 blk"), []() { return QString::number(GetCurrentChainHeight() + 100); });
            addLockPreset(QObject::tr("+1 day blk"), []() { return QString::number(GetCurrentChainHeight() + 2880); });
            addLockPreset(QObject::tr("+30 days ts"), []() {
                return QString::number(QDateTime::currentSecsSinceEpoch() + static_cast<qint64>(30) * 86400);
            });
            presetRow->addStretch(1);
            layout->addLayout(presetRow);

            QObject::connect(editLock, &QLineEdit::textChanged, host, [state, j, onChanged](const QString& txt) {
                UniValue newInputs(UniValue::VARR);
                const UniValue& curInputs = (*state)["inputs"];
                for (unsigned int idx = 0; idx < curInputs.size(); ++idx) {
                    if (idx == j) {
                        UniValue item = curInputs[idx];
                        bool okNum = false;
                        qint64 numVal = txt.trimmed().toLongLong(&okNum);
                        if (okNum) {
                            item.pushKV("value", static_cast<int64_t>(numVal));
                        } else {
                            item.pushKV("value", txt.trimmed().toStdString());
                        }
                        newInputs.push_back(item);
                    } else {
                        newInputs.push_back(curInputs[idx]);
                    }
                }
                state->pushKV("inputs", newInputs);
                onChanged(*state);
            });
            continue;
        }

        // Standard hex/text inputs (Pubkey, PubKeyHash, Hash160, Nonce, Challenge, generic)
        addFieldLabel(nameStr);
        QLineEdit* editText = new QLineEdit(host);
        editText->setProperty("vdInput", true);
        editText->setFixedHeight(34);
        editText->setClearButtonEnabled(true);
        styleLineEditClearButton(editText);
        const QString curVal = inputs[j].exists("value") ? QString::fromStdString(inputs[j]["value"].getValStr()) : QString();
        editText->setText(curVal);
        layout->addWidget(editText);

        QObject::connect(editText, &QLineEdit::textChanged, host, [state, j, onChanged](const QString& txt) {
            UniValue newInputs(UniValue::VARR);
            const UniValue& curInputs = (*state)["inputs"];
            for (unsigned int idx = 0; idx < curInputs.size(); ++idx) {
                if (idx == j) {
                    UniValue item = curInputs[idx];
                    item.pushKV("value", txt.trimmed().toStdString());
                    newInputs.push_back(item);
                } else {
                    newInputs.push_back(curInputs[idx]);
                }
            }
            state->pushKV("inputs", newInputs);
            onChanged(*state);
        });

        if (typeStr == "pubkey" || nameStr == "Pubkey") {
            QHBoxLayout* helperRow = new QHBoxLayout();
            helperRow->setContentsMargins(0, 0, 0, 0);
            QPushButton* btnMine = new QPushButton(QObject::tr("Use My Wallet PubKey"), host);
            btnMine->setProperty("vdMiniBtn", true);
            btnMine->setFixedHeight(26);
            btnMine->setCursor(Qt::PointingHandCursor);
            btnMine->setFocusPolicy(Qt::NoFocus);
            QObject::connect(btnMine, &QPushButton::clicked, host, [editText]() {
                editText->setText(GetDefaultWalletPubkeyHex());
            });
            helperRow->addWidget(btnMine);
            helperRow->addStretch(1);
            layout->addLayout(helperRow);
        } else if (typeStr == "pubkeyhash" || nameStr == "PubKeyHash" || nameStr == "PubkeyHash") {
            QHBoxLayout* helperRow = new QHBoxLayout();
            helperRow->setContentsMargins(0, 0, 0, 0);
            QPushButton* btnPkh = new QPushButton(QObject::tr("Use My Wallet PubKeyHash"), host);
            btnPkh->setProperty("vdMiniBtn", true);
            btnPkh->setFixedHeight(26);
            btnPkh->setCursor(Qt::PointingHandCursor);
            btnPkh->setFocusPolicy(Qt::NoFocus);
            QObject::connect(btnPkh, &QPushButton::clicked, host, [editText]() {
                editText->setText(GetDefaultWalletPubkeyHashHex());
            });
            helperRow->addWidget(btnPkh);
            helperRow->addStretch(1);
            layout->addLayout(helperRow);
        } else if (nameStr == "Challenge") {
            QHBoxLayout* helperRow = new QHBoxLayout();
            helperRow->setContentsMargins(0, 0, 0, 0);
            QPushButton* btnTip = new QPushButton(QObject::tr("Use Current Tip Hash"), host);
            btnTip->setProperty("vdMiniBtn", true);
            btnTip->setFixedHeight(26);
            btnTip->setCursor(Qt::PointingHandCursor);
            btnTip->setFocusPolicy(Qt::NoFocus);
            QObject::connect(btnTip, &QPushButton::clicked, host, [editText]() {
                editText->setText(GetTipHashHex());
            });
            helperRow->addWidget(btnTip);
            helperRow->addStretch(1);
            layout->addLayout(helperRow);
        } else if (typeStr == "hash160" || nameStr == "Hash160") {
            QHBoxLayout* hashGenRow = new QHBoxLayout();
            hashGenRow->setContentsMargins(0, 0, 0, 0);
            hashGenRow->setSpacing(6);
            QLineEdit* editPreimage = new QLineEdit(host);
            editPreimage->setProperty("vdInput", true);
            editPreimage->setFixedHeight(30);
            editPreimage->setPlaceholderText(QObject::tr("Type secret phrase to Hash160..."));
            QPushButton* btnHash = new QPushButton(QObject::tr("Hash160"), host);
            btnHash->setProperty("vdMiniBtn", true);
            btnHash->setFixedHeight(30);
            btnHash->setCursor(Qt::PointingHandCursor);
            btnHash->setFocusPolicy(Qt::NoFocus);
            QObject::connect(btnHash, &QPushButton::clicked, host, [editPreimage, editText]() {
                if (!editPreimage->text().isEmpty()) {
                    editText->setText(ComputeRawHash160Hex(editPreimage->text()));
                }
            });
            hashGenRow->addWidget(editPreimage, 1);
            hashGenRow->addWidget(btnHash);
            layout->addLayout(hashGenRow);
        }
    }
}

} // namespace

// =============================================================================
// DesignerNodeItem
// =============================================================================

DesignerNodeItem::DesignerNodeItem(int stepIdx,
                                   const QString& roleName,
                                   const QString& blockTitle,
                                   const QString& categoryName,
                                   const QString& detailText,
                                   const UniValue& data,
                                   bool lightTheme,
                                   QGraphicsItem* parent)
    : QGraphicsRectItem(parent),
      stepIndex(stepIdx),
      role(roleName),
      title(blockTitle),
      category(categoryName),
      details(detailText),
      nodeData(data),
      isLightTheme(lightTheme)
{
    setFlag(QGraphicsItem::ItemIsMovable);
    setFlag(QGraphicsItem::ItemIsSelectable);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges);
    setCursor(Qt::OpenHandCursor);
    updateContent(stepIdx, detailText, data, lightTheme);
}

void DesignerNodeItem::updateContent(int stepIdx, const QString& detailText, const UniValue& data, bool lightTheme)
{
    prepareGeometryChange();
    stepIndex = stepIdx;
    details = detailText;
    nodeData = data;
    isLightTheme = lightTheme;

    const int lineCount = details.isEmpty() ? 1 : (details.count('\n') + 1);
    const qreal h = std::max<qreal>(88.0, 56.0 + static_cast<qreal>(lineCount) * 18.0);
    setRect(0, 0, NODE_WIDTH, h);
    update();
}

void DesignerNodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* /*option*/, QWidget* /*widget*/)
{
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = rect();
    const bool selected = isSelected();
    const QColor accent = CategoryAccentColor(category, isLightTheme);

    const QColor cardBg = isLightTheme
        ? (selected ? QColor("#F5F8FE") : QColor("#FFFFFF"))
        : (selected ? QColor("#182542") : QColor("#131B2E"));
    const QColor borderCol = selected
        ? (isLightTheme ? QColor("#3E5B94") : QColor("#F5897C"))
        : (isLightTheme ? QColor("#CBD5E1") : QColor("#253554"));
    const QColor dividerCol = isLightTheme ? QColor("#E2E8F0") : QColor("#1E2D4A");
    const QColor titleCol = isLightTheme ? QColor("#0F172A") : QColor("#F1F5F9");
    const QColor subCol = isLightTheme ? QColor("#475569") : QColor("#CBD5E1");
    const QColor tagBg = isLightTheme ? QColor("#F1F5F9") : QColor("#1C2942");
    const QColor tagFg = isLightTheme ? QColor("#475569") : QColor("#94A3B8");
    const QColor badgeBg = isLightTheme ? QColor("#EEF3FA") : QColor("#1B2B4A");
    const QColor badgeFg = isLightTheme ? QColor("#3E5B94") : QColor("#A8C1EE");

    // Subtle elevation shadow
    QPainterPath shadowPath;
    shadowPath.addRoundedRect(r.adjusted(0, 2, 0, 2), 10, 10);
    painter->fillPath(shadowPath, isLightTheme ? QColor(15, 23, 42, 18) : QColor(0, 0, 0, 55));

    // Card fill & border
    QPainterPath cardPath;
    cardPath.addRoundedRect(r, 10, 10);
    painter->setClipPath(cardPath);
    painter->fillPath(cardPath, cardBg);

    // Left accent stripe
    painter->fillRect(QRectF(r.left(), r.top(), 4.5, r.height()), accent);
    painter->setClipping(false);

    QPen borderPen(borderCol, selected ? 2.2 : 1.2);
    painter->setPen(borderPen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(r, 10, 10);

    // Header row: Step badge + Title + Role pill
    const QRectF stepRect(14, 11, 28, 20);
    painter->setPen(Qt::NoPen);
    painter->setBrush(badgeBg);
    painter->drawRoundedRect(stepRect, 6, 6);

    QFont f = painter->font();
    f.setPointSize(9);
    f.setBold(true);
    painter->setFont(f);
    painter->setPen(badgeFg);
    painter->drawText(stepRect, Qt::AlignCenter, QString("#%1").arg(stepIndex));

    // Role tag pill on top right
    QFont tagFont = painter->font();
    tagFont.setPointSize(8);
    tagFont.setBold(false);
    painter->setFont(tagFont);
    const QFontMetrics tagFm(tagFont);
    const QString roleTag = role.length() > 18 ? (role.left(16) + QStringLiteral("..")) : role;
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
    const int tagTextW = tagFm.horizontalAdvance(roleTag);
#else
    const int tagTextW = tagFm.width(roleTag);
#endif
    const qreal tagW = std::min<qreal>(128.0, tagTextW + 14.0);
    const QRectF tagRect(r.width() - tagW - 12.0, 11, tagW, 20);
    painter->setPen(Qt::NoPen);
    painter->setBrush(tagBg);
    painter->drawRoundedRect(tagRect, 6, 6);
    painter->setPen(tagFg);
    painter->drawText(tagRect, Qt::AlignCenter, roleTag);

    // Block Title
    QFont titleFont = painter->font();
    titleFont.setPointSize(10);
    titleFont.setBold(true);
    painter->setFont(titleFont);
    painter->setPen(titleCol);
    const QRectF titleRect(49, 10, tagRect.left() - 56, 22);
    painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(titleFont).elidedText(title, Qt::ElideRight, static_cast<int>(titleRect.width())));

    // Header divider
    painter->setPen(QPen(dividerCol, 1.0));
    painter->drawLine(QPointF(14, 39), QPointF(r.width() - 12, 39));

    // Parameter Details
    QFont detailFont = painter->font();
    detailFont.setPointSize(9);
    detailFont.setBold(false);
    painter->setFont(detailFont);
    painter->setPen(subCol);
    const QString bodyText = details.isEmpty() ? QObject::tr("No parameters required") : details;
    painter->drawText(QRectF(14, 46, r.width() - 26, r.height() - 52),
                      Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, bodyText);
}

// =============================================================================
// BlueprintScene
// =============================================================================

BlueprintScene::BlueprintScene(bool isLight, QObject* parent)
    : QGraphicsScene(parent), m_isLight(isLight)
{
}

void BlueprintScene::setLightTheme(bool isLight)
{
    m_isLight = isLight;
    update();
}

void BlueprintScene::drawBackground(QPainter* painter, const QRectF& rect)
{
    painter->fillRect(rect, m_isLight ? QColor("#F8FAFC") : QColor("#0E1526"));

    const int gridStep = 20;
    const int left = static_cast<int>(std::floor(rect.left() / gridStep)) * gridStep;
    const int top = static_cast<int>(std::floor(rect.top() / gridStep)) * gridStep;

    const QColor dotColor = m_isLight ? QColor("#CBD5E1") : QColor("#253554");
    painter->setPen(Qt::NoPen);
    painter->setBrush(dotColor);

    for (int x = left; x < rect.right(); x += gridStep) {
        for (int y = top; y < rect.bottom(); y += gridStep) {
            painter->drawEllipse(QPointF(x, y), 1.1, 1.1);
        }
    }
}

// =============================================================================
// VisualDesignerDialog
// =============================================================================

VisualDesignerDialog::VisualDesignerDialog(const UniValue& initialActions, QWidget* parent)
    : QDialog(parent),
      currentActions(initialActions.isArray() ? initialActions : UniValue(UniValue::VARR)),
      m_isLight(::isLightTheme())
{
    setWindowTitle(tr("Visual Smart Contract Designer"));
    setObjectName("VisualDesignerDialog");
    resize(1180, 740);
    setMinimumSize(960, 620);

    setupLayout();
    applyTheme(m_isLight);
    rebuildScene(currentActions.empty() ? -1 : 0);
}

VisualDesignerDialog::~VisualDesignerDialog()
{
    m_isClosing = true;
    m_rebuildingScene = true;
    if (view) {
        view->removeEventFilter(this);
        if (view->viewport()) {
            view->viewport()->removeEventFilter(this);
        }
        view->setScene(nullptr);
    }
    if (scene) {
        disconnect(scene, nullptr, this, nullptr);
        nodeItems.clear();
        overlayItems.clear();
        scene->clear();
        delete scene;
        scene = nullptr;
    }
}

void VisualDesignerDialog::setupLayout()
{
    QVBoxLayout* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    dialogRoot = new QWidget(this);
    dialogRoot->setObjectName("vdDialogRoot");
    dialogRoot->setAttribute(Qt::WA_StyledBackground, true);
    outer->addWidget(dialogRoot);

    QVBoxLayout* rootLayout = new QVBoxLayout(dialogRoot);
    rootLayout->setContentsMargins(18, 14, 18, 14);
    rootLayout->setSpacing(12);

    // -------------------------------------------------------------------------
    // 1. Header Row
    // -------------------------------------------------------------------------
    QHBoxLayout* headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(10);

    QVBoxLayout* titleCol = new QVBoxLayout();
    titleCol->setContentsMargins(0, 0, 0, 0);
    titleCol->setSpacing(2);

    lblHeaderTitle = new QLabel(tr("Visual Smart Contract Designer"), dialogRoot);
    lblHeaderTitle->setProperty("vdTitle", true);
    lblHeaderSubtitle = new QLabel(
        tr("Build, reorder, and configure MESCAL Taproot smart-contract execution blocks visually."),
        dialogRoot);
    lblHeaderSubtitle->setProperty("vdSubtitle", true);
    titleCol->addWidget(lblHeaderTitle);
    titleCol->addWidget(lblHeaderSubtitle);

    lblBlockCount = new QLabel(tr("0 blocks"), dialogRoot);
    lblBlockCount->setProperty("vdCountPill", true);

    comboRecipes = new QComboBox(dialogRoot);
    comboRecipes->setProperty("vdCombo", true);
    comboRecipes->setView(new QListView(comboRecipes));
    comboRecipes->setFixedHeight(34);
    comboRecipes->setMinimumWidth(210);
    comboRecipes->setCursor(Qt::PointingHandCursor);
    comboRecipes->addItem(tr("Starter Recipes..."));
    comboRecipes->addItem(tr("1. Time-Locked Vault"));
    comboRecipes->addItem(tr("2. 2-of-3 Escrow Multi-Sig"));
    comboRecipes->addItem(tr("3. Hash-Locked Claim (HTLC)"));
    comboRecipes->addItem(tr("4. IF/ELSE Recovery Switch"));
    comboRecipes->addItem(tr("5. ADAM Coin-Lock Miner"));

    btnLoadRecipe = new QPushButton(tr("Load Recipe"), dialogRoot);
    btnLoadRecipe->setProperty("vdSecondary", true);
    btnLoadRecipe->setFixedHeight(34);
    btnLoadRecipe->setCursor(Qt::PointingHandCursor);
    btnLoadRecipe->setFocusPolicy(Qt::NoFocus);

    btnAutoLayout = new QPushButton(tr("Auto-Layout"), dialogRoot);
    btnAutoLayout->setProperty("vdSecondary", true);
    btnAutoLayout->setFixedHeight(34);
    btnAutoLayout->setCursor(Qt::PointingHandCursor);
    btnAutoLayout->setFocusPolicy(Qt::NoFocus);

    btnClearAll = new QPushButton(tr("Clear"), dialogRoot);
    btnClearAll->setProperty("vdSecondary", true);
    btnClearAll->setFixedHeight(34);
    btnClearAll->setCursor(Qt::PointingHandCursor);
    btnClearAll->setFocusPolicy(Qt::NoFocus);

    headerRow->addLayout(titleCol, 1);
    headerRow->addWidget(lblBlockCount);
    headerRow->addWidget(comboRecipes);
    headerRow->addWidget(btnLoadRecipe);
    headerRow->addWidget(btnAutoLayout);
    headerRow->addWidget(btnClearAll);
    rootLayout->addLayout(headerRow);

    // -------------------------------------------------------------------------
    // 2. Three-Pane Studio Body (Block Library | Canvas | Block Inspector)
    // -------------------------------------------------------------------------
    QHBoxLayout* bodyRow = new QHBoxLayout();
    bodyRow->setContentsMargins(0, 0, 0, 0);
    bodyRow->setSpacing(12);

    // Left Card: Block Library (272px)
    QFrame* libCard = new QFrame(dialogRoot);
    libCard->setProperty("vdCard", true);
    libCard->setAttribute(Qt::WA_StyledBackground, true);
    libCard->setFixedWidth(272);
    QVBoxLayout* libCardLayout = new QVBoxLayout(libCard);
    libCardLayout->setContentsMargins(0, 0, 0, 0);
    libCardLayout->setSpacing(0);

    QWidget* libHeader = new QWidget(libCard);
    libHeader->setObjectName("vdPaneHeader");
    QVBoxLayout* libHeaderLayout = new QVBoxLayout(libHeader);
    libHeaderLayout->setContentsMargins(14, 12, 14, 10);
    libHeaderLayout->setSpacing(8);

    QLabel* lblLibTitle = new QLabel(tr("Block Library"), libHeader);
    lblLibTitle->setProperty("vdSectionTitle", true);
    QLabel* lblLibSub = new QLabel(tr("Click any block to add it to the flow."), libHeader);
    lblLibSub->setProperty("vdHint", true);

    editLibrarySearch = new QLineEdit(libHeader);
    editLibrarySearch->setProperty("vdInput", true);
    editLibrarySearch->setPlaceholderText(tr("Filter blocks..."));
    editLibrarySearch->setClearButtonEnabled(true);
    editLibrarySearch->setFixedHeight(32);
    styleLineEditClearButton(editLibrarySearch);

    libHeaderLayout->addWidget(lblLibTitle);
    libHeaderLayout->addWidget(lblLibSub);
    libHeaderLayout->addWidget(editLibrarySearch);
    libCardLayout->addWidget(libHeader);

    QFrame* libDiv = new QFrame(libCard);
    libDiv->setProperty("vdDivider", true);
    libDiv->setFixedHeight(1);
    libCardLayout->addWidget(libDiv);

    libraryScroll = new QScrollArea(libCard);
    libraryScroll->setWidgetResizable(true);
    libraryScroll->setFrameShape(QFrame::NoFrame);
    libraryScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (libraryScroll->viewport()) {
        libraryScroll->viewport()->setObjectName("vdLibViewport");
    }

    libraryContainer = new QWidget(libraryScroll);
    libraryContainer->setObjectName("vdLibContainer");
    libraryLayout = new QVBoxLayout(libraryContainer);
    libraryLayout->setContentsMargins(10, 10, 10, 12);
    libraryLayout->setSpacing(6);
    populateBlockLibrary();
    libraryLayout->addStretch(1);
    libraryScroll->setWidget(libraryContainer);
    libCardLayout->addWidget(libraryScroll, 1);

    // Center Card: Execution Flow Canvas (Expanding)
    QFrame* canvasCard = new QFrame(dialogRoot);
    canvasCard->setProperty("vdCard", true);
    canvasCard->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* canvasCardLayout = new QVBoxLayout(canvasCard);
    canvasCardLayout->setContentsMargins(0, 0, 0, 0);
    canvasCardLayout->setSpacing(0);

    QWidget* canvasToolbar = new QWidget(canvasCard);
    canvasToolbar->setObjectName("vdPaneHeader");
    QHBoxLayout* ctLayout = new QHBoxLayout(canvasToolbar);
    ctLayout->setContentsMargins(14, 10, 14, 10);
    ctLayout->setSpacing(6);

    QVBoxLayout* canvasTitleCol = new QVBoxLayout();
    canvasTitleCol->setContentsMargins(0, 0, 0, 0);
    canvasTitleCol->setSpacing(1);
    QLabel* lblCanvasTitle = new QLabel(tr("Execution Flow"), canvasToolbar);
    lblCanvasTitle->setProperty("vdSectionTitle", true);
    QLabel* lblCanvasHint = new QLabel(tr("Drag vertically to reorder • Click block to inspect"), canvasToolbar);
    lblCanvasHint->setProperty("vdHint", true);
    canvasTitleCol->addWidget(lblCanvasTitle);
    canvasTitleCol->addWidget(lblCanvasHint);

    auto makeMiniBtn = [canvasToolbar](const QString& text, const QString& tip, bool isDanger = false) -> QPushButton* {
        QPushButton* b = new QPushButton(text, canvasToolbar);
        b->setProperty(isDanger ? "vdDangerBtn" : "vdMiniBtn", true);
        b->setFixedHeight(28);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(tip);
        return b;
    };

    btnMoveUp = makeMiniBtn(QString::fromUtf8("\xE2\x86\x91"), tr("Move selected block up"));
    btnMoveDown = makeMiniBtn(QString::fromUtf8("\xE2\x86\x93"), tr("Move selected block down"));
    btnDuplicate = makeMiniBtn(tr("Duplicate"), tr("Duplicate selected block"));
    btnRemove = makeMiniBtn(tr("Delete"), tr("Delete selected block (Del)"), true);

    QFrame* toolSep = new QFrame(canvasToolbar);
    toolSep->setProperty("vdDivider", true);
    toolSep->setFixedSize(1, 20);

    btnZoomOut = makeMiniBtn(QStringLiteral("-"), tr("Zoom out"));
    lblZoomPct = new QLabel(QStringLiteral("100%"), canvasToolbar);
    lblZoomPct->setProperty("vdHint", true);
    lblZoomPct->setAlignment(Qt::AlignCenter);
    lblZoomPct->setFixedWidth(40);
    btnZoomIn = makeMiniBtn(QStringLiteral("+"), tr("Zoom in"));
    btnZoomFit = makeMiniBtn(tr("Fit"), tr("Reset zoom & center flow"));

    ctLayout->addLayout(canvasTitleCol, 1);
    ctLayout->addWidget(btnMoveUp);
    ctLayout->addWidget(btnMoveDown);
    ctLayout->addWidget(btnDuplicate);
    ctLayout->addWidget(btnRemove);
    ctLayout->addSpacing(4);
    ctLayout->addWidget(toolSep);
    ctLayout->addSpacing(4);
    ctLayout->addWidget(btnZoomOut);
    ctLayout->addWidget(lblZoomPct);
    ctLayout->addWidget(btnZoomIn);
    ctLayout->addWidget(btnZoomFit);
    canvasCardLayout->addWidget(canvasToolbar);

    QFrame* canvasDiv = new QFrame(canvasCard);
    canvasDiv->setProperty("vdDivider", true);
    canvasDiv->setFixedHeight(1);
    canvasCardLayout->addWidget(canvasDiv);

    scene = new BlueprintScene(m_isLight, this);
    scene->setSceneRect(0, 0, CANVAS_WIDTH, 600);

    view = new QGraphicsView(scene, canvasCard);
    view->setObjectName("vdCanvasView");
    view->setFrameShape(QFrame::NoFrame);
    view->setRenderHint(QPainter::Antialiasing, true);
    view->setRenderHint(QPainter::TextAntialiasing, true);
    view->setDragMode(QGraphicsView::RubberBandDrag);
    view->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    view->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    view->installEventFilter(this);
    if (view->viewport()) {
        view->viewport()->installEventFilter(this);
    }
    canvasCardLayout->addWidget(view, 1);

    // Right Card: Block Inspector (320px)
    QFrame* inspCard = new QFrame(dialogRoot);
    inspCard->setProperty("vdCard", true);
    inspCard->setAttribute(Qt::WA_StyledBackground, true);
    inspCard->setFixedWidth(320);
    QVBoxLayout* inspCardLayout = new QVBoxLayout(inspCard);
    inspCardLayout->setContentsMargins(0, 0, 0, 0);
    inspCardLayout->setSpacing(0);

    QWidget* inspHeader = new QWidget(inspCard);
    inspHeader->setObjectName("vdPaneHeader");
    QVBoxLayout* ihLayout = new QVBoxLayout(inspHeader);
    ihLayout->setContentsMargins(14, 12, 14, 10);
    ihLayout->setSpacing(2);

    lblInspectorTitle = new QLabel(tr("Block Inspector"), inspHeader);
    lblInspectorTitle->setProperty("vdSectionTitle", true);
    lblInspectorSub = new QLabel(tr("Select a block on the canvas to configure"), inspHeader);
    lblInspectorSub->setProperty("vdHint", true);
    ihLayout->addWidget(lblInspectorTitle);
    ihLayout->addWidget(lblInspectorSub);
    inspCardLayout->addWidget(inspHeader);

    QFrame* inspDiv = new QFrame(inspCard);
    inspDiv->setProperty("vdDivider", true);
    inspDiv->setFixedHeight(1);
    inspCardLayout->addWidget(inspDiv);

    inspectorScroll = new QScrollArea(inspCard);
    inspectorScroll->setWidgetResizable(true);
    inspectorScroll->setFrameShape(QFrame::NoFrame);
    inspectorScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (inspectorScroll->viewport()) {
        inspectorScroll->viewport()->setObjectName("vdInspViewport");
    }

    inspectorBody = new QWidget(inspectorScroll);
    inspectorBody->setObjectName("vdInspBody");
    inspectorLayout = new QVBoxLayout(inspectorBody);
    inspectorLayout->setContentsMargins(14, 14, 14, 14);
    inspectorLayout->setSpacing(10);
    inspectorScroll->setWidget(inspectorBody);
    inspCardLayout->addWidget(inspectorScroll, 1);

    bodyRow->addWidget(libCard);
    bodyRow->addWidget(canvasCard, 1);
    bodyRow->addWidget(inspCard);
    rootLayout->addLayout(bodyRow, 1);

    // -------------------------------------------------------------------------
    // 3. Bottom Validation + Action Footer
    // -------------------------------------------------------------------------
    QHBoxLayout* footerRow = new QHBoxLayout();
    footerRow->setContentsMargins(0, 0, 0, 0);
    footerRow->setSpacing(10);

    lblValidationStatus = new QLabel(tr("Canvas is empty"), dialogRoot);
    lblValidationStatus->setProperty("vdStatusTone", "neutral");
    lblValidationStatus->setMinimumHeight(36);
    lblValidationStatus->setWordWrap(true);

    btnCancel = new QPushButton(tr("Cancel"), dialogRoot);
    btnCancel->setProperty("vdSecondary", true);
    btnCancel->setFixedHeight(36);
    btnCancel->setMinimumWidth(96);
    btnCancel->setCursor(Qt::PointingHandCursor);
    btnCancel->setFocusPolicy(Qt::NoFocus);

    btnApply = new QPushButton(tr("Apply to Contract Builder"), dialogRoot);
    btnApply->setProperty("vdPrimary", true);
    btnApply->setFixedHeight(36);
    btnApply->setMinimumWidth(190);
    btnApply->setCursor(Qt::PointingHandCursor);
    btnApply->setFocusPolicy(Qt::NoFocus);

    footerRow->addWidget(lblValidationStatus, 1);
    footerRow->addWidget(btnCancel);
    footerRow->addWidget(btnApply);
    rootLayout->addLayout(footerRow);

    // Signals
    connect(editLibrarySearch, &QLineEdit::textChanged, this, &VisualDesignerDialog::filterBlockLibrary);
    connect(btnLoadRecipe, &QPushButton::clicked, this, &VisualDesignerDialog::onLoadRecipeClicked);
    connect(btnAutoLayout, &QPushButton::clicked, this, &VisualDesignerDialog::onAutoLayoutClicked);
    connect(btnClearAll, &QPushButton::clicked, this, &VisualDesignerDialog::onClearCanvasClicked);
    connect(btnMoveUp, &QPushButton::clicked, this, &VisualDesignerDialog::onMoveUpClicked);
    connect(btnMoveDown, &QPushButton::clicked, this, &VisualDesignerDialog::onMoveDownClicked);
    connect(btnDuplicate, &QPushButton::clicked, this, &VisualDesignerDialog::onDuplicateClicked);
    connect(btnRemove, &QPushButton::clicked, this, &VisualDesignerDialog::onRemoveNodeClicked);
    connect(btnZoomOut, &QPushButton::clicked, this, &VisualDesignerDialog::onZoomOut);
    connect(btnZoomIn, &QPushButton::clicked, this, &VisualDesignerDialog::onZoomIn);
    connect(btnZoomFit, &QPushButton::clicked, this, &VisualDesignerDialog::onZoomFit);
    connect(scene, &QGraphicsScene::selectionChanged, this, &VisualDesignerDialog::onSceneSelectionChanged);
    connect(btnApply, &QPushButton::clicked, this, &VisualDesignerDialog::onApplyClicked);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
}

void VisualDesignerDialog::populateBlockLibrary()
{
    struct Spec {
        const char* categoryHeader;
        const char* role;
    };
    static const Spec specs[] = {
        {"SPENDING GUARDS", "lock-time"},
        {nullptr, "check-signature-verification"},
        {nullptr, "equalverify-checksig"},
        {nullptr, "multi-signature"},
        {nullptr, "hash160"},
        {"CONTROL FLOW", "if-condition"},
        {"ADAM MINING & CONSTANTS", "coin-lock-miner"},
        {nullptr, "pow-miner"},
        {nullptr, "number"}
    };

    for (const Spec& spec : specs) {
        if (spec.categoryHeader) {
            QLabel* catLbl = new QLabel(tr(spec.categoryHeader), libraryContainer);
            catLbl->setProperty("vdLibCategory", true);
            libraryLayout->addWidget(catLbl);
        }

        const QString roleStr = QString::fromLatin1(spec.role);
        QString title, category, desc;
        RoleMetadata(roleStr, title, category, desc);

        QPushButton* cardBtn = new QPushButton(libraryContainer);
        cardBtn->setProperty("vdLibCard", true);
        cardBtn->setProperty("vdCategory", category);
        cardBtn->setCursor(Qt::PointingHandCursor);
        cardBtn->setFocusPolicy(Qt::NoFocus);

        QVBoxLayout* cardL = new QVBoxLayout(cardBtn);
        cardL->setContentsMargins(10, 8, 10, 8);
        cardL->setSpacing(3);

        QHBoxLayout* topLine = new QHBoxLayout();
        topLine->setContentsMargins(0, 0, 0, 0);
        topLine->setSpacing(6);

        QFrame* dot = new QFrame(cardBtn);
        dot->setFixedSize(8, 8);
        const QColor dotCol = CategoryAccentColor(category, m_isLight);
        dot->setStyleSheet(QString("background-color: %1; border-radius: 4px; border: none;").arg(dotCol.name()));

        QLabel* lblTitle = new QLabel(title, cardBtn);
        lblTitle->setProperty("vdLibCardTitle", true);
        lblTitle->setAttribute(Qt::WA_TransparentForMouseEvents);

        QLabel* lblPlus = new QLabel(QStringLiteral("+"), cardBtn);
        lblPlus->setProperty("vdLibPlus", true);
        lblPlus->setAttribute(Qt::WA_TransparentForMouseEvents);

        topLine->addWidget(dot);
        topLine->addWidget(lblTitle, 1);
        topLine->addWidget(lblPlus);

        QLabel* lblDesc = new QLabel(desc, cardBtn);
        lblDesc->setProperty("vdLibCardDesc", true);
        lblDesc->setWordWrap(true);
        lblDesc->setAttribute(Qt::WA_TransparentForMouseEvents);

        QLabel* lblRole = new QLabel(roleStr, cardBtn);
        lblRole->setProperty("vdLibRole", true);
        lblRole->setAttribute(Qt::WA_TransparentForMouseEvents);

        cardL->addLayout(topLine);
        cardL->addWidget(lblDesc);
        cardL->addWidget(lblRole, 0, Qt::AlignLeft);

        cardBtn->setMinimumHeight(74);
        connect(cardBtn, &QPushButton::clicked, this, [this, roleStr]() {
            addBlockByRole(roleStr);
        });

        libraryLayout->addWidget(cardBtn);

        DesignerLibraryItem entry;
        entry.card = cardBtn;
        entry.role = roleStr;
        entry.title = title;
        entry.description = desc;
        entry.category = category;
        libraryEntries.append(entry);
    }
}

void VisualDesignerDialog::filterBlockLibrary(const QString& query)
{
    const QString q = query.trimmed();
    for (const DesignerLibraryItem& entry : libraryEntries) {
        if (!entry.card) continue;
        const bool match = q.isEmpty()
            || entry.title.contains(q, Qt::CaseInsensitive)
            || entry.description.contains(q, Qt::CaseInsensitive)
            || entry.role.contains(q, Qt::CaseInsensitive);
        entry.card->setVisible(match);
    }
}

void VisualDesignerDialog::addBlockByRole(const QString& role)
{
    const UniValue node = BuildDefaultNodeForRole(role);
    if (node.isNull() || !node.isObject()) return;

    currentActions.push_back(node);
    const int newIdx = static_cast<int>(currentActions.size()) - 1;
    rebuildScene(newIdx);
    if (newIdx >= 0 && newIdx < nodeItems.size()) {
        view->ensureVisible(nodeItems[newIdx], 20, 40);
    }
}

void VisualDesignerDialog::loadStarterRecipe(int recipeIdx)
{
    if (recipeIdx <= 0) return;

    UniValue next(UniValue::VARR);
    if (recipeIdx == 1) { // Time-Locked Vault
        next.push_back(BuildDefaultNodeForRole(QStringLiteral("lock-time")));
        next.push_back(BuildDefaultNodeForRole(QStringLiteral("check-signature-verification")));
    } else if (recipeIdx == 2) { // 2-of-3 Escrow Multi-Sig
        next.push_back(BuildDefaultNodeForRole(QStringLiteral("multi-signature")));
    } else if (recipeIdx == 3) { // Hash-Locked Claim (HTLC)
        next.push_back(BuildDefaultNodeForRole(QStringLiteral("hash160")));
        next.push_back(BuildDefaultNodeForRole(QStringLiteral("check-signature-verification")));
    } else if (recipeIdx == 4) { // IF/ELSE Recovery Switch
        next.push_back(BuildDefaultNodeForRole(QStringLiteral("if-condition")));
    } else if (recipeIdx == 5) { // ADAM Coin-Lock Miner
        next.push_back(BuildDefaultNodeForRole(QStringLiteral("coin-lock-miner")));
    }

    currentActions = next;
    rebuildScene(currentActions.empty() ? -1 : 0);
}

void VisualDesignerDialog::onLoadRecipeClicked()
{
    if (!comboRecipes) return;
    const int idx = comboRecipes->currentIndex();
    if (idx <= 0) {
        showThemedNotice(this, tr("Starter Recipes"), tr("Select a starter recipe from the dropdown first."));
        return;
    }
    loadStarterRecipe(idx);
    comboRecipes->setCurrentIndex(0);
}

void VisualDesignerDialog::rebuildScene(int selectIdx)
{
    m_rebuildingScene = true;
    scene->clear();
    nodeItems.clear();
    overlayItems.clear();

    if (currentActions.empty()) {
        // Draw friendly empty-canvas onboarding card
        const QRectF emptyRect((CANVAS_WIDTH - 360.0) * 0.5, 130.0, 360.0, 175.0);
        QGraphicsRectItem* box = scene->addRect(emptyRect,
            QPen(m_isLight ? QColor("#CBD5E1") : QColor("#253554"), 1.5, Qt::DashLine),
            QBrush(m_isLight ? QColor("#FFFFFF") : QColor("#131B2E")));
        Q_UNUSED(box);

        QGraphicsTextItem* head = scene->addText(tr("Your Contract Flow Is Empty"));
        QFont hf = head->font();
        hf.setPointSize(12);
        hf.setBold(true);
        head->setFont(hf);
        head->setDefaultTextColor(m_isLight ? QColor("#0F172A") : QColor("#F1F5F9"));
        head->setTextWidth(320.0);
        head->setPos(emptyRect.left() + 20.0, emptyRect.top() + 34.0);

        QGraphicsTextItem* sub = scene->addText(
            tr("Click any block in the Block Library on the left, or load a Starter Recipe from the toolbar above to begin."));
        QFont sf = sub->font();
        sf.setPointSize(9);
        sub->setFont(sf);
        sub->setDefaultTextColor(m_isLight ? QColor("#64748B") : QColor("#94A3B8"));
        sub->setTextWidth(320.0);
        sub->setPos(emptyRect.left() + 20.0, emptyRect.top() + 72.0);

        scene->setSceneRect(0, 0, CANVAS_WIDTH, 520.0);
        selectedIndex = -1;
        m_rebuildingScene = false;
        updateToolbarButtons();
        rebuildInspector();
        updateValidationStatus();
        return;
    }

    for (unsigned int i = 0; i < currentActions.size(); ++i) {
        const UniValue& act = currentActions[i];
        if (!act.isObject()) continue;
        const QString role = (act.exists("role") && act["role"].isStr())
            ? QString::fromStdString(act["role"].get_str()) : QStringLiteral("unknown");
        QString title, category, desc;
        RoleMetadata(role, title, category, desc);

        DesignerNodeItem* item = new DesignerNodeItem(
            static_cast<int>(i) + 1,
            role,
            title,
            category,
            FormatNodeDetails(act),
            act,
            m_isLight
        );
        scene->addItem(item);
        nodeItems.append(item);
    }

    relayoutFlowItems();

    if (selectIdx >= 0 && selectIdx < nodeItems.size()) {
        nodeItems[selectIdx]->setSelected(true);
        selectedIndex = selectIdx;
    } else {
        selectedIndex = -1;
    }

    m_rebuildingScene = false;
    updateToolbarButtons();
    rebuildInspector();
    updateValidationStatus();
}

void VisualDesignerDialog::relayoutFlowItems()
{
    for (QGraphicsItem* ov : overlayItems) {
        scene->removeItem(ov);
        delete ov;
    }
    overlayItems.clear();

    if (nodeItems.isEmpty()) return;

    const qreal centerX = FLOW_X + NODE_WIDTH * 0.5;
    const QColor pillBg = m_isLight ? QColor("#EEF3FA") : QColor("#1B2B4A");
    const QColor pillBrd = m_isLight ? QColor("#7695CD") : QColor("#4A679E");
    const QColor pillFg = m_isLight ? QColor("#3E5B94") : QColor("#A8C1EE");
    const QColor lineCol = m_isLight ? QColor("#7695CD") : QColor("#5576B2");

    auto drawTerminalPill = [&](qreal yTop, const QString& label) -> qreal {
        const qreal pw = 188.0;
        const qreal ph = 30.0;
        QPainterPath p;
        p.addRoundedRect(QRectF(centerX - pw * 0.5, yTop, pw, ph), 15.0, 15.0);
        QGraphicsPathItem* pillItem = scene->addPath(p, QPen(pillBrd, 1.3), QBrush(pillBg));
        pillItem->setZValue(-1);
        overlayItems.append(pillItem);

        QGraphicsTextItem* txt = scene->addText(label);
        QFont f = txt->font();
        f.setPointSize(8);
        f.setBold(true);
        txt->setFont(f);
        txt->setDefaultTextColor(pillFg);
        const QRectF br = txt->boundingRect();
        txt->setPos(centerX - br.width() * 0.5, yTop + (ph - br.height()) * 0.5);
        txt->setZValue(0);
        overlayItems.append(txt);
        return yTop + ph;
    };

    auto drawConnector = [&](qreal yFrom, qreal yTo) {
        if (yTo <= yFrom + 8.0) return;
        QGraphicsLineItem* line = scene->addLine(centerX, yFrom, centerX, yTo - 7.0,
                                                 QPen(lineCol, 2.0, Qt::SolidLine));
        line->setZValue(-2);
        overlayItems.append(line);

        QPolygonF arrow;
        arrow << QPointF(centerX, yTo)
              << QPointF(centerX - 5.5, yTo - 9.0)
              << QPointF(centerX + 5.5, yTo - 9.0);
        QGraphicsPolygonItem* tri = scene->addPolygon(arrow, QPen(Qt::NoPen), QBrush(lineCol));
        tri->setZValue(-2);
        overlayItems.append(tri);
    };

    qreal prevBottom = drawTerminalPill(24.0, QStringLiteral("START  \xE2\x80\xA2  INPUT SCRIPT"));
    qreal yCursor = prevBottom + 28.0;

    for (int i = 0; i < nodeItems.size(); ++i) {
        DesignerNodeItem* item = nodeItems[i];
        item->stepIndex = i + 1;
        item->setPos(FLOW_X, yCursor);
        item->update();

        drawConnector(prevBottom, yCursor);
        prevBottom = yCursor + item->rect().height();
        yCursor = prevBottom + NODE_V_GAP;
    }

    const qreal endY = prevBottom + 28.0;
    drawConnector(prevBottom, endY);
    const qreal finalBottom = drawTerminalPill(endY, QStringLiteral("SPEND AUTHORIZED"));

    scene->setSceneRect(0, 0, CANVAS_WIDTH, std::max<qreal>(540.0, finalBottom + 40.0));
}

void VisualDesignerDialog::syncOrderFromScenePositions()
{
    if (nodeItems.size() <= 1) {
        relayoutFlowItems();
        return;
    }

    DesignerNodeItem* previouslySelected = (selectedIndex >= 0 && selectedIndex < nodeItems.size())
        ? nodeItems[selectedIndex] : nullptr;

    QList<DesignerNodeItem*> sorted = nodeItems;
    std::sort(sorted.begin(), sorted.end(), [](DesignerNodeItem* a, DesignerNodeItem* b) {
        return a->y() < b->y();
    });

    bool changed = false;
    for (int i = 0; i < sorted.size(); ++i) {
        if (sorted[i] != nodeItems[i]) {
            changed = true;
            break;
        }
    }

    if (changed) {
        UniValue reordered(UniValue::VARR);
        for (DesignerNodeItem* item : sorted) {
            reordered.push_back(item->nodeData);
        }
        currentActions = reordered;
        nodeItems = sorted;
        if (previouslySelected) {
            selectedIndex = nodeItems.indexOf(previouslySelected);
        }
        relayoutFlowItems();
        updateToolbarButtons();
        rebuildInspector();
        updateValidationStatus();
    } else {
        relayoutFlowItems();
    }
}

void VisualDesignerDialog::updateNodeDisplay(int idx)
{
    if (idx < 0 || idx >= nodeItems.size() || idx >= static_cast<int>(currentActions.size())) {
        return;
    }
    const UniValue& act = currentActions[static_cast<unsigned int>(idx)];
    const qreal prevH = nodeItems[idx]->rect().height();
    nodeItems[idx]->updateContent(idx + 1, FormatNodeDetails(act), act, m_isLight);
    if (std::abs(nodeItems[idx]->rect().height() - prevH) > 0.5) {
        relayoutFlowItems();
    }
    updateValidationStatus();
}

void VisualDesignerDialog::onSceneSelectionChanged()
{
    if (m_rebuildingScene || m_isClosing || !scene) return;

    int nextIdx = -1;
    const QList<QGraphicsItem*> sel = scene->selectedItems();
    for (QGraphicsItem* gi : sel) {
        if (DesignerNodeItem* node = dynamic_cast<DesignerNodeItem*>(gi)) {
            nextIdx = nodeItems.indexOf(node);
            break;
        }
    }
    if (nextIdx != selectedIndex) {
        selectedIndex = nextIdx;
        updateToolbarButtons();
        rebuildInspector();
    }
}

void VisualDesignerDialog::updateToolbarButtons()
{
    const int n = nodeItems.size();
    const bool hasSel = (selectedIndex >= 0 && selectedIndex < n);
    if (btnMoveUp) btnMoveUp->setEnabled(hasSel && selectedIndex > 0);
    if (btnMoveDown) btnMoveDown->setEnabled(hasSel && selectedIndex < n - 1);
    if (btnDuplicate) btnDuplicate->setEnabled(hasSel);
    if (btnRemove) btnRemove->setEnabled(hasSel);
    if (btnAutoLayout) btnAutoLayout->setEnabled(n > 0);
    if (btnClearAll) btnClearAll->setEnabled(n > 0);
    if (lblBlockCount) {
        lblBlockCount->setText(n == 1 ? tr("1 block") : tr("%1 blocks").arg(n));
    }
}

void VisualDesignerDialog::onMoveUpClicked()
{
    if (selectedIndex <= 0 || selectedIndex >= static_cast<int>(currentActions.size())) return;
    UniValue arr(UniValue::VARR);
    for (unsigned int i = 0; i < currentActions.size(); ++i) {
        if (static_cast<int>(i) == selectedIndex - 1) {
            arr.push_back(currentActions[static_cast<unsigned int>(selectedIndex)]);
        } else if (static_cast<int>(i) == selectedIndex) {
            arr.push_back(currentActions[static_cast<unsigned int>(selectedIndex - 1)]);
        } else {
            arr.push_back(currentActions[i]);
        }
    }
    currentActions = arr;
    rebuildScene(selectedIndex - 1);
}

void VisualDesignerDialog::onMoveDownClicked()
{
    if (selectedIndex < 0 || selectedIndex + 1 >= static_cast<int>(currentActions.size())) return;
    UniValue arr(UniValue::VARR);
    for (unsigned int i = 0; i < currentActions.size(); ++i) {
        if (static_cast<int>(i) == selectedIndex) {
            arr.push_back(currentActions[static_cast<unsigned int>(selectedIndex + 1)]);
        } else if (static_cast<int>(i) == selectedIndex + 1) {
            arr.push_back(currentActions[static_cast<unsigned int>(selectedIndex)]);
        } else {
            arr.push_back(currentActions[i]);
        }
    }
    currentActions = arr;
    rebuildScene(selectedIndex + 1);
}

void VisualDesignerDialog::onDuplicateClicked()
{
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(currentActions.size())) return;
    UniValue arr(UniValue::VARR);
    for (unsigned int i = 0; i < currentActions.size(); ++i) {
        arr.push_back(currentActions[i]);
        if (static_cast<int>(i) == selectedIndex) {
            arr.push_back(currentActions[i]);
        }
    }
    currentActions = arr;
    rebuildScene(selectedIndex + 1);
}

void VisualDesignerDialog::onRemoveNodeClicked()
{
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(currentActions.size())) return;
    UniValue arr(UniValue::VARR);
    for (unsigned int i = 0; i < currentActions.size(); ++i) {
        if (static_cast<int>(i) != selectedIndex) {
            arr.push_back(currentActions[i]);
        }
    }
    currentActions = arr;
    const int nextIdx = std::min(selectedIndex, static_cast<int>(currentActions.size()) - 1);
    rebuildScene(nextIdx);
}

void VisualDesignerDialog::onAutoLayoutClicked()
{
    relayoutFlowItems();
}

void VisualDesignerDialog::onClearCanvasClicked()
{
    currentActions = UniValue(UniValue::VARR);
    rebuildScene(-1);
}

void VisualDesignerDialog::setZoom(double factor)
{
    const double clamped = std::max(0.55, std::min(1.6, factor));
    currentZoom = clamped;
    if (view) {
        QTransform tf;
        tf.scale(clamped, clamped);
        view->setTransform(tf);
    }
    if (lblZoomPct) {
        lblZoomPct->setText(QString("%1%").arg(qRound(clamped * 100.0)));
    }
}

void VisualDesignerDialog::onZoomIn() { setZoom(currentZoom + 0.15); }
void VisualDesignerDialog::onZoomOut() { setZoom(currentZoom - 0.15); }
void VisualDesignerDialog::onZoomReset() { setZoom(1.0); }
void VisualDesignerDialog::onZoomFit()
{
    setZoom(1.0);
    if (view && !nodeItems.isEmpty()) {
        view->centerOn(nodeItems.first());
    }
}

bool VisualDesignerDialog::eventFilter(QObject* obj, QEvent* event)
{
    if (m_isClosing || !scene) {
        return QDialog::eventFilter(obj, event);
    }
    if (view && obj == view->viewport()) {
        if (event->type() == QEvent::MouseButtonPress) {
            m_draggingNode = !scene->selectedItems().isEmpty();
        } else if (event->type() == QEvent::MouseButtonRelease && m_draggingNode) {
            m_draggingNode = false;
            syncOrderFromScenePositions();
        }
    }
    if ((obj == view || (view && obj == view->viewport())) && event->type() == QEvent::KeyPress) {
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);
        if ((ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace) && selectedIndex >= 0) {
            onRemoveNodeClicked();
            return true;
        }
    }
    return QDialog::eventFilter(obj, event);
}

void VisualDesignerDialog::rebuildInspector()
{
    if (!inspectorLayout || !inspectorBody) return;
    ClearLayoutItems(inspectorLayout);

    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(currentActions.size())) {
        if (lblInspectorTitle) lblInspectorTitle->setText(tr("Block Inspector"));
        if (lblInspectorSub) {
            lblInspectorSub->setText(currentActions.empty()
                ? tr("No blocks on canvas yet")
                : tr("Overview • Select a block to edit its parameters"));
        }

        QFrame* infoBox = new QFrame(inspectorBody);
        infoBox->setProperty("vdInfoCard", true);
        QVBoxLayout* il = new QVBoxLayout(infoBox);
        il->setContentsMargins(12, 10, 12, 10);
        il->setSpacing(6);

        QLabel* lblHow = new QLabel(tr("Quick Tips"), infoBox);
        lblHow->setProperty("vdFieldLabel", true);
        QLabel* lblTips = new QLabel(
            tr("• Click a block in the Library to add it with ready wallet defaults.\n"
               "• Click any canvas block to inspect and edit fields inline.\n"
               "• Drag blocks up or down (or press↑/↓) to reorder execution."),
            infoBox);
        lblTips->setProperty("vdHint", true);
        lblTips->setWordWrap(true);
        il->addWidget(lblHow);
        il->addWidget(lblTips);
        inspectorLayout->addWidget(infoBox);

        QLabel* lblPreviewKey = new QLabel(tr("Live MESCAL Specification Preview"), inspectorBody);
        lblPreviewKey->setProperty("vdFieldLabel", true);
        inspectorLayout->addWidget(lblPreviewKey);

        QPlainTextEdit* jsonPreview = new QPlainTextEdit(inspectorBody);
        jsonPreview->setProperty("vdTextArea", true);
        jsonPreview->setReadOnly(true);
        jsonPreview->setMinimumHeight(240);
        const UniValue doc = BuildMescalDocFromActions(currentActions);
        jsonPreview->setPlainText(QString::fromStdString(doc.write(2)));
        inspectorLayout->addWidget(jsonPreview, 1);
        return;
    }

    const int activeIdx = selectedIndex;
    const UniValue act = currentActions[static_cast<unsigned int>(activeIdx)];
    const QString role = (act.exists("role") && act["role"].isStr())
        ? QString::fromStdString(act["role"].get_str()) : QStringLiteral("unknown");

    QString title, category, desc;
    RoleMetadata(role, title, category, desc);

    if (lblInspectorTitle) lblInspectorTitle->setText(title);
    if (lblInspectorSub) lblInspectorSub->setText(QString("Step #%1  \xE2\x80\xA2  %2").arg(activeIdx + 1).arg(role));

    QFrame* descBox = new QFrame(inspectorBody);
    descBox->setProperty("vdInfoCard", true);
    QVBoxLayout* dl = new QVBoxLayout(descBox);
    dl->setContentsMargins(11, 9, 11, 9);
    QLabel* lblDesc = new QLabel(desc, descBox);
    lblDesc->setProperty("vdHint", true);
    lblDesc->setWordWrap(true);
    dl->addWidget(lblDesc);
    inspectorLayout->addWidget(descBox);

    if (role != "if-condition") {
        QWidget* formHost = new QWidget(inspectorBody);
        BuildBasicBlockInputsForm(formHost, act, [this, activeIdx](const UniValue& updatedAct) {
            if (activeIdx < 0 || activeIdx >= static_cast<int>(currentActions.size())) return;
            UniValue nextArr(UniValue::VARR);
            for (unsigned int i = 0; i < currentActions.size(); ++i) {
                nextArr.push_back(static_cast<int>(i) == activeIdx ? updatedAct : currentActions[i]);
            }
            currentActions = nextArr;
            updateNodeDisplay(activeIdx);
        });
        inspectorLayout->addWidget(formHost);
        inspectorLayout->addStretch(1);
        return;
    }

    // Structured Inline Editor for IF / THEN / ELSE
    auto sharedIf = std::make_shared<UniValue>(act);

    auto normalizeList = [](const UniValue& v) -> UniValue {
        UniValue out(UniValue::VARR);
        if (v.isArray()) return v;
        if (v.isObject() && v.exists("role")) out.push_back(v);
        return out;
    };

    auto commitIfChange = [this, activeIdx, sharedIf]() {
        if (activeIdx < 0 || activeIdx >= static_cast<int>(currentActions.size())) return;
        UniValue nextArr(UniValue::VARR);
        for (unsigned int i = 0; i < currentActions.size(); ++i) {
            nextArr.push_back(static_cast<int>(i) == activeIdx ? *sharedIf : currentActions[i]);
        }
        currentActions = nextArr;
        updateNodeDisplay(activeIdx);
    };

    auto buildBranchSection = [&](const QString& sectionHeading,
                                  const QString& primaryKey,
                                  const QString& legacyKey,
                                  bool allowNoneAndCompound) {
        QFrame* subCard = new QFrame(inspectorBody);
        subCard->setProperty("vdInfoCard", true);
        QVBoxLayout* sl = new QVBoxLayout(subCard);
        sl->setContentsMargins(11, 10, 11, 10);
        sl->setSpacing(8);

        QLabel* lblHead = new QLabel(sectionHeading, subCard);
        lblHead->setProperty("vdFieldLabel", true);
        sl->addWidget(lblHead);

        QComboBox* comboRole = new QComboBox(subCard);
        comboRole->setProperty("vdCombo", true);
        comboRole->setView(new QListView(comboRole));
        comboRole->setFixedHeight(32);

        if (allowNoneAndCompound) {
            comboRole->addItem(tr("None (Skip Branch)"), QStringLiteral("none"));
        }
        comboRole->addItem(tr("Signature Check"), QStringLiteral("check-signature-verification"));
        comboRole->addItem(tr("Hash-Locked (HTLC)"), QStringLiteral("hash160"));
        comboRole->addItem(tr("Address / PubKeyHash"), QStringLiteral("equalverify-checksig"));
        comboRole->addItem(tr("Multi-Signature (m-of-n)"), QStringLiteral("multi-signature"));
        comboRole->addItem(tr("Time-Locked"), QStringLiteral("lock-time"));
        if (allowNoneAndCompound) {
            comboRole->addItem(tr("Time-Lock + Signature"), QStringLiteral("lock-sig"));
        } else {
            comboRole->addItem(tr("Stack Number Constant"), QStringLiteral("number"));
        }

        const UniValue existingRaw = sharedIf->exists(primaryKey.toStdString())
            ? (*sharedIf)[primaryKey.toStdString()]
            : (sharedIf->exists(legacyKey.toStdString()) ? (*sharedIf)[legacyKey.toStdString()] : UniValue());
        UniValue curList = normalizeList(existingRaw);

        QString detectedKey = QStringLiteral("hash160");
        if (curList.empty()) {
            detectedKey = allowNoneAndCompound ? QStringLiteral("none") : QStringLiteral("hash160");
        } else if (allowNoneAndCompound && curList.size() >= 2 &&
                   curList[0]["role"].get_str() == "lock-time" &&
                   curList[1]["role"].get_str() == "check-signature-verification") {
            detectedKey = QStringLiteral("lock-sig");
        } else if (curList[0].exists("role") && curList[0]["role"].isStr()) {
            detectedKey = QString::fromStdString(curList[0]["role"].get_str());
        }

        {
            const QSignalBlocker b(comboRole);
            const int foundIdx = comboRole->findData(detectedKey);
            comboRole->setCurrentIndex(foundIdx >= 0 ? foundIdx : 0);
        }
        sl->addWidget(comboRole);

        QWidget* branchBody = new QWidget(subCard);
        QVBoxLayout* branchBodyLayout = new QVBoxLayout(branchBody);
        branchBodyLayout->setContentsMargins(0, 0, 0, 0);
        branchBodyLayout->setSpacing(8);
        sl->addWidget(branchBody);

        auto renderBranchFields = [=](const UniValue& branchList) {
            ClearLayoutItems(branchBodyLayout);
            for (unsigned int k = 0; k < branchList.size(); ++k) {
                const UniValue& itemObj = branchList[k];
                if (!itemObj.isObject()) continue;
                QWidget* itemHost = new QWidget(branchBody);
                BuildBasicBlockInputsForm(itemHost, itemObj, [=](const UniValue& updatedSub) {
                    const std::string pKey = primaryKey.toStdString();
                    UniValue latest = normalizeList(sharedIf->exists(pKey)
                        ? (*sharedIf)[pKey]
                        : (sharedIf->exists(legacyKey.toStdString()) ? (*sharedIf)[legacyKey.toStdString()] : UniValue()));
                    if (k < latest.size()) {
                        UniValue replaced(UniValue::VARR);
                        for (unsigned int r = 0; r < latest.size(); ++r) {
                            replaced.push_back(r == k ? updatedSub : latest[r]);
                        }
                        if (pKey == "expression" && replaced.size() == 1) {
                            sharedIf->pushKV(pKey, replaced[0]);
                        } else {
                            sharedIf->pushKV(pKey, replaced);
                        }
                        commitIfChange();
                    }
                });
                branchBodyLayout->addWidget(itemHost);
            }
        };

        renderBranchFields(curList);

        connect(comboRole, QOverload<int>::of(&QComboBox::currentIndexChanged), subCard, [=](int idx) {
            const QString key = comboRole->itemData(idx).toString();
            const std::string pKey = primaryKey.toStdString();
            UniValue nextBranch(UniValue::VARR);
            if (key == "lock-sig") {
                nextBranch.push_back(BuildDefaultNodeForRole(QStringLiteral("lock-time")));
                nextBranch.push_back(BuildDefaultNodeForRole(QStringLiteral("check-signature-verification")));
            } else if (key != "none") {
                nextBranch.push_back(BuildDefaultNodeForRole(key));
            }

            if (key == "none") {
                UniValue cleaned(UniValue::VOBJ);
                const std::vector<std::string>& keys = sharedIf->getKeys();
                for (const std::string& k : keys) {
                    if (k != pKey && k != legacyKey.toStdString()) {
                        cleaned.pushKV(k, (*sharedIf)[k]);
                    }
                }
                *sharedIf = cleaned;
            } else if (pKey == "expression" && nextBranch.size() == 1) {
                sharedIf->pushKV(pKey, nextBranch[0]);
            } else {
                sharedIf->pushKV(pKey, nextBranch);
            }
            renderBranchFields(nextBranch);
            commitIfChange();
        });

        inspectorLayout->addWidget(subCard);
    };

    buildBranchSection(tr("IF Condition"), QStringLiteral("expression"), QStringLiteral("expressions"), false);
    buildBranchSection(tr("THEN Branch (when TRUE)"), QStringLiteral("true_action"), QStringLiteral("true"), true);
    buildBranchSection(tr("ELSE Branch (when FALSE)"), QStringLiteral("false_action"), QStringLiteral("false"), true);

    inspectorLayout->addStretch(1);
}

void VisualDesignerDialog::updateValidationStatus()
{
    if (!lblValidationStatus) return;

    auto setTone = [this](const QString& tone, const QString& msg) {
        if (lblValidationStatus->property("vdStatusTone").toString() != tone) {
            lblValidationStatus->setProperty("vdStatusTone", tone);
            updateStyle(lblValidationStatus);
        }
        lblValidationStatus->setText(msg);
    };

    if (currentActions.empty()) {
        setTone(QStringLiteral("neutral"), tr("Canvas is empty — add blocks from the left library or load a recipe."));
        return;
    }

    const QString sanityErr = ValidateActionsSanity(currentActions);
    if (!sanityErr.isEmpty()) {
        setTone(QStringLiteral("error"), tr("Parameter warning: %1").arg(sanityErr));
        return;
    }

    const UniValue doc = BuildMescalDocFromActions(currentActions);
    std::string compileErr;
    const CScript compiled = CMescal::Compile(doc.write(), compileErr);
    if (!compileErr.empty()) {
        setTone(QStringLiteral("error"), tr("MESCAL error: %1").arg(QString::fromStdString(compileErr)));
        return;
    }

    const int n = static_cast<int>(currentActions.size());
    setTone(QStringLiteral("success"),
            tr("\xE2\x9C\x93 Valid MESCAL Contract  \xE2\x80\xA2  %1 bytes  \xE2\x80\xA2  %2 block(s)")
                .arg(compiled.size())
                .arg(n));
}

void VisualDesignerDialog::onApplyClicked()
{
    QList<DesignerNodeItem*> sorted = nodeItems;
    std::sort(sorted.begin(), sorted.end(), [](DesignerNodeItem* a, DesignerNodeItem* b) {
        return a->y() < b->y();
    });
    UniValue reordered(UniValue::VARR);
    for (DesignerNodeItem* item : sorted) {
        reordered.push_back(item->nodeData);
    }
    currentActions = reordered;
    accept();
}

void VisualDesignerDialog::applyTheme(bool isLight)
{
    m_isLight = isLight;
    if (scene) scene->setLightTheme(isLight);

    const QString canvasBg    = isLight ? QStringLiteral("#F8FAFC") : QStringLiteral("#0B1120");
    const QString cardBg      = isLight ? QStringLiteral("#FFFFFF") : QStringLiteral("#131B2E");
    const QString subCardBg   = isLight ? QStringLiteral("#F8FAFC") : QStringLiteral("#0F172A");
    const QString cardBrd     = isLight ? QStringLiteral("#CBD5E1") : QStringLiteral("#253554");
    const QString divider     = isLight ? QStringLiteral("#E2E8F0") : QStringLiteral("#1E2D4A");
    const QString titleCol    = isLight ? QStringLiteral("#0F172A") : QStringLiteral("#F1F5F9");
    const QString subCol      = isLight ? QStringLiteral("#475569") : QStringLiteral("#CBD5E1");
    const QString hintCol     = isLight ? QStringLiteral("#64748B") : QStringLiteral("#94A3B8");
    const QString inputBg     = isLight ? QStringLiteral("#FFFFFF") : QStringLiteral("#0D1424");

    const QString inputBrd    = isLight ? QStringLiteral("#CBD5E1") : QStringLiteral("#2D3F63");
    const QString priBg       = isLight ? QStringLiteral("#3E5B94") : QStringLiteral("#4A6AB0");
    const QString priHov      = isLight ? QStringLiteral("#2F4A7D") : QStringLiteral("#5576B2");
    const QString priPres     = isLight ? QStringLiteral("#253B66") : QStringLiteral("#3E5B94");
    const QString secBg       = isLight ? QStringLiteral("#FFFFFF") : QStringLiteral("#1A2540");
    const QString secFg       = isLight ? QStringLiteral("#334155") : QStringLiteral("#E2E8F0");
    const QString secHov      = isLight ? QStringLiteral("#EEF3FA") : QStringLiteral("#223254");
    const QString dropArrow   = isLight ? QStringLiteral("://ic-arrow-drop-down") : QStringLiteral("://ic-arrow-drop-down-white");

    const QString succBg      = isLight ? QStringLiteral("#EEF3FA") : QStringLiteral("#182642");
    const QString succFg      = isLight ? QStringLiteral("#3E5B94") : QStringLiteral("#A8C1EE");
    const QString succBrd     = isLight ? QStringLiteral("#B8CBE8") : QStringLiteral("#2E4775");
    const QString errBg       = isLight ? QStringLiteral("#FEF2F2") : QStringLiteral("#2E1619");
    const QString errFg       = isLight ? QStringLiteral("#C94A3E") : QStringLiteral("#FCA5A5");
    const QString errBrd      = isLight ? QStringLiteral("#F5B5AE") : QStringLiteral("#7F2D28");
    const QString disBg       = isLight ? QStringLiteral("#F1F5F9") : QStringLiteral("#111A2E");
    const QString disFg       = isLight ? QStringLiteral("#94A3B8") : QStringLiteral("#475569");

    QString css = QString(
        "QWidget#vdDialogRoot { background-color: %1; border: none; }"
        "QWidget#vdDialogRoot QWidget#vdPaneHeader { background-color: %2; border: none; }"
        "QWidget#vdDialogRoot QWidget#vdLibContainer, "
        "QWidget#vdDialogRoot QWidget#vdInspBody, "
        "QWidget#vdDialogRoot QWidget#vdLibViewport, "
        "QWidget#vdDialogRoot QWidget#vdInspViewport { background-color: %3; border: none; }"
        "QWidget#vdDialogRoot QFrame[vdCard=\"true\"] { background-color: %3; border: 1px solid %4; border-radius: 10px; }"
        "QWidget#vdDialogRoot QFrame[vdInfoCard=\"true\"] { background-color: %2; border: 1px solid %5; border-radius: 8px; }"
        "QWidget#vdDialogRoot QFrame[vdDivider=\"true\"] { background-color: %5; border: none; }"
    ).arg(canvasBg, subCardBg, cardBg, cardBrd, divider);

    css += QString(
        "QWidget#vdDialogRoot QLabel[vdTitle=\"true\"] { color: %1; font-size: 20px; font-weight: 500; background: transparent; }"
        "QWidget#vdDialogRoot QLabel[vdSubtitle=\"true\"] { color: %2; font-size: 12px; background: transparent; }"
        "QWidget#vdDialogRoot QLabel[vdSectionTitle=\"true\"] { color: %1; font-size: 14px; font-weight: 600; background: transparent; }"
        "QWidget#vdDialogRoot QLabel[vdFieldLabel=\"true\"] { color: %3; font-size: 12px; font-weight: 600; background: transparent; }"
        "QWidget#vdDialogRoot QLabel[vdHint=\"true\"] { color: %2; font-size: 11px; background: transparent; }"
        "QWidget#vdDialogRoot QLabel[vdLibCategory=\"true\"] { color: %2; font-size: 10px; font-weight: 700; letter-spacing: 0.6px; padding: 6px 2px 2px 2px; background: transparent; }"
        "QWidget#vdDialogRoot QLabel[vdCountPill=\"true\"] { background-color: %4; color: %5; border: 1px solid %6; border-radius: 10px; padding: 3px 10px; font-size: 11px; font-weight: 600; }"
    ).arg(titleCol, hintCol, subCol, succBg, succFg, succBrd);

    css += QString(
        "QWidget#vdDialogRoot QPushButton[vdLibCard=\"true\"] { background-color: %1; border: 1px solid %2; border-radius: 8px; text-align: left; }"
        "QWidget#vdDialogRoot QPushButton[vdLibCard=\"true\"]:hover { background-color: %3; border-color: #7695CD; }"
        "QWidget#vdDialogRoot QLabel[vdLibCardTitle=\"true\"] { color: %4; font-size: 12px; font-weight: 600; background: transparent; }"
        "QWidget#vdDialogRoot QLabel[vdLibPlus=\"true\"] { color: %5; font-size: 14px; font-weight: 700; background: transparent; }"
        "QWidget#vdDialogRoot QLabel[vdLibCardDesc=\"true\"] { color: %6; font-size: 11px; background: transparent; }"
        "QWidget#vdDialogRoot QLabel[vdLibRole=\"true\"] { color: %5; background-color: %7; border-radius: 4px; padding: 1px 6px; font-family: monospace; font-size: 10px; }"
    ).arg(subCardBg, divider, secHov, titleCol, succFg, hintCol, succBg);

    css += QString(
        "QWidget#vdDialogRoot QLineEdit[vdInput=\"true\"], "
        "QWidget#vdDialogRoot QSpinBox[vdSpin=\"true\"], "
        "QWidget#vdDialogRoot QPlainTextEdit[vdTextArea=\"true\"] { background-color: %1; color: %2; border: 1.5px solid %3; border-radius: 7px; padding: 4px 9px; font-size: 12px; }"
        "QWidget#vdDialogRoot QLineEdit[vdInput=\"true\"]:focus, "
        "QWidget#vdDialogRoot QSpinBox[vdSpin=\"true\"]:focus, "
        "QWidget#vdDialogRoot QPlainTextEdit[vdTextArea=\"true\"]:focus { border-color: #7695CD; }"
        "QWidget#vdDialogRoot QComboBox[vdCombo=\"true\"] { combobox-popup: 0; background-color: %1; color: %2; border: 1.5px solid %3; border-radius: 7px; padding: 3px 26px 3px 10px; font-size: 12px; font-weight: 600; }"
        "QWidget#vdDialogRoot QComboBox[vdCombo=\"true\"]:hover { border-color: #7695CD; }"
        "QWidget#vdDialogRoot QComboBox[vdCombo=\"true\"]::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 24px; border: none; background: transparent; }"
        "QWidget#vdDialogRoot QComboBox[vdCombo=\"true\"]::down-arrow { image: url(\"%4\"); width: 12px; height: 12px; }"
        "QWidget#vdDialogRoot QComboBox[vdCombo=\"true\"] QAbstractItemView { background-color: %5; color: %2; border: 1px solid %3; selection-background-color: #5576B2; selection-color: #FFFFFF; outline: none; }"
    ).arg(inputBg, titleCol, inputBrd, dropArrow, cardBg);

    css += QString(
        "QWidget#vdDialogRoot QPushButton[vdPrimary=\"true\"] { background-color: %1; color: #FFFFFF; border: 1px solid %1; border-radius: 7px; padding: 0 16px; font-size: 12px; font-weight: 600; }"
        "QWidget#vdDialogRoot QPushButton[vdPrimary=\"true\"]:hover { background-color: %2; border-color: %2; }"
        "QWidget#vdDialogRoot QPushButton[vdPrimary=\"true\"]:pressed { background-color: %3; }"
        "QWidget#vdDialogRoot QPushButton[vdSecondary=\"true\"] { background-color: %4; color: %5; border: 1px solid %6; border-radius: 7px; padding: 0 12px; font-size: 12px; font-weight: 600; }"
        "QWidget#vdDialogRoot QPushButton[vdSecondary=\"true\"]:hover { background-color: %7; border-color: #7695CD; }"
        "QWidget#vdDialogRoot QPushButton[vdSecondary=\"true\"]:disabled { background-color: %8; color: %9; border-color: %6; }"
    ).arg(priBg, priHov, priPres, secBg, secFg, inputBrd, secHov, disBg, disFg);

    css += QString(
        "QWidget#vdDialogRoot QPushButton[vdMiniBtn=\"true\"] { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 0 9px; font-size: 11px; font-weight: 600; }"
        "QWidget#vdDialogRoot QPushButton[vdMiniBtn=\"true\"]:hover { background-color: %4; border-color: #7695CD; }"
        "QWidget#vdDialogRoot QPushButton[vdMiniBtn=\"true\"]:disabled { background-color: %5; color: %6; border-color: %3; }"
        "QWidget#vdDialogRoot QPushButton[vdDangerBtn=\"true\"] { background-color: %7; color: %8; border: 1px solid %9; border-radius: 6px; padding: 0 9px; font-size: 11px; font-weight: 600; }"
    ).arg(secBg, secFg, inputBrd, secHov, disBg, disFg, errBg, errFg, errBrd);

    css += QString(
        "QWidget#vdDialogRoot QPushButton[vdDangerBtn=\"true\"]:hover { background-color: #C94A3E; color: #FFFFFF; border-color: #9E3329; }"
        "QWidget#vdDialogRoot QPushButton[vdDangerBtn=\"true\"]:disabled { background-color: %1; color: %2; border-color: %3; }"
        "QWidget#vdDialogRoot QLabel[vdStatusTone=\"neutral\"] { background-color: %4; color: %5; border: 1px solid %3; border-radius: 7px; padding: 6px 12px; font-size: 12px; font-weight: 600; }"
        "QWidget#vdDialogRoot QLabel[vdStatusTone=\"success\"] { background-color: %6; color: %7; border: 1px solid %8; border-radius: 7px; padding: 6px 12px; font-size: 12px; font-weight: 600; }"
        "QWidget#vdDialogRoot QLabel[vdStatusTone=\"error\"] { background-color: %9; color: #C94A3E; border: 1px solid #F5B5AE; border-radius: 7px; padding: 6px 12px; font-size: 12px; font-weight: 600; }"
    ).arg(disBg, disFg, inputBrd, secBg, subCol, succBg, succFg, succBrd, errBg);

    if (!isLight) {
        css += QString(
            "QWidget#vdDialogRoot QLabel[vdStatusTone=\"error\"] { color: %1; border-color: %2; }"
        ).arg(errFg, errBrd);
    }

    dialogRoot->setStyleSheet(css);
}
