// Copyright (c) 2026 The KristaTech developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/visualdesignerdialog.h"
#include "qt/kristatech/qtutils.h"
#include "main.h"
#include "utilstrencodings.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QPainter>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsLineItem>
#include <QDebug>

static QString GetDefaultWalletPubkeyHex()
{
#ifdef ENABLE_WALLET
    if (pwalletMain) {
        CPubKey pubKey;
        if (pwalletMain->GetKeyFromPool(pubKey) && pubKey.IsValid()) {
            return QString::fromStdString(HexStr(pubKey.begin(), pubKey.end()));
        }
    }
#endif
    return "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f";
}

static QString GetDefaultWalletPubkeyHashHex()
{
#ifdef ENABLE_WALLET
    if (pwalletMain) {
        CPubKey pubKey;
        if (pwalletMain->GetKeyFromPool(pubKey) && pubKey.IsValid()) {
            return QString::fromStdString(HexStr(pubKey.GetID()));
        }
    }
#endif
    return "b5a9c9f285d893ce71ab9de8f5c09d765ee982ba";
}

static int GetDefaultLockHeight()
{
    int h = chainActive.Height();
    return (h > 0) ? (h + 100) : 150;
}

DesignerNodeItem::DesignerNodeItem(const QString& name, const QString& details, const UniValue& data, QGraphicsItem* parent)
    : QGraphicsRectItem(parent), name(name), details(details), nodeData(data)
{
    int lineCount = details.isEmpty() ? 0 : (details.count('\n') + 1);
    int h = std::max(70, 36 + lineCount * 15);
    setRect(0, 0, 240, h);
    setFlag(QGraphicsItem::ItemIsMovable);
    setFlag(QGraphicsItem::ItemIsSelectable);
}

void DesignerNodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    // Draw rounded background block
    painter->setRenderHint(QPainter::Antialiasing);
    
    QPen pen(QColor("#26a69a"));
    if (isSelected()) {
        pen.setColor(QColor("#ff9100"));
        pen.setWidth(3);
    } else {
        pen.setWidth(2);
    }
    
    painter->setPen(pen);
    painter->setBrush(QBrush(QColor("#1b1b2a")));
    painter->drawRoundedRect(rect(), 8, 8);
    
    // Draw Name
    painter->setPen(QPen(QColor("#ffffff")));
    QFont fontName = painter->font();
    fontName.setBold(true);
    painter->setFont(fontName);
    painter->drawText(QRectF(10, 8, rect().width() - 20, 20), Qt::AlignLeft, name);
    
    // Draw Details
    painter->setPen(QPen(QColor("#b0bec5")));
    QFont fontDetails = painter->font();
    fontDetails.setBold(false);
    fontDetails.setPointSize(8);
    painter->setFont(fontDetails);
    painter->drawText(QRectF(10, 30, rect().width() - 20, rect().height() - 34), Qt::AlignLeft | Qt::TextWordWrap, details);
}

VisualDesignerDialog::VisualDesignerDialog(const UniValue& initialActions, QWidget* parent)
    : QDialog(parent), currentActions(initialActions)
{
    setWindowTitle(tr("Visual Smart Contract Designer"));
    resize(1000, 700);
    
    setupLayout();
    rebuildScene();
}

VisualDesignerDialog::~VisualDesignerDialog()
{
    delete scene;
}

void VisualDesignerDialog::setupLayout()
{
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    
    // Left: visual graphics scene
    scene = new QGraphicsScene(this);
    scene->setSceneRect(0, 0, 600, 800);
    scene->setBackgroundBrush(QBrush(QColor("#0d0d1b")));
    
    view = new QGraphicsView(scene, this);
    view->setRenderHint(QPainter::Antialiasing);
    view->setDragMode(QGraphicsView::RubberBandDrag);
    view->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    
    mainLayout->addWidget(view, 3);
    
    // Right: control panel
    QVBoxLayout* rightLayout = new QVBoxLayout();
    
    listNodes = new QListWidget(this);
    listNodes->addItem(tr("Time-Locked (lock-time)"));
    listNodes->addItem(tr("Signature Check (check-signature-verification)"));
    listNodes->addItem(tr("Address / PubKeyHash Check (equalverify-checksig)"));
    listNodes->addItem(tr("Multi-Signature (multi-signature)"));
    listNodes->addItem(tr("Hash-Locked (hash160)"));
    listNodes->addItem(tr("IF-Condition (if-condition)"));
    listNodes->addItem(tr("Miner Registration: Coin-Lock (coin-lock-miner)"));
    listNodes->addItem(tr("Miner Registration: PoW-Lock (pow-miner)"));
    listNodes->addItem(tr("Push Number / True (number)"));
    listNodes->setCurrentRow(0);
    
    rightLayout->addWidget(listNodes);
    
    btnAdd = new QPushButton(tr("+ Add Selected to Canvas"), this);
    btnEdit = new QPushButton(tr("✎ Edit Selected Block"), this);
    btnRemove = new QPushButton(tr("- Delete Selected Block"), this);
    btnApply = new QPushButton(tr("Apply Changes"), this);
    btnCancel = new QPushButton(tr("Cancel"), this);
    
    rightLayout->addWidget(btnAdd);
    rightLayout->addWidget(btnEdit);
    rightLayout->addWidget(btnRemove);
    rightLayout->addStretch();
    rightLayout->addWidget(btnApply);
    rightLayout->addWidget(btnCancel);
    
    mainLayout->addLayout(rightLayout, 1);
    
    connect(listNodes, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { onAddNodeClicked(); });
    connect(btnAdd, &QPushButton::clicked, this, &VisualDesignerDialog::onAddNodeClicked);
    connect(btnEdit, &QPushButton::clicked, this, &VisualDesignerDialog::onEditNodeClicked);
    connect(btnRemove, &QPushButton::clicked, this, &VisualDesignerDialog::onRemoveNodeClicked);
    connect(btnApply, &QPushButton::clicked, this, &VisualDesignerDialog::onApplyClicked);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
}

void VisualDesignerDialog::rebuildScene()
{
    qDebug() << "VisualDesignerDialog::rebuildScene() - currentActions size:" << currentActions.size();
    scene->clear();
    nodeItems.clear();
    
    int yOffset = 40;
    const int nodeX = 180;
    const int nodeWidth = 240;
    const int centerX = nodeX + nodeWidth / 2;
    
    for (unsigned int i = 0; i < currentActions.size(); i++) {
        try {
            const UniValue& act = currentActions[i];
            if (!act.isObject()) continue;

            std::string role = (act.exists("role") && act["role"].isStr()) ? act["role"].get_str() : "unknown";
            
            QString qName = QString::fromStdString(role);
            QString qDetails = "";
            
            if (role == "if-condition") {
                qName = "IF-Condition";
                
                // 1. Expression
                QString exprStr = "";
                if (act.exists("expression")) {
                    const UniValue& expr = act["expression"];
                    if (expr.isArray()) {
                        QStringList exprList;
                        for (unsigned int k = 0; k < expr.size(); ++k) {
                            if (expr[k].isObject() && expr[k].exists("role") && expr[k]["role"].isStr())
                                exprList << QString::fromStdString(expr[k]["role"].get_str());
                        }
                        exprStr = exprList.join(" AND ");
                    } else if (expr.isObject() && expr.exists("role") && expr["role"].isStr()) {
                        exprStr = QString::fromStdString(expr["role"].get_str());
                    }
                }
                
                // 2. True Action
                QString trueStr = "";
                if (act.exists("true_action")) {
                    const UniValue& trueAct = act["true_action"];
                    if (trueAct.isArray()) {
                        QStringList trueList;
                        for (unsigned int k = 0; k < trueAct.size(); ++k) {
                            if (trueAct[k].isObject() && trueAct[k].exists("role") && trueAct[k]["role"].isStr())
                                trueList << QString::fromStdString(trueAct[k]["role"].get_str());
                        }
                        trueStr = trueList.join(", ");
                    } else if (trueAct.isObject() && trueAct.exists("role") && trueAct["role"].isStr()) {
                        trueStr = QString::fromStdString(trueAct["role"].get_str());
                    }
                }
                
                qDetails = QString("IF [%1]\nTHEN [%2]").arg(exprStr).arg(trueStr);
                
                // 3. False Action
                if (act.exists("false_action")) {
                    const UniValue& falseAct = act["false_action"];
                    QString falseStr = "";
                    if (falseAct.isArray()) {
                        QStringList falseList;
                        for (unsigned int k = 0; k < falseAct.size(); ++k) {
                            if (falseAct[k].isObject() && falseAct[k].exists("role") && falseAct[k]["role"].isStr())
                                falseList << QString::fromStdString(falseAct[k]["role"].get_str());
                        }
                        falseStr = falseList.join(", ");
                    } else if (falseAct.isObject() && falseAct.exists("role") && falseAct["role"].isStr()) {
                        falseStr = QString::fromStdString(falseAct["role"].get_str());
                    }
                    if (!falseStr.isEmpty() || falseAct.isArray()) {
                        qDetails += QString("\nELSE [%1]").arg(falseStr);
                    }
                }
            } else {
                if (act.exists("inputs")) {
                    const UniValue& inputs = act["inputs"];
                    if (inputs.isArray() && !inputs.empty()) {
                        for (unsigned int j = 0; j < inputs.size(); j++) {
                            if (!inputs[j].isObject()) continue;
                            std::string nameStr = (inputs[j].exists("name") && inputs[j]["name"].isStr()) ? inputs[j]["name"].get_str() : "";
                            std::string valStr = "";
                            if (inputs[j].exists("value")) {
                                const UniValue& valUni = inputs[j]["value"];
                                if (valUni.isStr() || valUni.isNum() || valUni.isBool()) {
                                    valStr = valUni.getValStr();
                                } else if (valUni.isArray()) {
                                    valStr = QString("[%1 items]").arg(valUni.size()).toStdString();
                                } else {
                                    valStr = valUni.write();
                                }
                            }
                            if (!nameStr.empty()) {
                                qDetails += QString("%1: %2\n").arg(QString::fromStdString(nameStr)).arg(QString::fromStdString(valStr));
                            }
                        }
                    }
                }
            }
            
            DesignerNodeItem* item = new DesignerNodeItem(qName, qDetails.trimmed(), act);
            item->setPos(nodeX, yOffset);
            scene->addItem(item);
            nodeItems.append(item);
            
            // Draw connector line from previous item bottom to current item top
            if (nodeItems.size() > 1) {
                DesignerNodeItem* prevItem = nodeItems[nodeItems.size() - 2];
                qreal prevBottom = prevItem->y() + prevItem->rect().height();
                QGraphicsLineItem* line = new QGraphicsLineItem(centerX, prevBottom, centerX, yOffset);
                line->setPen(QPen(QColor("#26a69a"), 2, Qt::DashLine));
                scene->addItem(line);
            }
            
            yOffset += item->rect().height() + 30;
        } catch (const std::exception& e) {
            qWarning() << "Error rendering node in rebuildScene:" << e.what();
        }
    }
    scene->setSceneRect(0, 0, 600, std::max<qreal>(800.0, yOffset + 60.0));
}

void VisualDesignerDialog::onAddNodeClicked()
{
    int row = listNodes->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, tr("Selection Required"), tr("Please select a block type to add from the list."));
        return;
    }
    
    UniValue node(UniValue::VOBJ);
    UniValue inputs(UniValue::VARR);
    QString defaultPubkey = GetDefaultWalletPubkeyHex();
    QString defaultPubkeyHash = GetDefaultWalletPubkeyHashHex();
    int defaultLock = GetDefaultLockHeight();
    
    if (row == 0) { // Time-Locked (lock-time)
        bool ok;
        int64_t lockTime = QInputDialog::getInt(this, tr("Time-Locked"), tr("Lock Until (Block Height or Timestamp):"), defaultLock, 0, 2000000000, 1, &ok);
        if (!ok) return;
        
        node.pushKV("role", "lock-time");
        UniValue inp(UniValue::VOBJ);
        inp.pushKV("name", "Lock-Until");
        inp.pushKV("type", "timestamp-or-block-height");
        inp.pushKV("value", lockTime);
        inputs.push_back(inp);
    }
    else if (row == 1) { // Signature Check (check-signature-verification)
        bool ok;
        QString pubkey = QInputDialog::getText(this, tr("Signature Check"), tr("Owner Public Key (Hex):"), QLineEdit::Normal, defaultPubkey, &ok);
        if (!ok || pubkey.isEmpty()) return;
        
        node.pushKV("role", "check-signature-verification");
        UniValue inp(UniValue::VOBJ);
        inp.pushKV("name", "Pubkey");
        inp.pushKV("type", "pubkey");
        inp.pushKV("value", pubkey.trimmed().toStdString());
        inputs.push_back(inp);
    }
    else if (row == 2) { // Address / PubKeyHash Check (equalverify-checksig)
        bool ok;
        QString pubkeyHash = QInputDialog::getText(this, tr("Address / PubKeyHash Check"), tr("PubKeyHash (40-char Hex):"), QLineEdit::Normal, defaultPubkeyHash, &ok);
        if (!ok || pubkeyHash.isEmpty()) return;
        
        node.pushKV("role", "equalverify-checksig");
        UniValue inp(UniValue::VOBJ);
        inp.pushKV("name", "PubKeyHash");
        inp.pushKV("type", "string");
        inp.pushKV("value", pubkeyHash.trimmed().toStdString());
        inputs.push_back(inp);
    }
    else if (row == 3) { // Multi-Signature (multi-signature)
        bool ok1, ok2, ok3;
        int m = QInputDialog::getInt(this, tr("Multi-Signature"), tr("Required Signatures (m):"), 2, 1, 20, 1, &ok1);
        if (!ok1) return;
        int n = QInputDialog::getInt(this, tr("Multi-Signature"), tr("Total Keys (n):"), 3, 1, 20, 1, &ok2);
        if (!ok2) return;
        QString keysStr = QInputDialog::getText(this, tr("Multi-Signature"), tr("Public Keys (space-separated):"), QLineEdit::Normal, defaultPubkey, &ok3);
        if (!ok3 || keysStr.isEmpty()) return;
        
        node.pushKV("role", "multi-signature");
        UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", m); inputs.push_back(inpM);
        UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", n); inputs.push_back(inpN);
        
        UniValue keysArray(UniValue::VARR);
        QStringList keysList = keysStr.split(' ', QString::SkipEmptyParts);
        for (const QString& key : keysList) {
            keysArray.push_back(key.trimmed().toStdString());
        }
        UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); inputs.push_back(inpSigs);
    }
    else if (row == 4) { // Hash-Locked (hash160)
        bool ok1, ok2;
        QString hashHex = QInputDialog::getText(this, tr("Hash-Locked"), tr("Hash160 of Preimage (Hex):"), QLineEdit::Normal, "", &ok1);
        if (!ok1 || hashHex.isEmpty()) return;
        QString pubkey = QInputDialog::getText(this, tr("Hash-Locked"), tr("Recipient Pubkey (Hex):"), QLineEdit::Normal, defaultPubkey, &ok2);
        if (!ok2 || pubkey.isEmpty()) return;
        
        node.pushKV("role", "hash160");
        UniValue inpHash(UniValue::VOBJ);
        inpHash.pushKV("name", "Hash160");
        inpHash.pushKV("type", "string-or-number");
        inpHash.pushKV("value", hashHex.trimmed().toStdString());
        inputs.push_back(inpHash);
        node.pushKV("inputs", inputs);
        
        currentActions.push_back(node);
        
        // Also automatically add check-signature-verification for Hash-Locked recipient sig
        UniValue sigNode(UniValue::VOBJ);
        sigNode.pushKV("role", "check-signature-verification");
        UniValue inpSig(UniValue::VOBJ);
        inpSig.pushKV("name", "Pubkey");
        inpSig.pushKV("type", "pubkey");
        inpSig.pushKV("value", pubkey.trimmed().toStdString());
        UniValue sigInputs(UniValue::VARR);
        sigInputs.push_back(inpSig);
        sigNode.pushKV("inputs", sigInputs);
        
        currentActions.push_back(sigNode);
        rebuildScene();
        return;
    }
    else if (row == 5) { // IF-Condition (if-condition)
        bool ok;
        QStringList exprTypes;
        exprTypes << tr("Hash-Locked (hash160)")
                  << tr("Signature Check (check-signature-verification)")
                  << tr("Stack Value / Number (number)");
        QString exprType = getCustomItem(this, tr("IF-Condition - Expression"), tr("Select condition check type:"), exprTypes, 0, false, &ok);
        if (!ok || exprType.isEmpty()) return;

        UniValue exprAct(UniValue::VOBJ);
        UniValue exprInputs(UniValue::VARR);
        if (exprType.contains("check-signature-verification")) {
            QString pubkey = QInputDialog::getText(this, tr("Expression - Signature Check"), tr("Owner Public Key (Hex):"), QLineEdit::Normal, defaultPubkey, &ok);
            if (!ok || pubkey.isEmpty()) return;
            exprAct.pushKV("role", "check-signature-verification");
            UniValue inp(UniValue::VOBJ); inp.pushKV("name", "Pubkey"); inp.pushKV("type", "pubkey"); inp.pushKV("value", pubkey.trimmed().toStdString());
            exprInputs.push_back(inp);
        } else if (exprType.contains("number")) {
            int val = QInputDialog::getInt(this, tr("Expression - Number"), tr("Integer Value (1 = TRUE, 0 = FALSE):"), 1, -1, 16, 1, &ok);
            if (!ok) return;
            exprAct.pushKV("role", "number");
            UniValue inp(UniValue::VOBJ); inp.pushKV("name", "Value"); inp.pushKV("type", "number"); inp.pushKV("value", val);
            exprInputs.push_back(inp);
        } else {
            bool ok1;
            QString hashHex = QInputDialog::getText(this, tr("Expression - Hash-Locked"), tr("Hash160 of Preimage (Hex):"), QLineEdit::Normal, "", &ok1);
            if (!ok1 || hashHex.isEmpty()) return;
            exprAct.pushKV("role", "hash160");
            UniValue inp(UniValue::VOBJ); inp.pushKV("name", "Hash160"); inp.pushKV("type", "string-or-number"); inp.pushKV("value", hashHex.trimmed().toStdString());
            exprInputs.push_back(inp);
        }
        exprAct.pushKV("inputs", exprInputs);

        QStringList actionTypes;
        actionTypes << tr("Signature Check (check-signature-verification)")
                    << tr("Time-Locked + Signature Check (lock-time + check-signature-verification)")
                    << tr("Address / PubKeyHash Check (equalverify-checksig)")
                    << tr("Multi-Signature (multi-signature)")
                    << tr("Time-Locked (lock-time)")
                    << tr("Hash-Locked (hash160)");
        QString trueType = getCustomItem(this, tr("IF-Condition - True Branch"), tr("Select action when TRUE:"), actionTypes, 0, false, &ok);
        if (!ok || trueType.isEmpty()) return;

        UniValue trueBranch(UniValue::VARR);
        if (trueType.contains("lock-time + check-signature-verification")) {
            int64_t lockTime = QInputDialog::getInt(this, tr("True Branch - Time-Locked"), tr("Lock Until (Block or Timestamp):"), defaultLock, 0, 2000000000, 1, &ok);
            if (!ok) return;
            QString pubkey = QInputDialog::getText(this, tr("True Branch - Signature Check"), tr("Public Key (Hex):"), QLineEdit::Normal, defaultPubkey, &ok);
            if (!ok || pubkey.isEmpty()) return;
            UniValue ltAct(UniValue::VOBJ), ltInputs(UniValue::VARR), ltInp(UniValue::VOBJ);
            ltAct.pushKV("role", "lock-time");
            ltInp.pushKV("name", "Lock-Until"); ltInp.pushKV("type", "timestamp-or-block-height"); ltInp.pushKV("value", lockTime);
            ltInputs.push_back(ltInp); ltAct.pushKV("inputs", ltInputs);
            trueBranch.push_back(ltAct);

            UniValue sigAct(UniValue::VOBJ), sigInputs(UniValue::VARR), sigInp(UniValue::VOBJ);
            sigAct.pushKV("role", "check-signature-verification");
            sigInp.pushKV("name", "Pubkey"); sigInp.pushKV("type", "pubkey"); sigInp.pushKV("value", pubkey.trimmed().toStdString());
            sigInputs.push_back(sigInp); sigAct.pushKV("inputs", sigInputs);
            trueBranch.push_back(sigAct);
        } else {
            UniValue trueAct(UniValue::VOBJ);
            UniValue trueInputs(UniValue::VARR);
            if (trueType.contains("lock-time")) {
                int64_t lockTime = QInputDialog::getInt(this, tr("True Branch - Time-Locked"), tr("Lock Until (Block or Timestamp):"), defaultLock, 0, 2000000000, 1, &ok);
                if (!ok) return;
                trueAct.pushKV("role", "lock-time");
                UniValue inp(UniValue::VOBJ); inp.pushKV("name", "Lock-Until"); inp.pushKV("type", "timestamp-or-block-height"); inp.pushKV("value", lockTime); trueInputs.push_back(inp);
            } else if (trueType.contains("check-signature-verification")) {
                QString pubkey = QInputDialog::getText(this, tr("True Branch - Signature Check"), tr("Owner Public Key (Hex):"), QLineEdit::Normal, defaultPubkey, &ok);
                if (!ok || pubkey.isEmpty()) return;
                trueAct.pushKV("role", "check-signature-verification");
                UniValue inp(UniValue::VOBJ); inp.pushKV("name", "Pubkey"); inp.pushKV("type", "pubkey"); inp.pushKV("value", pubkey.trimmed().toStdString()); trueInputs.push_back(inp);
            } else if (trueType.contains("equalverify-checksig")) {
                QString pkh = QInputDialog::getText(this, tr("True Branch - PubKeyHash Check"), tr("PubKeyHash (40-char Hex):"), QLineEdit::Normal, defaultPubkeyHash, &ok);
                if (!ok || pkh.isEmpty()) return;
                trueAct.pushKV("role", "equalverify-checksig");
                UniValue inp(UniValue::VOBJ); inp.pushKV("name", "PubKeyHash"); inp.pushKV("type", "string"); inp.pushKV("value", pkh.trimmed().toStdString()); trueInputs.push_back(inp);
            } else if (trueType.contains("multi-signature")) {
                bool ok1, ok2, ok3;
                int m = QInputDialog::getInt(this, tr("True Branch - Multi-Signature"), tr("Required Signatures (m):"), 2, 1, 20, 1, &ok1);
                if (!ok1) return;
                int n = QInputDialog::getInt(this, tr("True Branch - Multi-Signature"), tr("Total Keys (n):"), 3, 1, 20, 1, &ok2);
                if (!ok2) return;
                QString keysStr = QInputDialog::getText(this, tr("True Branch - Multi-Signature"), tr("Public Keys (space-separated):"), QLineEdit::Normal, defaultPubkey, &ok3);
                if (!ok3 || keysStr.isEmpty()) return;
                trueAct.pushKV("role", "multi-signature");
                UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", m); trueInputs.push_back(inpM);
                UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", n); trueInputs.push_back(inpN);
                UniValue keysArray(UniValue::VARR);
                QStringList keysList = keysStr.split(' ', QString::SkipEmptyParts);
                for (const QString& key : keysList) keysArray.push_back(key.trimmed().toStdString());
                UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); trueInputs.push_back(inpSigs);
            } else if (trueType.contains("hash160")) {
                bool ok1;
                QString hashHex = QInputDialog::getText(this, tr("True Branch - Hash-Locked"), tr("Hash160 of Preimage (Hex):"), QLineEdit::Normal, "", &ok1);
                if (!ok1 || hashHex.isEmpty()) return;
                trueAct.pushKV("role", "hash160");
                UniValue inpHash(UniValue::VOBJ); inpHash.pushKV("name", "Hash160"); inpHash.pushKV("type", "string-or-number"); inpHash.pushKV("value", hashHex.trimmed().toStdString()); trueInputs.push_back(inpHash);
            }
            trueAct.pushKV("inputs", trueInputs);
            trueBranch.push_back(trueAct);
        }

        QStringList falseActionTypes;
        falseActionTypes << tr("Time-Locked + Signature Check (lock-time + check-signature-verification)")
                         << tr("Signature Check (check-signature-verification)")
                         << tr("Address / PubKeyHash Check (equalverify-checksig)")
                         << tr("Multi-Signature (multi-signature)")
                         << tr("Time-Locked (lock-time)")
                         << tr("Hash-Locked (hash160)")
                         << tr("None");
        QString falseType = getCustomItem(this, tr("IF-Condition - False Branch"), tr("Select action when FALSE:"), falseActionTypes, 0, false, &ok);
        if (!ok || falseType.isEmpty()) return;

        UniValue falseBranch(UniValue::VARR);
        bool hasFalse = false;
        if (falseType.contains("lock-time + check-signature-verification")) {
            int64_t lockTime = QInputDialog::getInt(this, tr("False Branch - Time-Locked"), tr("Lock Until (Block or Timestamp):"), defaultLock, 0, 2000000000, 1, &ok);
            if (!ok) return;
            QString pubkey = QInputDialog::getText(this, tr("False Branch - Signature Check"), tr("Refund Public Key (Hex):"), QLineEdit::Normal, defaultPubkey, &ok);
            if (!ok || pubkey.isEmpty()) return;
            UniValue ltAct(UniValue::VOBJ), ltInputs(UniValue::VARR), ltInp(UniValue::VOBJ);
            ltAct.pushKV("role", "lock-time");
            ltInp.pushKV("name", "Lock-Until"); ltInp.pushKV("type", "timestamp-or-block-height"); ltInp.pushKV("value", lockTime);
            ltInputs.push_back(ltInp); ltAct.pushKV("inputs", ltInputs);
            falseBranch.push_back(ltAct);

            UniValue sigAct(UniValue::VOBJ), sigInputs(UniValue::VARR), sigInp(UniValue::VOBJ);
            sigAct.pushKV("role", "check-signature-verification");
            sigInp.pushKV("name", "Pubkey"); sigInp.pushKV("type", "pubkey"); sigInp.pushKV("value", pubkey.trimmed().toStdString());
            sigInputs.push_back(sigInp); sigAct.pushKV("inputs", sigInputs);
            falseBranch.push_back(sigAct);
            hasFalse = true;
        } else if (falseType != tr("None")) {
            UniValue falseAct(UniValue::VOBJ);
            UniValue falseInputs(UniValue::VARR);
            if (falseType.contains("lock-time")) {
                int64_t lockTime = QInputDialog::getInt(this, tr("False Branch - Time-Locked"), tr("Lock Until (Block or Timestamp):"), defaultLock, 0, 2000000000, 1, &ok);
                if (!ok) return;
                falseAct.pushKV("role", "lock-time");
                UniValue inp(UniValue::VOBJ); inp.pushKV("name", "Lock-Until"); inp.pushKV("type", "timestamp-or-block-height"); inp.pushKV("value", lockTime); falseInputs.push_back(inp);
                hasFalse = true;
            } else if (falseType.contains("check-signature-verification")) {
                QString pubkey = QInputDialog::getText(this, tr("False Branch - Signature Check"), tr("Owner Public Key (Hex):"), QLineEdit::Normal, defaultPubkey, &ok);
                if (!ok || pubkey.isEmpty()) return;
                falseAct.pushKV("role", "check-signature-verification");
                UniValue inp(UniValue::VOBJ); inp.pushKV("name", "Pubkey"); inp.pushKV("type", "pubkey"); inp.pushKV("value", pubkey.trimmed().toStdString()); falseInputs.push_back(inp);
                hasFalse = true;
            } else if (falseType.contains("equalverify-checksig")) {
                QString pkh = QInputDialog::getText(this, tr("False Branch - PubKeyHash Check"), tr("PubKeyHash (40-char Hex):"), QLineEdit::Normal, defaultPubkeyHash, &ok);
                if (!ok || pkh.isEmpty()) return;
                falseAct.pushKV("role", "equalverify-checksig");
                UniValue inp(UniValue::VOBJ); inp.pushKV("name", "PubKeyHash"); inp.pushKV("type", "string"); inp.pushKV("value", pkh.trimmed().toStdString()); falseInputs.push_back(inp);
                hasFalse = true;
            } else if (falseType.contains("multi-signature")) {
                bool ok1, ok2, ok3;
                int m = QInputDialog::getInt(this, tr("False Branch - Multi-Signature"), tr("Required Signatures (m):"), 2, 1, 20, 1, &ok1);
                if (!ok1) return;
                int n = QInputDialog::getInt(this, tr("False Branch - Multi-Signature"), tr("Total Keys (n):"), 3, 1, 20, 1, &ok2);
                if (!ok2) return;
                QString keysStr = QInputDialog::getText(this, tr("False Branch - Multi-Signature"), tr("Public Keys (space-separated):"), QLineEdit::Normal, defaultPubkey, &ok3);
                if (!ok3 || keysStr.isEmpty()) return;
                falseAct.pushKV("role", "multi-signature");
                UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", m); falseInputs.push_back(inpM);
                UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", n); falseInputs.push_back(inpN);
                UniValue keysArray(UniValue::VARR);
                QStringList keysList = keysStr.split(' ', QString::SkipEmptyParts);
                for (const QString& key : keysList) keysArray.push_back(key.trimmed().toStdString());
                UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); falseInputs.push_back(inpSigs);
                hasFalse = true;
            } else if (falseType.contains("hash160")) {
                bool ok1;
                QString hashHex = QInputDialog::getText(this, tr("False Branch - Hash-Locked"), tr("Hash160 of Preimage (Hex):"), QLineEdit::Normal, "", &ok1);
                if (!ok1 || hashHex.isEmpty()) return;
                falseAct.pushKV("role", "hash160");
                UniValue inpHash(UniValue::VOBJ); inpHash.pushKV("name", "Hash160"); inpHash.pushKV("type", "string-or-number"); inpHash.pushKV("value", hashHex.trimmed().toStdString()); falseInputs.push_back(inpHash);
                hasFalse = true;
            }
            if (hasFalse) {
                falseAct.pushKV("inputs", falseInputs);
                falseBranch.push_back(falseAct);
            }
        }

        UniValue condNode(UniValue::VOBJ);
        condNode.pushKV("role", "if-condition");
        condNode.pushKV("expression", exprAct);
        condNode.pushKV("true", trueBranch);
        if (hasFalse) {
            condNode.pushKV("false", falseBranch);
        }

        currentActions.push_back(condNode);
        rebuildScene();
        return;
    }
    else if (row == 6) { // Miner Registration: Coin-Lock (coin-lock-miner)
        bool ok1, ok2, ok3;
        int64_t lockHeight = QInputDialog::getInt(this, tr("Coin-Lock Miner"), tr("Lock Until Block Height:"), defaultLock, 0, 2000000000, 1, &ok1);
        if (!ok1) return;
        QString ownerPubkey = QInputDialog::getText(this, tr("Coin-Lock Miner"), tr("Owner Public Key (Hex):"), QLineEdit::Normal, defaultPubkey, &ok2);
        if (!ok2 || ownerPubkey.isEmpty()) return;
        QString minerPubkey = QInputDialog::getText(this, tr("Coin-Lock Miner"), tr("Miner Public Key (Hex):"), QLineEdit::Normal, defaultPubkey, &ok3);
        if (!ok3 || minerPubkey.isEmpty()) return;

        node.pushKV("role", "coin-lock-miner");
        UniValue inp1(UniValue::VOBJ); inp1.pushKV("name", "LockHeight"); inp1.pushKV("type", "timestamp-or-block-height"); inp1.pushKV("value", lockHeight); inputs.push_back(inp1);
        UniValue inp2(UniValue::VOBJ); inp2.pushKV("name", "OwnerPubKey"); inp2.pushKV("type", "pubkey"); inp2.pushKV("value", ownerPubkey.trimmed().toStdString()); inputs.push_back(inp2);
        UniValue inp3(UniValue::VOBJ); inp3.pushKV("name", "MinerPubKey"); inp3.pushKV("type", "pubkey"); inp3.pushKV("value", minerPubkey.trimmed().toStdString()); inputs.push_back(inp3);
    }
    else if (row == 7) { // Miner Registration: PoW-Lock (pow-miner)
        bool ok1, ok2, ok3, ok4, ok5;
        int64_t lockHeight = QInputDialog::getInt(this, tr("PoW-Lock Miner"), tr("Lock Until Block Height:"), defaultLock, 0, 2000000000, 1, &ok1);
        if (!ok1) return;
        QString ownerPubkey = QInputDialog::getText(this, tr("PoW-Lock Miner"), tr("Owner Public Key (Hex):"), QLineEdit::Normal, defaultPubkey, &ok2);
        if (!ok2 || ownerPubkey.isEmpty()) return;
        QString minerPubkey = QInputDialog::getText(this, tr("PoW-Lock Miner"), tr("Miner Public Key (Hex):"), QLineEdit::Normal, defaultPubkey, &ok3);
        if (!ok3 || minerPubkey.isEmpty()) return;
        int nonce = QInputDialog::getInt(this, tr("PoW-Lock Miner"), tr("PoW Nonce:"), 0, 0, 2147483647, 1, &ok4);
        if (!ok4) return;
        int targetBits = QInputDialog::getInt(this, tr("PoW-Lock Miner"), tr("Target Difficulty Bits (1-64):"), 16, 1, 64, 1, &ok5);
        if (!ok5) return;

        node.pushKV("role", "pow-miner");
        UniValue inp1(UniValue::VOBJ); inp1.pushKV("name", "LockHeight"); inp1.pushKV("type", "timestamp-or-block-height"); inp1.pushKV("value", lockHeight); inputs.push_back(inp1);
        UniValue inp2(UniValue::VOBJ); inp2.pushKV("name", "OwnerPubKey"); inp2.pushKV("type", "pubkey"); inp2.pushKV("value", ownerPubkey.trimmed().toStdString()); inputs.push_back(inp2);
        UniValue inp3(UniValue::VOBJ); inp3.pushKV("name", "MinerPubKey"); inp3.pushKV("type", "pubkey"); inp3.pushKV("value", minerPubkey.trimmed().toStdString()); inputs.push_back(inp3);
        UniValue inp4(UniValue::VOBJ); inp4.pushKV("name", "PoWNonce"); inp4.pushKV("type", "number"); inp4.pushKV("value", nonce); inputs.push_back(inp4);
        UniValue inp5(UniValue::VOBJ); inp5.pushKV("name", "TargetDifficultyBits"); inp5.pushKV("type", "number"); inp5.pushKV("value", targetBits); inputs.push_back(inp5);
    }
    else if (row == 8) { // Push Number / True (number)
        bool ok;
        int val = QInputDialog::getInt(this, tr("Push Number / True"), tr("Integer Value (-1 to 16):"), 1, -1, 16, 1, &ok);
        if (!ok) return;

        node.pushKV("role", "number");
        UniValue inp(UniValue::VOBJ);
        inp.pushKV("name", "Value");
        inp.pushKV("type", "number");
        inp.pushKV("value", val);
        inputs.push_back(inp);
    }
    
    node.pushKV("inputs", inputs);
    currentActions.push_back(node);
    rebuildScene();
}

void VisualDesignerDialog::onEditNodeClicked()
{
    QList<QGraphicsItem*> selected = scene->selectedItems();
    if (selected.isEmpty()) {
        QMessageBox::warning(this, tr("Selection Required"), tr("Please click on a block item on the canvas to select it for editing."));
        return;
    }

    DesignerNodeItem* target = dynamic_cast<DesignerNodeItem*>(selected.first());
    if (!target) return;

    int index = nodeItems.indexOf(target);
    if (index < 0 || index >= (int)currentActions.size()) return;

    UniValue act = currentActions[index];
    if (!act.isObject() || !act.exists("role")) return;
    std::string role = act["role"].get_str();

    if (act.exists("inputs") && act["inputs"].isArray() && !act["inputs"].empty()) {
        UniValue newInputs(UniValue::VARR);
        const UniValue& oldInputs = act["inputs"];
        for (unsigned int j = 0; j < oldInputs.size(); ++j) {
            UniValue inp = oldInputs[j];
            if (!inp.isObject() || !inp.exists("name")) {
                newInputs.push_back(inp);
                continue;
            }
            QString name = QString::fromStdString(inp["name"].get_str());
            std::string type = (inp.exists("type") && inp["type"].isStr()) ? inp["type"].get_str() : "string";
            bool ok = false;

            UniValue updatedInp(UniValue::VOBJ);
            updatedInp.pushKV("name", inp["name"]);
            updatedInp.pushKV("type", type);

            if (inp.exists("value") && inp["value"].isArray()) {
                QStringList currentList;
                for (unsigned int k = 0; k < inp["value"].size(); ++k) {
                    currentList << QString::fromStdString(inp["value"][k].getValStr());
                }
                QString edited = QInputDialog::getText(this, tr("Edit %1").arg(QString::fromStdString(role)),
                    tr("%1 (space-separated):").arg(name), QLineEdit::Normal, currentList.join(" "), &ok);
                if (!ok) return;
                UniValue arr(UniValue::VARR);
                for (const QString& part : edited.split(' ', QString::SkipEmptyParts)) {
                    arr.push_back(part.trimmed().toStdString());
                }
                updatedInp.pushKV("value", arr);
            } else if (type == "number" || type == "timestamp-or-block-height" || (inp.exists("value") && inp["value"].isNum())) {
                int curVal = inp.exists("value") ? (inp["value"].isNum() ? inp["value"].get_int() : atoi(inp["value"].getValStr().c_str())) : 0;
                int newVal = QInputDialog::getInt(this, tr("Edit %1").arg(QString::fromStdString(role)),
                    tr("%1:").arg(name), curVal, -1, 2000000000, 1, &ok);
                if (!ok) return;
                updatedInp.pushKV("value", newVal);
            } else {
                QString curVal = inp.exists("value") ? QString::fromStdString(inp["value"].getValStr()) : "";
                QString newVal = QInputDialog::getText(this, tr("Edit %1").arg(QString::fromStdString(role)),
                    tr("%1:").arg(name), QLineEdit::Normal, curVal, &ok);
                if (!ok) return;
                updatedInp.pushKV("value", newVal.trimmed().toStdString());
            }
            newInputs.push_back(updatedInp);
        }

        UniValue updatedAct(UniValue::VOBJ);
        updatedAct.pushKV("role", role);
        updatedAct.pushKV("inputs", newInputs);

        UniValue newActions(UniValue::VARR);
        for (unsigned int i = 0; i < currentActions.size(); ++i) {
            newActions.push_back((int)i == index ? updatedAct : currentActions[i]);
        }
        currentActions = newActions;
        rebuildScene();
    } else {
        QMessageBox::information(this, tr("Edit Block"), tr("To modify an IF-Condition structure, please remove it and add a new IF-Condition block, or edit it directly in Developer Mode."));
    }
}

void VisualDesignerDialog::onRemoveNodeClicked()
{
    QList<QGraphicsItem*> selected = scene->selectedItems();
    if (selected.isEmpty()) {
        QMessageBox::warning(this, tr("Selection Required"), tr("Please click on a block item on the canvas to select it."));
        return;
    }
    
    DesignerNodeItem* target = dynamic_cast<DesignerNodeItem*>(selected.first());
    if (!target) return;
    
    int index = nodeItems.indexOf(target);
    if (index >= 0) {
        // Remove from currentActions array
        UniValue newActions(UniValue::VARR);
        for (unsigned int i = 0; i < currentActions.size(); i++) {
            if ((int)i != index) {
                newActions.push_back(currentActions[i]);
            }
        }
        currentActions = newActions;
        rebuildScene();
    }
}

void VisualDesignerDialog::onApplyClicked()
{
    // Reorder actions array based on their vertical Y coordinate on the canvas!
    // This allows users to visually reorder blocks by dragging them up or down!
    QList<DesignerNodeItem*> sortedNodes = nodeItems;
    std::sort(sortedNodes.begin(), sortedNodes.end(), [](DesignerNodeItem* a, DesignerNodeItem* b) {
        return a->y() < b->y();
    });
    
    UniValue reorderedActions(UniValue::VARR);
    for (DesignerNodeItem* item : sortedNodes) {
        reorderedActions.push_back(item->nodeData);
    }
    currentActions = reorderedActions;
    
    accept();
}
