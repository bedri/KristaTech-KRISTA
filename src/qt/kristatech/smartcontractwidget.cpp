// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/smartcontractwidget.h"
#include "qt/kristatech/forms/ui_smartcontractwidget.h"
#include "qt/kristatech/kristatechgui.h"
#include "qt/kristatech/defaultdialog.h"
#include "qt/kristatech/qtutils.h"
#include "bitcoinunits.h"
#include "chainparams.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "walletmodel.h"
#include "wallet/wallet.h"
#include "script/mescal.h"
#include "script/standard.h"
#include "net.h"
#include "main.h"       // cs_main, COIN
#include "timedata.h"
#include "coincontrol.h"
#include "utilstrencodings.h"
#include "primitives/transaction.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QListView>
#include <QLocale>
#include <QMenu>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextStream>
#include <QVBoxLayout>
#include <QFile>
#include <algorithm>

class YamlSyntaxHighlighter : public QObject
{
public:
    explicit YamlSyntaxHighlighter(QTextEdit* edit)
        : QObject(edit)
        , m_edit(edit)
    {
        setLightTheme(true);
        if (m_edit) {
            connect(m_edit, &QTextEdit::textChanged, this, [this]() { rehighlight(); });
        }
    }

    void setLightTheme(bool isLight)
    {
        auto makeFmt = [](const QString& hex, bool bold = false) {
            QTextCharFormat fmt;
            fmt.setForeground(QColor(hex));
            if (bold) fmt.setFontWeight(QFont::Bold);
            return fmt;
        };
        m_fmtTopKey  = makeFmt(isLight ? QStringLiteral("#C2410C") : QStringLiteral("#FCA5A5"), true);
        m_fmtKey     = makeFmt(isLight ? QStringLiteral("#7C3AED") : QStringLiteral("#D8B4FE"), true);
        m_fmtStr     = makeFmt(isLight ? QStringLiteral("#0F766E") : QStringLiteral("#6EE7B7"));
        m_fmtNum     = makeFmt(isLight ? QStringLiteral("#BE185D") : QStringLiteral("#FCD34D"));
        m_fmtBool    = makeFmt(isLight ? QStringLiteral("#2563EB") : QStringLiteral("#7DD3FC"), true);
        m_fmtDash    = makeFmt(isLight ? QStringLiteral("#D97706") : QStringLiteral("#FDE047"), true);
        m_fmtPunct   = makeFmt(isLight ? QStringLiteral("#475569") : QStringLiteral("#CBD5E1"));
        m_fmtComment = makeFmt(isLight ? QStringLiteral("#64748B") : QStringLiteral("#94A3B8"));
        rehighlight();
    }

    void rehighlight()
    {
        if (!m_edit || !m_edit->document()) return;
        QList<QTextEdit::ExtraSelection> selections;
        for (QTextBlock blk = m_edit->document()->begin(); blk.isValid(); blk = blk.next()) {
            highlightBlock(blk, selections);
        }
        m_edit->setExtraSelections(selections);
    }

private:
    void highlightBlock(const QTextBlock& blk, QList<QTextEdit::ExtraSelection>& out) const
    {
        const QString text = blk.text();
        const int blkPos = blk.position();
        const int len = text.size();

        auto addSpan = [&](int start, int count, const QTextCharFormat& fmt) {
            if (count <= 0 || !m_edit || !m_edit->document()) return;
            QTextEdit::ExtraSelection sel;
            QTextCursor cur(m_edit->document());
            cur.setPosition(blkPos + start);
            cur.setPosition(blkPos + start + count, QTextCursor::KeepAnchor);
            sel.cursor = cur;
            sel.format = fmt;
            out.append(sel);
        };

        int pos = 0;
        while (pos < len && (text.at(pos) == QLatin1Char(' ') || text.at(pos) == QLatin1Char('\t'))) {
            ++pos;
        }
        if (pos >= len) return;

        if (text.at(pos) == QLatin1Char('#')) {
            addSpan(pos, len - pos, m_fmtComment);
            return;
        }

        const int indent = pos;
        if (text.at(pos) == QLatin1Char('-') && (pos + 1 == len || text.at(pos + 1) == QLatin1Char(' '))) {
            addSpan(pos, 1, m_fmtDash);
            ++pos;
            while (pos < len && text.at(pos) == QLatin1Char(' ')) {
                ++pos;
            }
            if (pos >= len) return;
        }

        // Split off unquoted trailing # comment so scalar/key matching stays clean
        int commentStart = len;
        bool inS = false;
        bool inD = false;
        for (int k = pos; k < len; ++k) {
            const QChar c = text.at(k);
            if (c == QLatin1Char('\'') && !inD) inS = !inS;
            else if (c == QLatin1Char('"') && !inS) inD = !inD;
            else if (c == QLatin1Char('#') && !inS && !inD && (k == pos || text.at(k - 1).isSpace())) {
                commentStart = k;
                break;
            }
        }
        if (commentStart < len) {
            addSpan(commentStart, len - commentStart, m_fmtComment);
        }

        int colonIdx = -1;
        inS = false;
        inD = false;
        for (int k = pos; k < commentStart; ++k) {
            const QChar c = text.at(k);
            if (c == QLatin1Char('\'') && !inD) inS = !inS;
            else if (c == QLatin1Char('"') && !inS) inD = !inD;
            else if (c == QLatin1Char(':') && !inS && !inD) {
                if (k + 1 == commentStart || text.at(k + 1) == QLatin1Char(' ')) {
                    colonIdx = k;
                    break;
                }
            }
        }

        auto styleScalarSlice = [&](int sStart, int sEnd) {
            while (sStart < sEnd && text.at(sStart).isSpace()) ++sStart;
            while (sEnd > sStart && text.at(sEnd - 1).isSpace()) --sEnd;
            if (sStart >= sEnd) return;
            const QString val = text.mid(sStart, sEnd - sStart);
            if (val == QLatin1String("true") || val == QLatin1String("false") ||
                val == QLatin1String("null") || val == QLatin1String("~")) {
                addSpan(sStart, sEnd - sStart, m_fmtBool);
            } else if (val == QLatin1String("{}") || val == QLatin1String("[]")) {
                addSpan(sStart, sEnd - sStart, m_fmtPunct);
            } else if ((val.startsWith(QLatin1Char('"')) && val.endsWith(QLatin1Char('"')) && val.size() >= 2) ||
                       (val.startsWith(QLatin1Char('\'')) && val.endsWith(QLatin1Char('\'')) && val.size() >= 2)) {
                addSpan(sStart, sEnd - sStart, m_fmtStr);
            } else {
                bool isNum = false;
                val.toDouble(&isNum);
                addSpan(sStart, sEnd - sStart, isNum ? m_fmtNum : m_fmtStr);
            }
        };

        if (colonIdx > pos) {
            addSpan(pos, colonIdx - pos, (indent == 0) ? m_fmtTopKey : m_fmtKey);
            addSpan(colonIdx, 1, m_fmtPunct);
            styleScalarSlice(colonIdx + 1, commentStart);
        } else {
            styleScalarSlice(pos, commentStart);
        }
    }

    QTextEdit* m_edit{nullptr};
    QTextCharFormat m_fmtTopKey;
    QTextCharFormat m_fmtKey;
    QTextCharFormat m_fmtStr;
    QTextCharFormat m_fmtNum;
    QTextCharFormat m_fmtBool;
    QTextCharFormat m_fmtDash;
    QTextCharFormat m_fmtPunct;
    QTextCharFormat m_fmtComment;
};

// ─── Helpers ─────────────────────────────────────────────────────────────────

struct ParsedContractMeta {
    bool isTimeLock{false};
    bool isSigCheck{false};
    bool isPkhCheck{false};
    bool isMultiSig{false};
    bool isHashLock{false};
    bool isMinerReg{false};
    bool isPowMiner{false};
    bool isIfCond{false};

    std::string timeLockOwnerPubkey;
    std::string pkhHex;
    std::vector<std::string> multiSigPubkeys;
    int multiSigM{0};
    int multiSigN{0};
    std::string hashLockPubkey;
    int64_t lockTimeVal{0};
    bool ifExprIsHashLock{false};
    std::string ifTruePubkey;
    std::string ifFalsePubkey;
    int64_t ifFalseLockTime{0};
};

static ParsedContractMeta ParseContractMetadata(const UniValue& decompiled)
{
    ParsedContractMeta meta;
    if (!decompiled.exists("actions"))
        return meta;
    const UniValue& actions = decompiled["actions"];
    if (!actions.isArray())
        return meta;

    for (unsigned int i = 0; i < actions.size(); i++) {
        const UniValue& act = actions[i];
        if (!act.isObject()) continue;
        std::string role = act.exists("role") ? act["role"].get_str() : "";
        UniValue inputs = act.exists("inputs") ? act["inputs"] : UniValue();
        if (role == "lock-time") {
            meta.isTimeLock = true;
            if (inputs.isArray() && !inputs.empty() && inputs[0].exists("value")) {
                meta.lockTimeVal = inputs[0]["value"].isNum()
                    ? inputs[0]["value"].get_int64()
                    : atoll(inputs[0]["value"].getValStr().c_str());
            }
        } else if (role == "check-signature-verification") {
            meta.isSigCheck = true;
            if (inputs.isArray() && !inputs.empty() && inputs[0].exists("value")) {
                std::string pk = inputs[0]["value"].getValStr();
                if (meta.isHashLock) meta.hashLockPubkey = pk;
                else meta.timeLockOwnerPubkey = pk;
            }
        } else if (role == "equalverify-checksig") {
            meta.isPkhCheck = true;
            if (inputs.isArray() && !inputs.empty() && inputs[0].exists("value")) {
                meta.pkhHex = inputs[0]["value"].getValStr();
            }
        } else if (role == "multi-signature") {
            meta.isMultiSig = true;
            if (inputs.isArray()) {
                for (unsigned int j = 0; j < inputs.size(); j++) {
                    std::string inputName = inputs[j]["name"].get_str();
                    if (inputName == "m") {
                        meta.multiSigM = inputs[j]["value"].get_int();
                    } else if (inputName == "n") {
                        meta.multiSigN = inputs[j]["value"].get_int();
                    } else if (inputName == "Signatures") {
                        const UniValue& sigsVal = inputs[j]["value"];
                        if (sigsVal.isArray()) {
                            for (unsigned int k = 0; k < sigsVal.size(); k++) {
                                meta.multiSigPubkeys.push_back(sigsVal[k].get_str());
                            }
                        }
                    }
                }
            }
        } else if (role == "hash160") {
            meta.isHashLock = true;
        } else if (role == "coin-lock-miner" || role == "pow-miner") {
            meta.isMinerReg = true;
            if (role == "pow-miner") meta.isPowMiner = true;
            if (inputs.isArray()) {
                for (unsigned int j = 0; j < inputs.size(); j++) {
                    if (!inputs[j].isObject() || !inputs[j].exists("name") || !inputs[j].exists("value")) continue;
                    std::string n = inputs[j]["name"].get_str();
                    if (n == "Pubkey" || n == "OwnerPubKey") meta.timeLockOwnerPubkey = inputs[j]["value"].getValStr();
                    else if (n == "Lock-Until" || n == "LockHeight") {
                        meta.lockTimeVal = inputs[j]["value"].isNum()
                            ? inputs[j]["value"].get_int64()
                            : atoll(inputs[j]["value"].getValStr().c_str());
                    }
                }
            }
        } else if (role == "if-condition") {
            meta.isIfCond = true;
            if (act.exists("expressions") && act["expressions"].isArray()) {
                for (unsigned int k = 0; k < act["expressions"].size(); ++k) {
                    const UniValue& ex = act["expressions"][k];
                    if (ex.isObject() && ex.exists("role") && ex["role"].get_str() == "hash160") {
                        meta.ifExprIsHashLock = true;
                    }
                }
            }
            const UniValue& trueArr = act.exists("true") ? act["true"] : (act.exists("true_action") ? act["true_action"] : UniValue());
            if (trueArr.isArray()) {
                for (unsigned int k = 0; k < trueArr.size(); ++k) {
                    const UniValue& ta = trueArr[k];
                    if (ta.isObject() && ta.exists("role") && ta["role"].get_str() == "check-signature-verification" &&
                        ta.exists("inputs") && ta["inputs"].isArray() && !ta["inputs"].empty()) {
                        meta.ifTruePubkey = ta["inputs"][0]["value"].getValStr();
                    }
                }
            }
            const UniValue& falseArr = act.exists("false") ? act["false"] : (act.exists("false_action") ? act["false_action"] : UniValue());
            if (falseArr.isArray()) {
                for (unsigned int k = 0; k < falseArr.size(); ++k) {
                    const UniValue& fa = falseArr[k];
                    if (!fa.isObject() || !fa.exists("role")) continue;
                    if (fa["role"].get_str() == "lock-time" && fa.exists("inputs") && fa["inputs"].isArray() && !fa["inputs"].empty()) {
                        meta.ifFalseLockTime = fa["inputs"][0]["value"].isNum()
                            ? fa["inputs"][0]["value"].get_int64()
                            : atoll(fa["inputs"][0]["value"].getValStr().c_str());
                    } else if (fa["role"].get_str() == "check-signature-verification" && fa.exists("inputs") && fa["inputs"].isArray() && !fa["inputs"].empty()) {
                        meta.ifFalsePubkey = fa["inputs"][0]["value"].getValStr();
                    }
                }
            }
        }
    }
    return meta;
}

static QString ClassifyContractTag(const ParsedContractMeta& meta, CAmount amount)
{
    if (meta.isMinerReg) {
        if (amount == 0) return QStringLiteral("[Zero-Coin PoW]");
        if (meta.isPowMiner) return QStringLiteral("[PoW-Lock]");
        return QStringLiteral("[Coin-Lock]");
    }
    if (meta.isIfCond) return QStringLiteral("[IF-Cond]");
    if (meta.isMultiSig) return QStringLiteral("[MultiSig]");
    if (meta.isHashLock) return QStringLiteral("[HashLock]");
    if (meta.isTimeLock) return QStringLiteral("[TimeLock]");
    return QStringLiteral("[MESCAL]");
}

static void CollectLockTimeInputs(const UniValue& node, std::vector<UniValue>& outLockInputs, int depth = 0)
{
    if (depth > 16) return;
    if (node.isArray()) {
        for (unsigned int i = 0; i < node.size(); ++i) {
            CollectLockTimeInputs(node[i], outLockInputs, depth + 1);
        }
        return;
    }
    if (!node.isObject()) return;

    if (node.exists("role") && node["role"].isStr() && node.exists("inputs") && node["inputs"].isArray()) {
        const std::string role = node["role"].get_str();
        const UniValue& inputs = node["inputs"];
        if (role == "lock-time" && !inputs.empty() && inputs[0].isObject() && inputs[0].exists("value")) {
            outLockInputs.push_back(inputs[0]["value"]);
        } else if (role == "coin-lock-miner" || role == "pow-miner") {
            for (unsigned int i = 0; i < inputs.size(); ++i) {
                if (!inputs[i].isObject() || !inputs[i].exists("value")) continue;
                const std::string inpName = (inputs[i].exists("name") && inputs[i]["name"].isStr()) ? inputs[i]["name"].get_str() : "";
                if (inpName == "Lock-Until" || inpName == "LockHeight" ||
                    (inpName.empty() && ((role == "coin-lock-miner" && i == 1) || (role == "pow-miner" && i == 3)))) {
                    outLockInputs.push_back(inputs[i]["value"]);
                }
            }
        }
    }

    const std::vector<std::string>& keys = node.getKeys();
    for (const std::string& k : keys) {
        CollectLockTimeInputs(node[k], outLockInputs, depth + 1);
    }
}

static QString FormatLockDurationApprox(qint64 absSeconds)
{
    if (absSeconds < 60) return QObject::tr("%1s").arg(absSeconds);
    if (absSeconds < 3600) return QObject::tr("%1m").arg(absSeconds / 60);
    if (absSeconds < 86400) return QObject::tr("%1h %2m").arg(absSeconds / 3600).arg((absSeconds % 3600) / 60);
    const double days = static_cast<double>(absSeconds) / 86400.0;
    if (days >= 365.0) {
        return QObject::tr("~%1 years (%2 days)").arg(days / 365.0, 0, 'f', 1).arg(QLocale().toString(static_cast<qlonglong>(days)));
    }
    return QObject::tr("~%1 days").arg(days, 0, 'f', 1);
}

static std::string UniValueToYAML(const UniValue& val, int indent = 0) {
    std::string result;
    std::string spaces(indent, ' ');
    if (val.isObject()) {
        const std::vector<std::string>& keys = val.getKeys();
        for (const std::string& key : keys) {
            const UniValue& subVal = val[key];
            if (subVal.isObject()) {
                result += spaces + key + ":\n" + UniValueToYAML(subVal, indent + 2);
            } else if (subVal.isArray()) {
                result += spaces + key + ":\n" + UniValueToYAML(subVal, indent + 2);
            } else {
                result += spaces + key + ": " + UniValueToYAML(subVal, 0);
            }
        }
    } else if (val.isArray()) {
        for (unsigned int i = 0; i < val.size(); ++i) {
            const UniValue& subVal = val[i];
            if (subVal.isObject() || subVal.isArray()) {
                result += spaces + "- \n" + UniValueToYAML(subVal, indent + 2);
            } else {
                result += spaces + "- " + UniValueToYAML(subVal, 0);
            }
        }
    } else if (val.isNum()) {
        result += val.getValStr() + "\n";
    } else if (val.isBool()) {
        result += std::string(val.get_bool() ? "true" : "false") + "\n";
    } else {
        result += "\"" + val.get_str() + "\"\n";
    }
    return result;
}

// ─── Constructor / Destructor ─────────────────────────────────────────────────

SmartContractWidget::SmartContractWidget(KRISTATECHGUI* parent) :
    PWidget(parent),
    ui(new Ui::SmartContractWidget)
{
    ui->setupUi(this);
    setObjectName(QStringLiteral("SmartContractWidget"));

    // Build unified KristaTech page container over tabWidgetMode
    scRoot = new QWidget(this);
    scRoot->setObjectName(QStringLiteral("scPageRoot"));
    scRoot->setAttribute(Qt::WA_StyledBackground, true);

    QVBoxLayout* scRootLayout = new QVBoxLayout(scRoot);
    scRootLayout->setContentsMargins(24, 16, 24, 20);
    scRootLayout->setSpacing(14);

    if (ui->verticalLayout_Root) {
        ui->verticalLayout_Root->removeWidget(ui->tabWidgetMode);
        ui->verticalLayout_Root->addWidget(scRoot, 1);
    }

    // Unified Page Header Row
    QHBoxLayout* pageHeader = new QHBoxLayout();
    pageHeader->setContentsMargins(2, 0, 2, 0);
    pageHeader->setSpacing(12);

    QVBoxLayout* titleCol = new QVBoxLayout();
    titleCol->setContentsMargins(0, 0, 0, 0);
    titleCol->setSpacing(2);
    QLabel* lblPageTitle = new QLabel(tr("Smart Contracts"), scRoot);
    lblPageTitle->setProperty("scPageTitle", true);
    QLabel* lblPageSub = new QLabel(tr("Build, compile, deploy, and execute MESCAL script smart contracts."), scRoot);
    lblPageSub->setProperty("scPageSubtitle", true);
    lblPageSub->setWordWrap(true);
    titleCol->addWidget(lblPageTitle);
    titleCol->addWidget(lblPageSub);

    QHBoxLayout* modeBtnRow = new QHBoxLayout();
    modeBtnRow->setContentsMargins(0, 0, 0, 0);
    modeBtnRow->setSpacing(8);

    btnModeBuild = new QPushButton(tr("Build && Publish"), scRoot);
    btnModeRun = new QPushButton(tr("Run Contract"), scRoot);
    for (QPushButton* modeBtn : {btnModeBuild, btnModeRun}) {
        modeBtn->setCheckable(true);
        modeBtn->setAutoExclusive(true);
        modeBtn->setFixedHeight(34);
        modeBtn->setMinimumWidth(128);
        modeBtn->setCursor(Qt::PointingHandCursor);
        modeBtn->setFocusPolicy(Qt::TabFocus);
        modeBtn->setProperty("scSegBtn", true);
        modeBtnRow->addWidget(modeBtn);
    }
    btnModeBuild->setChecked(true);

    pageHeader->addLayout(titleCol, 1);
    pageHeader->addLayout(modeBtnRow, 0);
    scRootLayout->addLayout(pageHeader);

    // Embed tabWidgetMode cleanly with hidden native QTabBar
    ui->tabWidgetMode->setParent(scRoot);
    if (ui->tabWidgetMode->tabBar()) {
        ui->tabWidgetMode->tabBar()->hide();
    }
    ui->tabWidgetMode->setDocumentMode(true);
    scRootLayout->addWidget(ui->tabWidgetMode, 1);

    auto makeDivider = [](QWidget* owner) -> QFrame* {
        QFrame* div = new QFrame(owner);
        div->setProperty("scDivider", true);
        div->setAttribute(Qt::WA_StyledBackground, true);
        div->setFixedHeight(1);
        return div;
    };

    auto configureCombo = [](QComboBox* cb) {
        clearLegacySettingsConstraints(cb);
        cb->setView(new QListView());
        cb->setMaxVisibleItems(12);
        cb->setFixedHeight(36);
        cb->setCursor(Qt::PointingHandCursor);
        cb->setProperty("scInput", true);
    };

    // ── Build tab ─────────────────────────────────────────────────────────────
    if (ui->horizontalLayout_Build) {
        ui->horizontalLayout_Build->setContentsMargins(0, 0, 0, 0);
        ui->horizontalLayout_Build->setSpacing(16);
        ui->horizontalLayout_Build->setStretch(0, 1);
        ui->horizontalLayout_Build->setStretch(1, 1);
    }

    clearLegacySettingsConstraints(ui->left);
    ui->left->setProperty("scCard", true);
    ui->left->setAttribute(Qt::WA_StyledBackground, true);
    if (ui->verticalLayout_Left) {
        ui->verticalLayout_Left->setContentsMargins(18, 16, 18, 16);
        ui->verticalLayout_Left->setSpacing(10);
    }

    clearLegacySettingsConstraints(ui->right);
    ui->right->setProperty("scCard", true);
    ui->right->setAttribute(Qt::WA_StyledBackground, true);
    if (ui->verticalLayout_Right) {
        ui->verticalLayout_Right->setContentsMargins(18, 16, 18, 16);
        ui->verticalLayout_Right->setSpacing(10);
    }

    clearLegacySettingsConstraints(ui->labelTitle);
    ui->labelTitle->setText(tr("Contract Configuration"));
    ui->labelTitle->setProperty("scCardTitle", true);
    ui->labelSubtitle1->setVisible(false);

    clearLegacySettingsConstraints(ui->frameTemplateBar);
    ui->frameTemplateBar->setFrameShape(QFrame::NoFrame);
    if (ui->horizontalLayout_Templates) {
        ui->horizontalLayout_Templates->setContentsMargins(0, 2, 0, 2);
        ui->horizontalLayout_Templates->setSpacing(8);
    }
    clearLegacySettingsConstraints(ui->lblSelectTemplate);
    ui->lblSelectTemplate->setText(tr("Template:"));
    ui->lblSelectTemplate->setMinimumWidth(84);
    ui->lblSelectTemplate->setProperty("scFieldLabel", true);

    ui->comboTemplates->addItem(tr("Custom / Blank"));
    ui->comboTemplates->addItem(tr("Time-Locked Deposit"));
    ui->comboTemplates->addItem(tr("Escrow Multi-Signature (2-of-3)"));
    ui->comboTemplates->addItem(tr("Hash-Locked Claim"));
    ui->comboTemplates->addItem(tr("Dead Man's Switch (Inheritance)"));
    ui->comboTemplates->addItem(tr("Dual-Signature Escrow with Mediator"));
    ui->comboTemplates->addItem(tr("2-Factor Authentication (2FA) Wallet"));
    ui->comboTemplates->addItem(tr("Hash Time-Locked Swap (HTLC)"));
    ui->comboTemplates->addItem(tr("Multi-Path Security Recovery"));
    ui->comboTemplates->addItem(tr("Tokenized Asset (Escrow & Compliance)"));
    ui->comboTemplates->addItem(tr("Miner Registration (Coin-Lock)"));
    ui->comboTemplates->addItem(tr("Miner Registration (PoW-Lock)"));
    ui->comboTemplates->addItem(tr("Time-Delayed Vault (Anti-Theft)"));
    ui->comboTemplates->addItem(tr("Social Recovery Wallet"));
    ui->comboTemplates->addItem(tr("Milestone Vesting Escrow"));
    ui->comboTemplates->addItem(tr("Collateralized Loan Liquidation"));
    ui->comboTemplates->addItem(tr("Double-Deposit P2P Escrow"));
    ui->comboTemplates->addItem(tr("Pay-on-Delivery Supply Chain"));
    ui->comboTemplates->addItem(tr("Subscription Pull Authorization"));
    ui->comboTemplates->addItem(tr("Real Estate Title Transfer Escrow"));
    ui->comboTemplates->addItem(tr("Rental Deposit Trust"));
    ui->comboTemplates->addItem(tr("Corporate Governance (3-of-5)"));
    ui->comboTemplates->addItem(tr("Weighted Board Voting"));
    ui->comboTemplates->addItem(tr("DAO Ragequit Covenant"));
    ui->comboTemplates->addItem(tr("Dual-Custodian Audit Lock"));
    ui->comboTemplates->addItem(tr("P2P Prediction Market Escrow"));
    ui->comboTemplates->addItem(tr("Gaming Tournament Prize Pool"));
    ui->comboTemplates->addItem(tr("IoT EV Charging Pay-per-Use"));
    ui->comboTemplates->addItem(tr("Parametric Insurance Claim"));

    configureCombo(ui->comboTemplates);
    connect(ui->comboTemplates, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SmartContractWidget::onTemplateSelected);

    // 1-Click helper menu to inspect, copy, or generate real compressed 33-byte public keys from the active wallet
    btnCopyMyPubKey = new QPushButton(tr("Wallet PubKey ▾"), ui->frameTemplateBar);
    btnCopyMyPubKey->setCursor(Qt::PointingHandCursor);
    btnCopyMyPubKey->setFocusPolicy(Qt::TabFocus);
    btnCopyMyPubKey->setFixedHeight(36);
    btnCopyMyPubKey->setProperty("scSecondary", true);
    btnCopyMyPubKey->setToolTip(tr("Copy the active Owner Public Key, pick a key from a wallet address, or generate a new wallet key"));
    ui->horizontalLayout_Templates->addWidget(btnCopyMyPubKey);
    connect(btnCopyMyPubKey, &QPushButton::clicked, this, [this]() {
        if (!pwalletMain) {
            inform(tr("Wallet is not available."));
            return;
        }
        const bool light = isLightTheme();
        QMenu menu(this);
        menu.setStyleSheet(QString(
            "QMenu { background-color: %1; color: %2; border: 1px solid %3; border-radius: 8px; padding: 6px; font-size: 12px; }"
            "QMenu::item { padding: 7px 14px; border-radius: 5px; }"
            "QMenu::item:selected { background-color: #5576B2; color: #FFFFFF; }"
            "QMenu::separator { height: 1px; background: %3; margin: 4px 8px; }")
            .arg(light ? "#FFFFFF" : "#111A2E",
                 light ? "#0F172A" : "#E2E8F0",
                 light ? "#CBD5E1" : "#263859"));

        // Determine active Owner/Recipient pubkey or wallet default key
        QLineEdit* activeField = nullptr;
        const int curTab = ui->tabWidget ? ui->tabWidget->currentIndex() : 0;
        if (curTab == 0) activeField = ui->lineEditPubkeyTimeLock;
        else if (curTab == 2) activeField = ui->lineEditPubkeyHashLock;

        CPubKey activeFieldKey;
        if (activeField) {
            const std::vector<unsigned char> raw = ParseHex(activeField->text().trimmed().toStdString());
            if (raw.size() == CPubKey::COMPRESSED_PUBLIC_KEY_SIZE) {
                CPubKey parsed(raw.begin(), raw.end());
                if (parsed.IsFullyValid()) activeFieldKey = parsed;
            }
        }
        const CPubKey primaryKey = activeFieldKey.IsValid() ? activeFieldKey : m_walletPubKey;
        QAction* actCopyCurrent = nullptr;
        if (primaryKey.IsValid()) {
            const QString hexKey = QString::fromStdString(HexStr(primaryKey.begin(), primaryKey.end()));
            const QString addrStr = QString::fromStdString(EncodeDestination(primaryKey.GetID()));
            const QString shortHex = hexKey.left(10) + "..." + hexKey.right(6);
            actCopyCurrent = menu.addAction(tr("Copy Owner Public Key (%1 • %2)").arg(shortHex, addrStr));
            connect(actCopyCurrent, &QAction::triggered, this, [this, hexKey, addrStr]() {
                GUIUtil::setClipboard(hexKey);
                inform(tr("Owner Public Key (%1) copied:\n%2").arg(addrStr, hexKey));
            });
        }

        // Submenu listing existing wallet-owned P2PKH receiving keys (up to 20)
        QMenu* addrSubMenu = menu.addMenu(tr("Copy PubKey from Wallet Address"));
        addrSubMenu->setStyleSheet(menu.styleSheet());
        {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            int added = 0;
            for (const auto& kv : pwalletMain->mapAddressBook) {
                if (added >= 20) break;
                if (kv.second.purpose != AddressBook::AddressBookPurpose::RECEIVE) continue;
                const CKeyID* kid = boost::get<CKeyID>(&kv.first);
                if (!kid) continue;
                CPubKey pk;
                if (!pwalletMain->GetPubKey(*kid, pk) || !pk.IsValid() || !pk.IsCompressed()) continue;
                const QString hexKey = QString::fromStdString(HexStr(pk.begin(), pk.end()));
                const QString addrStr = QString::fromStdString(EncodeDestination(kv.first));
                const QString lbl = QString::fromStdString(kv.second.name).trimmed();
                const QString title = lbl.isEmpty() ? addrStr : QString("%1 • %2").arg(lbl, addrStr);
                QAction* a = addrSubMenu->addAction(title);
                connect(a, &QAction::triggered, this, [this, hexKey, addrStr, lbl]() {
                    GUIUtil::setClipboard(hexKey);
                    const QString nameTag = lbl.isEmpty() ? addrStr : QString("%1 (%2)").arg(lbl, addrStr);
                    inform(tr("Public key for %1 copied:\n%2").arg(nameTag, hexKey));
                });
                ++added;
            }
            if (added == 0) {
                QAction* emptyAct = addrSubMenu->addAction(tr("(No P2PKH receiving keys found)"));
                emptyAct->setEnabled(false);
            }
        }

        menu.addSeparator();
        QAction* actGenCopy = menu.addAction(tr("Generate && Copy New Wallet PubKey"));
        connect(actGenCopy, &QAction::triggered, this, [this]() {
            CPubKey newKey;
            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                if (pwalletMain->GetKeyFromPool(newKey) && newKey.IsValid()) {
                    pwalletMain->SetAddressBook(newKey.GetID(), "MESCAL Contract Key", AddressBook::AddressBookPurpose::RECEIVE);
                }
            }
            if (!newKey.IsValid()) {
                inform(tr("Unlock your wallet first to derive a new public key from the keypool."));
                return;
            }
            const QString hexKey = QString::fromStdString(HexStr(newKey.begin(), newKey.end()));
            const QString addrStr = QString::fromStdString(EncodeDestination(newKey.GetID()));
            GUIUtil::setClipboard(hexKey);
            inform(tr("New wallet PubKey (%1) registered & copied:\n%2").arg(addrStr, hexKey));
        });

        if (activeField) {
            QAction* actGenApply = menu.addAction(tr("Generate && Set as Owner Public Key"));
            connect(actGenApply, &QAction::triggered, this, [this, activeField]() {
                CPubKey newKey;
                {
                    LOCK2(cs_main, pwalletMain->cs_wallet);
                    if (pwalletMain->GetKeyFromPool(newKey) && newKey.IsValid()) {
                        pwalletMain->SetAddressBook(newKey.GetID(), "MESCAL Contract Key", AddressBook::AddressBookPurpose::RECEIVE);
                    }
                }
                if (!newKey.IsValid()) {
                    inform(tr("Unlock your wallet first to derive a new public key from the keypool."));
                    return;
                }
                m_walletPubKey = newKey;
                const QString hexKey = QString::fromStdString(HexStr(newKey.begin(), newKey.end()));
                const QString addrStr = QString::fromStdString(EncodeDestination(newKey.GetID()));
                activeField->setText(hexKey);
                GUIUtil::setClipboard(hexKey);
                updateOwnerPubKeyHints();
                generateContract();
                inform(tr("New Owner Public Key (%1) set & copied:\n%2").arg(addrStr, hexKey));
            });
        }

        menu.exec(btnCopyMyPubKey->mapToGlobal(QPoint(0, btnCopyMyPubKey->height() + 4)));
    });

    ui->tabWidget->setDocumentMode(false);
    ui->tabWidget->setMinimumHeight(255);
    for (QFormLayout* fl : {ui->formLayout_TimeLock, ui->formLayout_MultiSig, ui->formLayout_HashLock}) {
        if (fl) {
            fl->setContentsMargins(14, 14, 14, 14);
            fl->setVerticalSpacing(10);
            fl->setHorizontalSpacing(12);
        }
    }
    if (ui->verticalLayout_Custom) {
        ui->verticalLayout_Custom->setContentsMargins(14, 14, 14, 14);
        ui->verticalLayout_Custom->setSpacing(10);
    }

    for (QLabel* lbl : {ui->lblTLName, ui->lblTLExpiry, ui->lblTLPubkey,
                        ui->lblMSName, ui->lblMSN, ui->lblMSM, ui->lblMSKeys,
                        ui->lblHLName, ui->lblHLHash, ui->lblHLPubkey,
                        ui->labelAmount}) {
        clearLegacySettingsConstraints(lbl);
        lbl->setProperty("scFieldLabel", true);
    }

    for (QLineEdit* le : {ui->lineEditNameTimeLock, ui->lineEditExpiryTimeLock, ui->lineEditPubkeyTimeLock,
                          ui->lineEditNameMultiSig, ui->lineEditNMultiSig, ui->lineEditMMultiSig,
                          ui->lineEditNameHashLock, ui->lineEditHashHashLock, ui->lineEditPubkeyHashLock,
                          ui->lineEditAmount}) {
        clearLegacySettingsConstraints(le);
        le->setFixedHeight(36);
        le->setAttribute(Qt::WA_MacShowFocusRect, false);
        le->setProperty("scInput", true);
    }
    for (QLineEdit* monoLe : {ui->lineEditPubkeyTimeLock, ui->lineEditHashHashLock, ui->lineEditPubkeyHashLock}) {
        monoLe->setProperty("scMono", true);
    }
    ui->lineEditHashHashLock->setPlaceholderText(tr("40-char hex HASH160 of secret preimage"));

    // Replace raw numeric-only Lock Until field with explicit Timestamp (UTC) / Block Height selector + live preview
    if (ui->formLayout_TimeLock) {
        ui->formLayout_TimeLock->removeWidget(ui->lineEditExpiryTimeLock);
        QWidget* lockUntilBox = new QWidget(ui->tabTimeLock);
        QVBoxLayout* lockUntilV = new QVBoxLayout(lockUntilBox);
        lockUntilV->setContentsMargins(0, 0, 0, 0);
        lockUntilV->setSpacing(4);

        QHBoxLayout* lockUntilRow = new QHBoxLayout();
        lockUntilRow->setContentsMargins(0, 0, 0, 0);
        lockUntilRow->setSpacing(8);

        comboLockMode = new QComboBox(lockUntilBox);
        comboLockMode->addItem(tr("Timestamp (UTC)"), 1);
        comboLockMode->addItem(tr("Block Height"), 0);
        configureCombo(comboLockMode);
        comboLockMode->setFixedWidth(152);
        comboLockMode->setToolTip(tr("Choose whether funds unlock at a specific UTC date/time or at a block height (< 500,000,000)"));

        dateTimeEditLockTime = new QDateTimeEdit(lockUntilBox);
        dateTimeEditLockTime->setTimeSpec(Qt::UTC);
        dateTimeEditLockTime->setCalendarPopup(true);
        dateTimeEditLockTime->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm 'UTC'"));
        dateTimeEditLockTime->setMinimumDateTime(QDateTime::fromSecsSinceEpoch(LOCKTIME_THRESHOLD, Qt::UTC));
        dateTimeEditLockTime->setMaximumDateTime(QDateTime::fromSecsSinceEpoch(0xFFFFFFFFLL, Qt::UTC));
        dateTimeEditLockTime->setFixedHeight(36);
        dateTimeEditLockTime->setProperty("scInput", true);
        dateTimeEditLockTime->setDateTime(QDateTime::currentDateTimeUtc().addDays(30));

        ui->lineEditExpiryTimeLock->setParent(lockUntilBox);
        ui->lineEditExpiryTimeLock->setPlaceholderText(tr("Block height (e.g. 10000, < 500,000,000)"));

        lockUntilRow->addWidget(comboLockMode, 0);
        lockUntilRow->addWidget(dateTimeEditLockTime, 1);
        lockUntilRow->addWidget(ui->lineEditExpiryTimeLock, 1);

        lblLockUntilPreview = new QLabel(lockUntilBox);
        lblLockUntilPreview->setWordWrap(true);

        lockUntilV->addLayout(lockUntilRow);
        lockUntilV->addWidget(lblLockUntilPreview);
        ui->formLayout_TimeLock->setWidget(1, QFormLayout::FieldRole, lockUntilBox);
    }

    auto wrapPubkeyRow = [this](QFormLayout* form, QWidget* tabParent, QLineEdit* lineEdit,
                                QLabel*& lblHintOut, QPushButton*& btnCopyOut) {
        if (!form || !tabParent || !lineEdit) return;
        form->removeWidget(lineEdit);
        QWidget* box = new QWidget(tabParent);
        QVBoxLayout* v = new QVBoxLayout(box);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(4);
        QHBoxLayout* row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(8);
        lineEdit->setParent(box);
        lineEdit->setPlaceholderText(tr("66-char compressed secp256k1 pubkey hex (02.../03...)"));
        btnCopyOut = new QPushButton(tr("Copy"), box);
        btnCopyOut->setProperty("scSecondary", true);
        btnCopyOut->setFixedHeight(36);
        btnCopyOut->setMinimumWidth(64);
        btnCopyOut->setCursor(Qt::PointingHandCursor);
        btnCopyOut->setFocusPolicy(Qt::TabFocus);
        btnCopyOut->setToolTip(tr("Copy this exact public key hex to clipboard"));
        connect(btnCopyOut, &QPushButton::clicked, this, [this, lineEdit]() {
            const QString hex = lineEdit->text().trimmed();
            if (hex.isEmpty()) {
                inform(tr("Public key field is empty."));
                return;
            }
            GUIUtil::setClipboard(hex);
            inform(tr("Public key copied to clipboard:\n%1").arg(hex));
        });
        row->addWidget(lineEdit, 1);
        row->addWidget(btnCopyOut, 0);
        lblHintOut = new QLabel(box);
        lblHintOut->setWordWrap(true);
        v->addLayout(row);
        v->addWidget(lblHintOut);
        form->setWidget(2, QFormLayout::FieldRole, box);
    };
    wrapPubkeyRow(ui->formLayout_TimeLock, ui->tabTimeLock, ui->lineEditPubkeyTimeLock, lblTlPubkeyHint, btnCopyTlPubkey);
    wrapPubkeyRow(ui->formLayout_HashLock, ui->tabHashLock, ui->lineEditPubkeyHashLock, lblHlPubkeyHint, btnCopyHlPubkey);

    clearLegacySettingsConstraints(ui->plainTextEditKeysMultiSig);
    ui->plainTextEditKeysMultiSig->setMinimumHeight(76);
    ui->plainTextEditKeysMultiSig->setProperty("scInput", true);
    ui->plainTextEditKeysMultiSig->setProperty("scMono", true);

    GUIUtil::setupAmountWidget(ui->lineEditAmount, this);
    ui->lineEditAmount->clear();
    ui->lineEditAmount->setPlaceholderText(tr("0.00"));

    clearLegacySettingsConstraints(ui->btnSave);
    ui->btnSave->setText(tr("Export Spec ▾"));
    ui->btnSave->setToolTip(tr("Export the current MESCAL contract specification to disk as YAML, JSON, or both."));
    ui->btnSave->setProperty("scSecondary", true);
    ui->btnSave->setFixedHeight(38);
    ui->btnSave->setCursor(Qt::PointingHandCursor);
    ui->btnSave->setFocusPolicy(Qt::TabFocus);

    clearLegacySettingsConstraints(ui->btnPublish);
    ui->btnPublish->setProperty("scPrimary", true);
    ui->btnPublish->setFixedHeight(38);
    ui->btnPublish->setCursor(Qt::PointingHandCursor);
    ui->btnPublish->setFocusPolicy(Qt::TabFocus);

    ui->labelStatus->setWordWrap(true);

    // Restructure Right Card (Human-Readable Summary + Live Output Preview) cleanly
    if (ui->verticalLayout_Right) {
        while (ui->verticalLayout_Right->count() > 0) {
            QLayoutItem* item = ui->verticalLayout_Right->takeAt(0);
            delete item;
        }
        QHBoxLayout* rightHeader = new QHBoxLayout();
        rightHeader->setContentsMargins(0, 0, 0, 2);
        rightHeader->setSpacing(10);

        clearLegacySettingsConstraints(ui->labelTitleRight);
        ui->labelTitleRight->setText(tr("Contract Summary"));
        ui->labelTitleRight->setProperty("scCardTitle", true);

        clearLegacySettingsConstraints(ui->cbDeveloperMode);
        ui->cbDeveloperMode->setProperty("scCheck", true);
        ui->cbDeveloperMode->setCursor(Qt::PointingHandCursor);
        ui->cbDeveloperMode->setFocusPolicy(Qt::TabFocus);

        rightHeader->addWidget(ui->labelTitleRight);
        rightHeader->addStretch(1);
        rightHeader->addWidget(ui->cbDeveloperMode, 0, Qt::AlignVCenter);
        ui->verticalLayout_Right->addLayout(rightHeader);
        ui->verticalLayout_Right->addWidget(makeDivider(ui->right));

        // Human-Readable Contract Summary Card (shown when Developer Mode is off)
        summaryCard = new QFrame(ui->right);
        summaryCard->setObjectName("scSummaryCard");
        summaryCard->setFrameShape(QFrame::NoFrame);
        summaryCard->setProperty("scSubCard", true);
        summaryCard->setAttribute(Qt::WA_StyledBackground, true);
        QFormLayout* sumForm = new QFormLayout(summaryCard);
        sumForm->setContentsMargins(14, 12, 14, 12);
        sumForm->setHorizontalSpacing(14);
        sumForm->setVerticalSpacing(9);
        sumForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);

        auto addSumRow = [&](const QString& key, QLabel*& valOut, bool mono = false) {
            QLabel* k = new QLabel(key, summaryCard);
            k->setProperty("scInfoKey", true);
            valOut = new QLabel(QStringLiteral("—"), summaryCard);
            valOut->setWordWrap(true);
            valOut->setTextInteractionFlags(Qt::TextSelectableByMouse);
            valOut->setProperty(mono ? "scInfoMono" : "scInfoVal", true);
            sumForm->addRow(k, valOut);
        };
        addSumRow(tr("Contract:"), lblSumContractVal);
        addSumRow(tr("Funding:"), lblSumAmountVal);
        addSumRow(tr("Unlock Rule:"), lblSumRuleVal);
        addSumRow(tr("Authorized Key(s):"), lblSumKeysVal, true);
        addSumRow(tr("Script & Est. Fee:"), lblSumScriptVal);
        ui->verticalLayout_Right->addWidget(summaryCard);

        btnToggleTechSpec = new QPushButton(tr("Show Technical Spec (YAML / JSON)"), ui->right);
        btnToggleTechSpec->setProperty("scSecondary", true);
        btnToggleTechSpec->setFixedHeight(32);
        btnToggleTechSpec->setCursor(Qt::PointingHandCursor);
        btnToggleTechSpec->setFocusPolicy(Qt::TabFocus);
        ui->verticalLayout_Right->addWidget(btnToggleTechSpec);
        connect(btnToggleTechSpec, &QPushButton::clicked, this, [this]() {
            m_showTechSpec = !m_showTechSpec;
            updateRightPaneVisibility();
        });

        devPreviewContainer = new QWidget(ui->right);
        QVBoxLayout* devLayout = new QVBoxLayout(devPreviewContainer);
        devLayout->setContentsMargins(0, 0, 0, 0);
        devLayout->setSpacing(8);

        QHBoxLayout* yamlHeader = new QHBoxLayout();
        yamlHeader->setContentsMargins(0, 2, 0, 0);
        clearLegacySettingsConstraints(ui->lblYAML);
        ui->lblYAML->setParent(devPreviewContainer);
        ui->lblYAML->setProperty("scSectionLabel", true);
        QPushButton* btnCopyYaml = new QPushButton(tr("Copy YAML"), devPreviewContainer);
        btnCopyYaml->setProperty("scMiniBtn", true);
        btnCopyYaml->setFixedHeight(26);
        btnCopyYaml->setCursor(Qt::PointingHandCursor);
        btnCopyYaml->setFocusPolicy(Qt::TabFocus);
        yamlHeader->addWidget(ui->lblYAML);
        yamlHeader->addStretch(1);
        yamlHeader->addWidget(btnCopyYaml);
        devLayout->addLayout(yamlHeader);

        clearLegacySettingsConstraints(ui->textEditYAML);
        ui->textEditYAML->setParent(devPreviewContainer);
        ui->textEditYAML->setFrameShape(QFrame::NoFrame);
        ui->textEditYAML->setProperty("scCodeBox", true);
        m_yamlHighlighter = new YamlSyntaxHighlighter(ui->textEditYAML);
        devLayout->addWidget(ui->textEditYAML, 1);

        QHBoxLayout* jsonHeader = new QHBoxLayout();
        jsonHeader->setContentsMargins(0, 4, 0, 0);
        clearLegacySettingsConstraints(ui->lblJSON);
        ui->lblJSON->setParent(devPreviewContainer);
        ui->lblJSON->setProperty("scSectionLabel", true);
        QPushButton* btnCopyJson = new QPushButton(tr("Copy JSON"), devPreviewContainer);
        btnCopyJson->setProperty("scMiniBtn", true);
        btnCopyJson->setFixedHeight(26);
        btnCopyJson->setCursor(Qt::PointingHandCursor);
        btnCopyJson->setFocusPolicy(Qt::TabFocus);
        jsonHeader->addWidget(ui->lblJSON);
        jsonHeader->addStretch(1);
        jsonHeader->addWidget(btnCopyJson);
        devLayout->addLayout(jsonHeader);

        clearLegacySettingsConstraints(ui->textEditJSON);
        ui->textEditJSON->setParent(devPreviewContainer);
        ui->textEditJSON->setFrameShape(QFrame::NoFrame);
        ui->textEditJSON->setProperty("scCodeBox", true);
        devLayout->addWidget(ui->textEditJSON, 1);

        ui->verticalLayout_Right->addWidget(devPreviewContainer, 1);
        ui->verticalLayout_Right->addStretch(0);

        connect(btnCopyYaml, &QPushButton::clicked, this, [this]() {
            const QString txt = ui->textEditYAML->toPlainText();
            if (txt.isEmpty()) return;
            GUIUtil::setClipboard(txt);
            inform(tr("YAML spec copied to clipboard."));
        });
        connect(btnCopyJson, &QPushButton::clicked, this, [this]() {
            const QString txt = ui->textEditJSON->toPlainText();
            if (txt.isEmpty()) return;
            GUIUtil::setClipboard(txt);
            inform(tr("JSON spec copied to clipboard."));
        });
        updateRightPaneVisibility();
    }

    // Live preview connections
    connect(ui->lineEditNameTimeLock,   &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditExpiryTimeLock, &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditPubkeyTimeLock, &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    if (comboLockMode) {
        connect(comboLockMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            syncLockModeUi();
            const bool isTsMode = (comboLockMode->currentData().toInt() == 1);
            if (isTsMode && dateTimeEditLockTime) {
                bool ok = false;
                const qint64 curVal = ui->lineEditExpiryTimeLock->text().trimmed().toLongLong(&ok);
                if (!ok || curVal < LOCKTIME_THRESHOLD || curVal > 0xFFFFFFFFLL) {
                    const QDateTime defDt = QDateTime::currentDateTimeUtc().addDays(30);
                    {
                        QSignalBlocker bDt(dateTimeEditLockTime);
                        dateTimeEditLockTime->setDateTime(defDt);
                    }
                    QSignalBlocker bEd(ui->lineEditExpiryTimeLock);
                    ui->lineEditExpiryTimeLock->setText(QString::number(defDt.toSecsSinceEpoch()));
                } else {
                    QSignalBlocker bDt(dateTimeEditLockTime);
                    dateTimeEditLockTime->setDateTime(QDateTime::fromSecsSinceEpoch(curVal, Qt::UTC));
                }
            } else if (!isTsMode) {
                bool ok = false;
                const qint64 curVal = ui->lineEditExpiryTimeLock->text().trimmed().toLongLong(&ok);
                if (!ok || curVal >= LOCKTIME_THRESHOLD || curVal <= 0) {
                    int tipHeight = 0;
                    {
                        TRY_LOCK(cs_main, lockMain);
                        if (lockMain) tipHeight = chainActive.Height();
                    }
                    const int targetHeight = tipHeight > 0 ? (tipHeight + 2880) : 10000;
                    QSignalBlocker bEd(ui->lineEditExpiryTimeLock);
                    ui->lineEditExpiryTimeLock->setText(QString::number(targetHeight));
                }
            }
            generateContract();
        });
    }
    if (dateTimeEditLockTime) {
        connect(dateTimeEditLockTime, &QDateTimeEdit::dateTimeChanged, this, [this](const QDateTime& dt) {
            if (!comboLockMode || comboLockMode->currentData().toInt() != 1) return;
            const qint64 secs = dt.toUTC().toSecsSinceEpoch();
            {
                QSignalBlocker bEd(ui->lineEditExpiryTimeLock);
                ui->lineEditExpiryTimeLock->setText(QString::number(secs));
            }
            generateContract();
        });
    }
    connect(ui->lineEditNameMultiSig,   &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditNMultiSig,      &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditMMultiSig,      &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->plainTextEditKeysMultiSig, &QPlainTextEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditNameHashLock,   &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditHashHashLock,   &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditPubkeyHashLock, &QLineEdit::textChanged, this, [this]() {
        updateOwnerPubKeyHints();
        generateContract();
    });
    connect(ui->lineEditPubkeyTimeLock, &QLineEdit::textChanged, this, [this]() {
        updateOwnerPubKeyHints();
    });
    connect(ui->lineEditAmount, &QLineEdit::textChanged, this, [this]() {
        updateContractSummary();
    });
    connect(ui->tabWidget, &QTabWidget::currentChanged, this, [this](int tabIdx) {
        if (!m_syncingTemplateOrTab && ui->comboTemplates) {
            m_syncingTemplateOrTab = true;
            QSignalBlocker bTpl(ui->comboTemplates);
            if (tabIdx == 0) ui->comboTemplates->setCurrentIndex(1);
            else if (tabIdx == 1) ui->comboTemplates->setCurrentIndex(2);
            else if (tabIdx == 2) ui->comboTemplates->setCurrentIndex(3);
            else if (tabIdx == 3 && ui->comboTemplates->currentIndex() <= 3) ui->comboTemplates->setCurrentIndex(0);
            m_syncingTemplateOrTab = false;
        }
        generateContract();
    });

    connect(ui->btnSave,    &QPushButton::clicked, this, &SmartContractWidget::onSaveClicked);
    connect(ui->btnPublish, &QPushButton::clicked, this, &SmartContractWidget::onPublishClicked);
    ui->comboTemplates->setCurrentIndex(1); // Default to Time-Locked Deposit (after lock UI initialized)
    syncLockModeUi();

    // ── Run tab ───────────────────────────────────────────────────────────────
    if (ui->horizontalLayout_Run) {
        ui->horizontalLayout_Run->setContentsMargins(0, 0, 0, 0);
        ui->horizontalLayout_Run->setSpacing(16);
        ui->horizontalLayout_Run->setStretch(0, 1);
        ui->horizontalLayout_Run->setStretch(1, 1);
    }

    clearLegacySettingsConstraints(ui->runLeft);
    ui->runLeft->setProperty("scCard", true);
    ui->runLeft->setAttribute(Qt::WA_StyledBackground, true);
    if (ui->verticalLayout_RunLeft) {
        ui->verticalLayout_RunLeft->setContentsMargins(18, 16, 18, 16);
        ui->verticalLayout_RunLeft->setSpacing(10);
    }

    clearLegacySettingsConstraints(ui->runRight);
    ui->runRight->setProperty("scCard", true);
    ui->runRight->setAttribute(Qt::WA_StyledBackground, true);
    if (ui->verticalLayout_RunRight) {
        ui->verticalLayout_RunRight->setContentsMargins(18, 16, 18, 16);
        ui->verticalLayout_RunRight->setSpacing(10);
    }

    clearLegacySettingsConstraints(ui->labelRunTitle);
    ui->labelRunTitle->setProperty("scCardTitle", true);
    clearLegacySettingsConstraints(ui->labelRunSubtitle);
    ui->labelRunSubtitle->setProperty("scCardDesc", true);

    configureCombo(ui->comboContracts);
    clearLegacySettingsConstraints(ui->btnRefreshContracts);
    ui->btnRefreshContracts->setProperty("scSecondary", true);
    ui->btnRefreshContracts->setFixedSize(105, 36);
    ui->btnRefreshContracts->setCursor(Qt::PointingHandCursor);
    ui->btnRefreshContracts->setFocusPolicy(Qt::TabFocus);

    for (QLabel* lbl : {ui->lblSelectContract, ui->lblRecipAddr, ui->lblParam1, ui->lblParam2, ui->lblParam3}) {
        clearLegacySettingsConstraints(lbl);
        lbl->setProperty("scFieldLabel", true);
        lbl->setMinimumWidth(130);
    }
    clearLegacySettingsConstraints(ui->lblUnlockParams);
    ui->lblUnlockParams->setProperty("scSectionLabel", true);

    for (QLineEdit* le : {ui->lineEditRecipAddr, ui->lineEditParam1, ui->lineEditParam2}) {
        clearLegacySettingsConstraints(le);
        le->setFixedHeight(36);
        le->setAttribute(Qt::WA_MacShowFocusRect, false);
        le->setProperty("scInput", true);
        le->setProperty("scMono", true);
    }

    clearLegacySettingsConstraints(ui->plainTextExtraSigs);
    ui->plainTextExtraSigs->setMinimumHeight(72);
    ui->plainTextExtraSigs->setMaximumHeight(96);
    ui->plainTextExtraSigs->setProperty("scInput", true);
    ui->plainTextExtraSigs->setProperty("scMono", true);

    clearLegacySettingsConstraints(ui->btnRunContract);
    ui->btnRunContract->setProperty("scPrimary", true);
    ui->btnRunContract->setFixedHeight(38);
    ui->btnRunContract->setCursor(Qt::PointingHandCursor);
    ui->btnRunContract->setFocusPolicy(Qt::TabFocus);

    ui->labelRunStatus->setProperty("scCardDesc", true);
    ui->labelRunStatus->setWordWrap(true);

    if (ui->chkShowZeroPow) {
        clearLegacySettingsConstraints(ui->chkShowZeroPow);
        ui->chkShowZeroPow->setProperty("scCheck", true);
        ui->chkShowZeroPow->setCursor(Qt::PointingHandCursor);
        ui->chkShowZeroPow->setFocusPolicy(Qt::TabFocus);
        connect(ui->chkShowZeroPow, &QCheckBox::toggled, this, &SmartContractWidget::onRefreshContractsClicked);
    }

    // Restructure Right Card (Run Contract Details) cleanly
    if (ui->verticalLayout_RunRight) {
        while (ui->verticalLayout_RunRight->count() > 0) {
            QLayoutItem* item = ui->verticalLayout_RunRight->takeAt(0);
            delete item;
        }
        clearLegacySettingsConstraints(ui->labelRunRightTitle);
        ui->labelRunRightTitle->setProperty("scCardTitle", true);
        ui->verticalLayout_RunRight->addWidget(ui->labelRunRightTitle);
        ui->verticalLayout_RunRight->addWidget(makeDivider(ui->runRight));

        ui->frameUtxoInfo->setFrameShape(QFrame::NoFrame);
        ui->frameUtxoInfo->setProperty("scSubCard", true);
        ui->frameUtxoInfo->setAttribute(Qt::WA_StyledBackground, true);
        if (ui->formLayoutUtxo) {
            ui->formLayoutUtxo->setContentsMargins(14, 12, 14, 12);
            ui->formLayoutUtxo->setVerticalSpacing(8);
            ui->formLayoutUtxo->setHorizontalSpacing(14);

            // Replace row 0 col 1 with TxID label + Copy button
            ui->formLayoutUtxo->removeWidget(ui->labelUtxoTxId);
            QWidget* txIdRow = new QWidget(ui->frameUtxoInfo);
            QHBoxLayout* txIdLayout = new QHBoxLayout(txIdRow);
            txIdLayout->setContentsMargins(0, 0, 0, 0);
            txIdLayout->setSpacing(8);
            clearLegacySettingsConstraints(ui->labelUtxoTxId);
            ui->labelUtxoTxId->setParent(txIdRow);
            ui->labelUtxoTxId->setWordWrap(true);
            ui->labelUtxoTxId->setProperty("scInfoMono", true);
            QPushButton* btnCopyTxId = new QPushButton(tr("Copy"), txIdRow);
            btnCopyTxId->setProperty("scMiniBtn", true);
            btnCopyTxId->setFixedHeight(24);
            btnCopyTxId->setCursor(Qt::PointingHandCursor);
            btnCopyTxId->setFocusPolicy(Qt::TabFocus);
            txIdLayout->addWidget(ui->labelUtxoTxId, 1);
            txIdLayout->addWidget(btnCopyTxId, 0, Qt::AlignTop);
            ui->formLayoutUtxo->setWidget(0, QFormLayout::FieldRole, txIdRow);

            connect(btnCopyTxId, &QPushButton::clicked, this, [this]() {
                const QString txid = ui->labelUtxoTxId->text().trimmed();
                if (txid.isEmpty() || txid == QStringLiteral("—")) return;
                GUIUtil::setClipboard(txid);
                inform(tr("UTXO TxID copied to clipboard."));
            });
        }
        for (QLabel* lbl : {ui->lblUtxoTxId, ui->lblUtxoVout, ui->lblUtxoAmount, ui->lblUtxoConfs}) {
            clearLegacySettingsConstraints(lbl);
            lbl->setProperty("scInfoKey", true);
        }
        for (QLabel* lbl : {ui->labelUtxoVout, ui->labelUtxoAmount, ui->labelUtxoConfs}) {
            clearLegacySettingsConstraints(lbl);
            lbl->setProperty("scInfoVal", true);
        }
        ui->verticalLayout_RunRight->addWidget(ui->frameUtxoInfo);

        QHBoxLayout* decHeader = new QHBoxLayout();
        decHeader->setContentsMargins(0, 4, 0, 0);
        clearLegacySettingsConstraints(ui->lblDecompiledTitle);
        ui->lblDecompiledTitle->setProperty("scSectionLabel", true);
        QPushButton* btnCopyDecJson = new QPushButton(tr("Copy JSON"), ui->runRight);
        btnCopyDecJson->setProperty("scMiniBtn", true);
        btnCopyDecJson->setFixedHeight(26);
        btnCopyDecJson->setCursor(Qt::PointingHandCursor);
        btnCopyDecJson->setFocusPolicy(Qt::TabFocus);
        decHeader->addWidget(ui->lblDecompiledTitle);
        decHeader->addStretch(1);
        decHeader->addWidget(btnCopyDecJson);
        ui->verticalLayout_RunRight->addLayout(decHeader);

        clearLegacySettingsConstraints(ui->textEditDecompiled);
        ui->textEditDecompiled->setFrameShape(QFrame::NoFrame);
        ui->textEditDecompiled->setProperty("scCodeBox", true);
        ui->verticalLayout_RunRight->addWidget(ui->textEditDecompiled, 1);

        QHBoxLayout* rawHeader = new QHBoxLayout();
        rawHeader->setContentsMargins(0, 4, 0, 0);
        clearLegacySettingsConstraints(ui->lblRawScript);
        ui->lblRawScript->setProperty("scSectionLabel", true);
        QPushButton* btnCopyRawHex = new QPushButton(tr("Copy Hex"), ui->runRight);
        btnCopyRawHex->setProperty("scMiniBtn", true);
        btnCopyRawHex->setFixedHeight(26);
        btnCopyRawHex->setCursor(Qt::PointingHandCursor);
        btnCopyRawHex->setFocusPolicy(Qt::TabFocus);
        rawHeader->addWidget(ui->lblRawScript);
        rawHeader->addStretch(1);
        rawHeader->addWidget(btnCopyRawHex);
        ui->verticalLayout_RunRight->addLayout(rawHeader);

        clearLegacySettingsConstraints(ui->lineEditRawScript);
        ui->lineEditRawScript->setFixedHeight(36);
        ui->lineEditRawScript->setAttribute(Qt::WA_MacShowFocusRect, false);
        ui->lineEditRawScript->setProperty("scInputReadOnly", true);
        ui->verticalLayout_RunRight->addWidget(ui->lineEditRawScript);

        connect(btnCopyDecJson, &QPushButton::clicked, this, [this]() {
            const QString txt = ui->textEditDecompiled->toPlainText();
            if (txt.isEmpty()) return;
            GUIUtil::setClipboard(txt);
            inform(tr("Decompiled contract JSON copied to clipboard."));
        });
        connect(btnCopyRawHex, &QPushButton::clicked, this, [this]() {
            const QString txt = ui->lineEditRawScript->text().trimmed();
            if (txt.isEmpty()) return;
            GUIUtil::setClipboard(txt);
            inform(tr("Raw scriptPubKey hex copied to clipboard."));
        });
    }

    connect(btnModeBuild, &QPushButton::clicked, this, [this]() {
        if (ui->tabWidgetMode) ui->tabWidgetMode->setCurrentIndex(0);
    });
    connect(btnModeRun, &QPushButton::clicked, this, [this]() {
        if (ui->tabWidgetMode) ui->tabWidgetMode->setCurrentIndex(1);
    });
    connect(ui->btnRefreshContracts, &QPushButton::clicked, this, &SmartContractWidget::onRefreshContractsClicked);
    connect(ui->comboContracts, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SmartContractWidget::onContractSelected);
    connect(ui->btnRunContract, &QPushButton::clicked, this, &SmartContractWidget::onRunContractClicked);
    connect(ui->tabWidgetMode, &QTabWidget::currentChanged, this, [this](int index){
        if (btnModeBuild && btnModeRun) {
            btnModeBuild->setChecked(index == 0);
            btnModeRun->setChecked(index == 1);
        }
        if (index == 1 && m_contractsDirty) {
            onRefreshContractsClicked();
        }
    });

    // ── Custom tab styling & connections ──────────────────────────────────────
    for (QPushButton* cBtn : {ui->btnAddAction, ui->btnRemoveAction, ui->btnMoveActionUp, ui->btnMoveActionDown}) {
        clearLegacySettingsConstraints(cBtn);
        cBtn->setProperty("scSecondary", true);
        cBtn->setFixedHeight(34);
        cBtn->setCursor(Qt::PointingHandCursor);
        cBtn->setFocusPolicy(Qt::TabFocus);
    }
    clearLegacySettingsConstraints(ui->btnOpenVisualDesigner);
    ui->btnOpenVisualDesigner->setProperty("scPrimary", true);
    ui->btnOpenVisualDesigner->setFixedHeight(36);
    ui->btnOpenVisualDesigner->setCursor(Qt::PointingHandCursor);
    ui->btnOpenVisualDesigner->setFocusPolicy(Qt::TabFocus);

    clearLegacySettingsConstraints(ui->treeCustomActions);
    ui->treeCustomActions->setProperty("scTree", true);
    ui->treeCustomActions->setAlternatingRowColors(true);
    if (ui->treeCustomActions->header()) {
        ui->treeCustomActions->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        ui->treeCustomActions->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    }
    ui->treeCustomActions->setToolTip(tr("Double-click any step to edit its parameters."));

    connect(ui->btnAddAction, &QPushButton::clicked, this, &SmartContractWidget::onAddActionClicked);
    connect(ui->treeCustomActions, &QTreeWidget::itemDoubleClicked, this, &SmartContractWidget::onEditAction);
    connect(ui->btnRemoveAction, &QPushButton::clicked, this, &SmartContractWidget::onRemoveActionClicked);
    connect(ui->btnMoveActionUp, &QPushButton::clicked, this, &SmartContractWidget::onMoveActionUp);
    connect(ui->btnMoveActionDown, &QPushButton::clicked, this, &SmartContractWidget::onMoveActionDown);
    connect(ui->btnOpenVisualDesigner, &QPushButton::clicked, this, &SmartContractWidget::onOpenVisualDesignerClicked);
    connect(ui->cbDeveloperMode, &QCheckBox::toggled, this, &SmartContractWidget::onDeveloperModeToggled);
    connect(ui->textEditYAML, &QTextEdit::textChanged, this, &SmartContractWidget::onYAMLTextChanged);

    customActions = UniValue(UniValue::VARR);

    applyTheme(isLightTheme());

    // Initial state
    generateContract();
    clearRunDetails();
}

SmartContractWidget::~SmartContractWidget()
{
    delete ui;
}

// ─── loadWalletModel ──────────────────────────────────────────────────────────

void SmartContractWidget::loadWalletModel()
{
    if (walletModel) {
        m_walletPubKey = CPubKey();
        if (pwalletMain) {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            CPubKey fallbackMnKey;
            for (const auto& kv : pwalletMain->mapAddressBook) {
                if (kv.second.purpose != AddressBook::AddressBookPurpose::RECEIVE) continue;
                const CKeyID* kid = boost::get<CKeyID>(&kv.first);
                if (!kid) continue;
                CPubKey cand;
                if (!pwalletMain->GetPubKey(*kid, cand) || !cand.IsValid() || !cand.IsCompressed()) continue;
                const std::string& lbl = kv.second.name;
                const bool isMn = (lbl.compare(0, 3, "mn_") == 0 ||
                                   lbl.compare(0, 8, "local_mn") == 0 ||
                                   lbl.compare(0, 3, "mn0") == 0);
                if (!isMn) {
                    m_walletPubKey = cand;
                    break;
                }
                if (!fallbackMnKey.IsValid()) fallbackMnKey = cand;
            }
            if (!m_walletPubKey.IsValid() && fallbackMnKey.IsValid()) {
                m_walletPubKey = fallbackMnKey;
            }
            if (!m_walletPubKey.IsValid()) {
                pwalletMain->GetKeyFromPool(m_walletPubKey);
            }
        }
        if (m_walletPubKey.IsValid()) {
            std::string pubkeyHex = HexStr(m_walletPubKey.begin(), m_walletPubKey.end());
            ui->lineEditPubkeyTimeLock->setText(QString::fromStdString(pubkeyHex));
            ui->lineEditPubkeyHashLock->setText(QString::fromStdString(pubkeyHex));
            if (ui->lineEditRecipAddr->text().trimmed().isEmpty()) {
                ui->lineEditRecipAddr->setText(QString::fromStdString(EncodeDestination(m_walletPubKey.GetID())));
            }
        }
        {
            LOCK(cs_main);
            if (chainActive.Height() > 0 && ui->lineEditExpiryTimeLock->text() == "150") {
                ui->lineEditExpiryTimeLock->setText(QString::number(chainActive.Height() + 100));
            }
        }
        updateOwnerPubKeyHints();
        generateContract();

        // Defer full mapWallet contract scans until Run Contract tab is actually visible
        connect(walletModel, &WalletModel::balanceChanged, this, [this]() {
            updateContractSummary();
            if (isVisible() && ui->tabWidgetMode->currentIndex() == 1) {
                onRefreshContractsClicked();
            } else {
                m_contractsDirty = true;
            }
        });
        updateContractSummary();
        m_contractsDirty = true;
    }
}

void SmartContractWidget::showEvent(QShowEvent* event)
{
    PWidget::showEvent(event);
    if (m_contractsDirty && ui->tabWidgetMode->currentIndex() == 1) {
        onRefreshContractsClicked();
    }
}

// ═══════════════════════════════════════════════════════════════════
//  BUILD TAB
// ═══════════════════════════════════════════════════════════════════

void SmartContractWidget::updateOwnerPubKeyHints()
{
    const bool light = isLightTheme();
    const QString ownedCol = light ? QStringLiteral("#15803D") : QStringLiteral("#4ADE80");
    const QString extCol   = light ? QStringLiteral("#B45309") : QStringLiteral("#FBBF24");
    const QString errCol   = light ? QStringLiteral("#DC2626") : QStringLiteral("#F87171");
    const QString mutedCol = light ? QStringLiteral("#64748B") : QStringLiteral("#94A3B8");

    auto updateOne = [&](QLineEdit* le, QLabel* lbl) {
        if (!le || !lbl) return;
        auto setHint = [&](const QString& text, const QString& col) {
            lbl->setText(text);
            lbl->setStyleSheet(QString("QLabel { color: %1; font-size: 11px; background: transparent; border: none; }").arg(col));
        };
        const QString hex = le->text().trimmed();
        if (hex.isEmpty()) {
            setHint(tr("Enter a 66-char compressed secp256k1 public key (02.../03...)"), mutedCol);
            return;
        }
        const std::vector<unsigned char> raw = ParseHex(hex.toStdString());
        if (hex.length() != 66 || raw.size() != CPubKey::COMPRESSED_PUBLIC_KEY_SIZE) {
            setHint(tr("Invalid length (%1/66 hex chars) — requires a 33-byte compressed pubkey (02.../03...)").arg(hex.length()), errCol);
            return;
        }
        CPubKey pk(raw.begin(), raw.end());
        if (!pk.IsFullyValid()) {
            setHint(tr("Invalid secp256k1 public key"), errCol);
            return;
        }
        const CKeyID kid = pk.GetID();
        const QString addrStr = QString::fromStdString(EncodeDestination(kid));
        if (!pwalletMain) {
            setHint(tr("Valid compressed pubkey • %1").arg(addrStr), mutedCol);
            return;
        }
        bool isMine = false;
        bool lockedOk = false;
        QString addrLabel;
        {
            TRY_LOCK(pwalletMain->cs_wallet, lockWallet);
            if (lockWallet) {
                lockedOk = true;
                isMine = pwalletMain->HaveKey(kid);
                auto itBook = pwalletMain->mapAddressBook.find(CTxDestination(kid));
                if (itBook != pwalletMain->mapAddressBook.end() && !itBook->second.name.empty()) {
                    addrLabel = QString::fromStdString(itBook->second.name).trimmed();
                }
            }
        }
        if (!lockedOk) {
            setHint(tr("Valid compressed pubkey • %1").arg(addrStr), mutedCol);
        } else if (isMine) {
            const QString tag = addrLabel.isEmpty() ? addrStr : QString("%1 (%2)").arg(addrStr, addrLabel);
            setHint(QString::fromUtf8("\xE2\x9C\x93 ") + tr("Owned by this wallet • %1").arg(tag), ownedCol);
        } else {
            setHint(tr("External public key (not in this wallet) • %1").arg(addrStr), extCol);
        }
    };

    updateOne(ui->lineEditPubkeyTimeLock, lblTlPubkeyHint);
    updateOne(ui->lineEditPubkeyHashLock, lblHlPubkeyHint);
}

void SmartContractWidget::syncLockModeUi()
{
    if (!comboLockMode || !dateTimeEditLockTime || !ui->lineEditExpiryTimeLock) return;
    const bool isTsMode = (comboLockMode->currentData().toInt() == 1);
    dateTimeEditLockTime->setVisible(isTsMode);
    ui->lineEditExpiryTimeLock->setVisible(!isTsMode);
    updateLockUntilPreview();
    updateOwnerPubKeyHints();
}

void SmartContractWidget::updateLockUntilPreview()
{
    if (!lblLockUntilPreview || !ui->lineEditExpiryTimeLock || !comboLockMode) return;
    const bool light = isLightTheme();
    const QString normalCol = light ? QStringLiteral("#475569") : QStringLiteral("#94A3B8");
    const QString warnCol   = light ? QStringLiteral("#B45309") : QStringLiteral("#FBBF24");
    const QString errCol    = light ? QStringLiteral("#B93E33") : QStringLiteral("#F08074");

    auto setPreview = [&](const QString& text, const QString& color) {
        lblLockUntilPreview->setText(text);
        lblLockUntilPreview->setStyleSheet(QStringLiteral(
            "QLabel { color: %1; font-size: 11px; font-weight: 500; background: transparent; border: none; }").arg(color));
    };

    const QString raw = ui->lineEditExpiryTimeLock->text().trimmed();
    bool ok = false;
    const qint64 val = raw.toLongLong(&ok);
    if (!ok) {
        setPreview(tr("✗ Must be an integer block height (< 500,000,000) or UNIX timestamp (>= 500,000,000)."), errCol);
        return;
    }
    if (val < 0 || val > 0xFFFFFFFFLL) {
        setPreview(tr("✗ Value exceeds 32-bit nLockTime limit (0 .. 4,294,967,295) and would be unspendable!"), errCol);
        return;
    }

    const bool isTsMode = (comboLockMode->currentData().toInt() == 1);
    const qint64 nowTs = std::max<qint64>(QDateTime::currentSecsSinceEpoch(), GetAdjustedTime());
    int curHeight = 0;
    {
        TRY_LOCK(cs_main, lockMain);
        if (lockMain) curHeight = chainActive.Height();
    }
    const qint64 targetSpacing = std::max<qint64>(1, Params().GetConsensus().nTargetSpacing);

    if (val >= LOCKTIME_THRESHOLD) {
        const QString utcStr = QDateTime::fromSecsSinceEpoch(val, Qt::UTC).toString(QStringLiteral("yyyy-MM-dd HH:mm 'UTC'"));
        if (!isTsMode) {
            setPreview(tr("⚠ Value %1 is >= 500,000,000 so consensus interprets it as UNIX timestamp: %2")
                .arg(QLocale().toString(static_cast<qlonglong>(val)), utcStr), warnCol);
        } else if (val <= nowTs) {
            setPreview(tr("⚠ Unlocks at %1 (epoch %2) — ALREADY IN THE PAST (no timelock delay).")
                .arg(utcStr).arg(val), warnCol);
        } else if ((val - nowTs) > 5LL * 365 * 86400) {
            setPreview(tr("⚠ Long lock: %1 (%2 from now, epoch %3) — double-check year!")
                .arg(utcStr, FormatLockDurationApprox(val - nowTs)).arg(val), warnCol);
        } else {
            setPreview(tr("Unlocks at %1 (%2 from now • epoch %3)")
                .arg(utcStr, FormatLockDurationApprox(val - nowTs)).arg(val), normalCol);
        }
    } else {
        if (isTsMode) {
            setPreview(tr("⚠ Value %1 is < 500,000,000 so consensus interprets it as block height #%1.").arg(val), warnCol);
        } else if (curHeight > 0 && val <= curHeight) {
            setPreview(tr("⚠ Block #%1 has ALREADY PASSED (current height: #%2 — no timelock delay).")
                .arg(QLocale().toString(static_cast<qlonglong>(val)))
                .arg(QLocale().toString(curHeight)), warnCol);
        } else if (curHeight > 0) {
            const qint64 deltaBlocks = val - curHeight;
            const qint64 estSecs = deltaBlocks * targetSpacing;
            if (estSecs > 5LL * 365 * 86400) {
                setPreview(tr("⚠ Block #%1 is +%2 blocks ahead (%3 at %4s/blk)! Verify you didn't enter a date like YYYYMMDD.")
                    .arg(QLocale().toString(static_cast<qlonglong>(val)))
                    .arg(QLocale().toString(static_cast<qlonglong>(deltaBlocks)))
                    .arg(FormatLockDurationApprox(estSecs))
                    .arg(targetSpacing), warnCol);
            } else {
                setPreview(tr("Block height #%1 (+%2 blocks from #%3 • %4 est.)")
                    .arg(QLocale().toString(static_cast<qlonglong>(val)))
                    .arg(QLocale().toString(static_cast<qlonglong>(deltaBlocks)))
                    .arg(QLocale().toString(curHeight))
                    .arg(FormatLockDurationApprox(estSecs)), normalCol);
            }
        } else {
            setPreview(tr("Block height #%1").arg(QLocale().toString(static_cast<qlonglong>(val))), normalCol);
        }
    }
}

void SmartContractWidget::generateContract()
{
    int tabIndex = ui->tabWidget->currentIndex();
    UniValue doc(UniValue::VOBJ);
    UniValue basic(UniValue::VOBJ);
    UniValue contract(UniValue::VOBJ);

    if (tabIndex == 0) { // Time-Locked
        std::string name     = ui->lineEditNameTimeLock->text().toStdString();
        const QString expiryQ = ui->lineEditExpiryTimeLock->text().trimmed();
        std::string pubkey   = ui->lineEditPubkeyTimeLock->text().trimmed().toStdString();

        activeName = name;

        bool okExpiry = false;
        const qlonglong parsedExpiry = expiryQ.toLongLong(&okExpiry);
        UniValue expiryVal = okExpiry
            ? UniValue(static_cast<int64_t>(parsedExpiry))
            : UniValue(expiryQ.toStdString());

        updateLockUntilPreview();
        updateOwnerPubKeyHints();

        UniValue tlInputs(UniValue::VARR);
        UniValue tlInput1(UniValue::VOBJ);
        tlInput1.pushKV("name",  "Lock-Until");
        tlInput1.pushKV("type",  "timestamp-or-block-height");
        tlInput1.pushKV("value", expiryVal);
        tlInputs.push_back(tlInput1);

        UniValue tlSpec(UniValue::VOBJ);
        tlSpec.pushKV("role",   "lock-time");
        tlSpec.pushKV("inputs", tlInputs);
        basic.pushKV("Lock-Time", tlSpec);

        UniValue osInputs(UniValue::VARR);
        UniValue osInput1(UniValue::VOBJ);
        osInput1.pushKV("name",  "Pubkey");
        osInput1.pushKV("type",  "pubkey");
        osInput1.pushKV("value", pubkey);
        osInputs.push_back(osInput1);

        UniValue osSpec(UniValue::VOBJ);
        osSpec.pushKV("role",   "check-signature-verification");
        osSpec.pushKV("inputs", osInputs);
        basic.pushKV("Owner-Sig", osSpec);

        UniValue cSpec(UniValue::VOBJ);
        cSpec.pushKV("description", "Funds locked requiring owner signature.");
        UniValue actions(UniValue::VARR);
        UniValue action1(UniValue::VOBJ); action1.pushKV("type","basic"); action1.pushKV("name","Lock-Time");  actions.push_back(action1);
        UniValue action2(UniValue::VOBJ); action2.pushKV("type","basic"); action2.pushKV("name","Owner-Sig");  actions.push_back(action2);
        cSpec.pushKV("actions", actions);
        contract.pushKV(name, cSpec);

        doc.pushKV("basic",           basic);
        doc.pushKV("contract",        contract);
        doc.pushKV("active_contract", name);

    } else if (tabIndex == 1) { // Multi-Signature
        std::string name = ui->lineEditNameMultiSig->text().toStdString();
        int n = ui->lineEditNMultiSig->text().toInt();
        int m = ui->lineEditMMultiSig->text().toInt();
        QStringList keysList = ui->plainTextEditKeysMultiSig->toPlainText()
                                   .split('\n', QString::SkipEmptyParts);

        activeName = name;

        UniValue msInputs(UniValue::VARR);
        UniValue msInput1(UniValue::VOBJ); msInput1.pushKV("name","m"); msInput1.pushKV("type","number"); msInput1.pushKV("value",m); msInputs.push_back(msInput1);
        UniValue msInput2(UniValue::VOBJ); msInput2.pushKV("name","n"); msInput2.pushKV("type","number"); msInput2.pushKV("value",n); msInputs.push_back(msInput2);

        UniValue keysArray(UniValue::VARR);
        for (const QString& k : keysList)
            keysArray.push_back(k.trimmed().toStdString());

        UniValue msInput3(UniValue::VOBJ); msInput3.pushKV("name","Signatures"); msInput3.pushKV("type","array"); msInput3.pushKV("value",keysArray); msInputs.push_back(msInput3);

        UniValue msSpec(UniValue::VOBJ);
        msSpec.pushKV("role",   "multi-signature");
        msSpec.pushKV("inputs", msInputs);
        basic.pushKV("Multi-Signature", msSpec);

        UniValue cSpec(UniValue::VOBJ);
        cSpec.pushKV("description", std::to_string(m)+"-of-"+std::to_string(n)+" multi-signature smart contract.");
        UniValue actions(UniValue::VARR);
        UniValue action1(UniValue::VOBJ); action1.pushKV("type","basic"); action1.pushKV("name","Multi-Signature"); actions.push_back(action1);
        cSpec.pushKV("actions", actions);
        contract.pushKV(name, cSpec);

        doc.pushKV("basic",           basic);
        doc.pushKV("contract",        contract);
        doc.pushKV("active_contract", name);

    } else if (tabIndex == 2) { // Hash-Locked
        std::string name    = ui->lineEditNameHashLock->text().toStdString();
        std::string hash160 = ui->lineEditHashHashLock->text().trimmed().toStdString();
        std::string pubkey  = ui->lineEditPubkeyHashLock->text().trimmed().toStdString();

        activeName = name;

        UniValue hcInputs(UniValue::VARR);
        UniValue hcInput1(UniValue::VOBJ); hcInput1.pushKV("name","hash160"); hcInput1.pushKV("type","hash160"); hcInput1.pushKV("value",hash160); hcInputs.push_back(hcInput1);

        UniValue hcSpec(UniValue::VOBJ);
        hcSpec.pushKV("role",   "hash160");
        hcSpec.pushKV("mode",   "hash-equalverify");
        hcSpec.pushKV("inputs", hcInputs);
        basic.pushKV("Hash-Check", hcSpec);

        UniValue rsInputs(UniValue::VARR);
        UniValue rsInput1(UniValue::VOBJ); rsInput1.pushKV("name","Pubkey"); rsInput1.pushKV("type","pubkey"); rsInput1.pushKV("value",pubkey); rsInputs.push_back(rsInput1);

        UniValue rsSpec(UniValue::VOBJ);
        rsSpec.pushKV("role",   "check-signature-verification");
        rsSpec.pushKV("inputs", rsInputs);
        basic.pushKV("Recipient-Sig", rsSpec);

        UniValue cSpec(UniValue::VOBJ);
        cSpec.pushKV("description", "Requires revealing preimage and recipient signature.");
        UniValue actions(UniValue::VARR);
        UniValue action1(UniValue::VOBJ); action1.pushKV("type","basic"); action1.pushKV("name","Hash-Check");     actions.push_back(action1);
        UniValue action2(UniValue::VOBJ); action2.pushKV("type","basic"); action2.pushKV("name","Recipient-Sig");  actions.push_back(action2);
        cSpec.pushKV("actions", actions);
        contract.pushKV(name, cSpec);

        doc.pushKV("basic",           basic);
        doc.pushKV("contract",        contract);
        doc.pushKV("active_contract", name);
    } else if (tabIndex == 3) { // Custom Builder
        buildContractFromCustom();
        return;
    }

    m_yamlHasError = false;
    m_yamlErrorText.clear();
    activeDoc = doc;
    updatePreviews();
}

QString SmartContractWidget::buildSmartContractStyleSheet(bool isLight) const
{
    const QString canvasBg    = isLight ? QStringLiteral("#F8FAFC") : QStringLiteral("#0B1120");
    const QString cardBg      = isLight ? QStringLiteral("#FFFFFF") : QStringLiteral("#131B2E");
    const QString subCardBg   = isLight ? QStringLiteral("#F8FAFC") : QStringLiteral("#0E1526");
    const QString cardBrd     = isLight ? QStringLiteral("#CBD5E1") : QStringLiteral("#253554");
    const QString divider     = isLight ? QStringLiteral("#E2E8F0") : QStringLiteral("#1E2C46");
    const QString titleCol    = isLight ? QStringLiteral("#1E293B") : QStringLiteral("#F1F5F9");
    const QString subCol      = isLight ? QStringLiteral("#475569") : QStringLiteral("#CBD5E1");
    const QString descCol     = isLight ? QStringLiteral("#64748B") : QStringLiteral("#94A3B8");
    const QString inputBg     = isLight ? QStringLiteral("#F8FAFC") : QStringLiteral("#0D1424");
    const QString inputBrd    = isLight ? QStringLiteral("#B6C5DC") : QStringLiteral("#33486E");
    const QString inputFocus  = isLight ? QStringLiteral("#FFFFFF") : QStringLiteral("#0A101E");
    const QString inputDisBg  = isLight ? QStringLiteral("#F1F5F9") : QStringLiteral("#111A2E");
    const QString inputDisFg  = isLight ? QStringLiteral("#94A3B8") : QStringLiteral("#64748B");

    const QString primBg      = isLight ? QStringLiteral("#3E5B94") : QStringLiteral("#4A6AB0");
    const QString primBrd     = isLight ? QStringLiteral("#354F82") : QStringLiteral("#5B7CC4");
    const QString primHov     = isLight ? QStringLiteral("#2F4A7D") : QStringLiteral("#5576B2");
    const QString primPres    = isLight ? QStringLiteral("#253B66") : QStringLiteral("#3E5B94");
    const QString segActBg    = isLight ? QStringLiteral("#EEF3FA") : QStringLiteral("#1B2B4A");
    const QString segActFg    = isLight ? QStringLiteral("#3E5B94") : QStringLiteral("#A8C1EE");
    const QString focCol      = isLight ? QStringLiteral("#3E5B94") : QStringLiteral("#93B4F0");
    const QString priFocCol   = isLight ? QStringLiteral("#1E3A8A") : QStringLiteral("#93B4F0");
    const QString secBg       = isLight ? QStringLiteral("#F1F5F9") : QStringLiteral("#1C2942");
    const QString secFg       = isLight ? QStringLiteral("#334155") : QStringLiteral("#CBD5E1");
    const QString secBrd      = isLight ? QStringLiteral("#CBD5E1") : QStringLiteral("#33486E");
    const QString secHov      = isLight ? QStringLiteral("#E2E8F0") : QStringLiteral("#26385A");
    const QString dropArrow   = isLight ? QStringLiteral("://ic-arrow-drop-down") : QStringLiteral("://ic-arrow-drop-down-white");

    QString css = QString(
        "QWidget#scPageRoot { background-color: %1; border: none; }"
        "QWidget#scPageRoot QTabWidget#tabWidgetMode { background-color: transparent; border: none; }"
        "QWidget#scPageRoot QTabWidget#tabWidgetMode::pane { border: none; background-color: transparent; padding-top: 0px; }"
        "QWidget#scPageRoot QWidget#tabBuild, QWidget#scPageRoot QWidget#tabRun { background-color: transparent; border: none; }"
        "QWidget#scPageRoot QFrame[scCard=\"true\"], QWidget#scPageRoot QFrame#left, QWidget#scPageRoot QFrame#right,"
        "  QWidget#scPageRoot QFrame#runLeft, QWidget#scPageRoot QFrame#runRight {"
        "  background-color: %2; border: 1px solid %3; border-radius: 10px;"
        "}"
        "QWidget#scPageRoot QFrame[scSubCard=\"true\"], QWidget#scPageRoot QFrame#frameUtxoInfo {"
        "  background-color: %4; border: 1px solid %5; border-radius: 8px;"
        "}"
        "QWidget#scPageRoot QFrame[scDivider=\"true\"] { background-color: %5; border: none; }"
        "QWidget#scPageRoot QFrame#frameTemplateBar { background-color: transparent; border: none; }"
    ).arg(canvasBg, cardBg, cardBrd, subCardBg, divider);

    css += QString(
        "QWidget#scPageRoot QLabel[scPageTitle=\"true\"] { color: %1; font-size: 22px; font-weight: 500; background: transparent; border: none; }"
        "QWidget#scPageRoot QLabel[scPageSubtitle=\"true\"] { color: %2; font-size: 13px; font-weight: 400; background: transparent; border: none; }"
        "QWidget#scPageRoot QLabel[scCardTitle=\"true\"] { color: %1; font-size: 14px; font-weight: 600; background: transparent; border: none; }"
        "QWidget#scPageRoot QLabel[scCardDesc=\"true\"] { color: %3; font-size: 12px; font-weight: 400; background: transparent; border: none; }"
        "QWidget#scPageRoot QLabel[scSectionLabel=\"true\"] { color: %2; font-size: 12px; font-weight: 700; background: transparent; border: none; }"
        "QWidget#scPageRoot QLabel[scFieldLabel=\"true\"], QWidget#scPageRoot QLabel#lblSelectTemplate {"
        "  color: %1; font-size: 13px; font-weight: 600; background: transparent; border: none;"
        "}"
        "QWidget#scPageRoot QLabel[scInfoKey=\"true\"] { color: %3; font-size: 12px; font-weight: 600; background: transparent; border: none; }"
        "QWidget#scPageRoot QLabel[scInfoVal=\"true\"] { color: %1; font-size: 12px; font-weight: 600; background: transparent; border: none; }"
        "QWidget#scPageRoot QLabel[scInfoMono=\"true\"] { color: %2; font-size: 11px; font-family: monospace; font-weight: 500; background: transparent; border: none; }"
    ).arg(titleCol, subCol, descCol);

    css += QString(
        "QWidget#scPageRoot QTabWidget#tabWidget { background-color: transparent; border: none; }"
        "QWidget#scPageRoot QTabWidget#tabWidget::pane {"
        "  background-color: %1; border: 1px solid %2; border-radius: 8px; border-top-left-radius: 0px; top: -1px;"
        "}"
        "QWidget#scPageRoot QTabWidget#tabWidget QTabBar::tab {"
        "  background-color: %3; color: %4; font-size: 12px; font-weight: 600;"
        "  padding: 7px 14px; min-width: 105px; border: 1px solid %5; border-bottom: 1px solid %2;"
        "  border-top-left-radius: 7px; border-top-right-radius: 7px; margin-right: 4px;"
        "}"
        "QWidget#scPageRoot QTabWidget#tabWidget QTabBar::tab:selected {"
        "  background-color: %1; color: %6; border: 1px solid %2; border-top: 2.5px solid #7695CD; border-bottom: 1px solid %1;"
        "}"
        "QWidget#scPageRoot QTabWidget#tabWidget QTabBar::tab:hover:!selected { background-color: %7; color: %6; }"
        "QWidget#scPageRoot QWidget#tabTimeLock, QWidget#scPageRoot QWidget#tabMultiSig,"
        "  QWidget#scPageRoot QWidget#tabHashLock, QWidget#scPageRoot QWidget#tabCustom {"
        "  background-color: %1; color: %6; border-radius: 8px;"
        "}"
    ).arg(subCardBg, cardBrd, secBg, descCol, divider, titleCol, secHov);

    css += QString(
        "QWidget#scPageRoot QLineEdit[scInput=\"true\"], QWidget#scPageRoot QPlainTextEdit[scInput=\"true\"] {"
        "  background-color: %1; color: %2; border: 1.5px solid %3; border-radius: 7px;"
        "  padding: 0px 10px; font-size: 13px; font-weight: 500;"
        "  selection-background-color: #5576B2; selection-color: #FFFFFF;"
        "}"
        "QWidget#scPageRoot QPlainTextEdit[scInput=\"true\"] { padding: 8px 10px; }"
        "QWidget#scPageRoot QLineEdit[scMono=\"true\"], QWidget#scPageRoot QPlainTextEdit[scMono=\"true\"] {"
        "  font-family: monospace; font-size: 12px;"
        "}"
        "QWidget#scPageRoot QLineEdit[scInput=\"true\"]:hover, QWidget#scPageRoot QPlainTextEdit[scInput=\"true\"]:hover,"
        "  QWidget#scPageRoot QDateTimeEdit[scInput=\"true\"]:hover { border-color: #7695CD; }"
        "QWidget#scPageRoot QLineEdit[scInput=\"true\"]:focus, QWidget#scPageRoot QPlainTextEdit[scInput=\"true\"]:focus,"
        "  QWidget#scPageRoot QDateTimeEdit[scInput=\"true\"]:focus {"
        "  border-color: #5576B2; background-color: %4;"
        "}"
        "QWidget#scPageRoot QLineEdit[scInput=\"true\"]:disabled, QWidget#scPageRoot QPlainTextEdit[scInput=\"true\"]:disabled,"
        "  QWidget#scPageRoot QDateTimeEdit[scInput=\"true\"]:disabled {"
        "  background-color: %5; color: %6; border: 1.5px solid %7;"
        "}"
        "QWidget#scPageRoot QLineEdit[scInputReadOnly=\"true\"] {"
        "  background-color: %5; color: %8; border: 1.5px solid %7; border-radius: 7px;"
        "  padding: 0px 10px; font-size: 12px; font-family: monospace; font-weight: 500;"
        "}"
    ).arg(inputBg, titleCol, inputBrd, inputFocus, inputDisBg, inputDisFg, divider, subCol);

    css += QString(
        "QWidget#scPageRoot QDateTimeEdit[scInput=\"true\"] {"
        "  background-color: %1; color: %2; border: 1.5px solid %3; border-radius: 7px;"
        "  padding: 0px 30px 0px 10px; font-size: 13px; font-weight: 500;"
        "  selection-background-color: #5576B2; selection-color: #FFFFFF;"
        "}"
        "QWidget#scPageRoot QDateTimeEdit[scInput=\"true\"]::drop-down {"
        "  subcontrol-origin: padding; subcontrol-position: top right; width: 28px;"
        "  border: none; border-left: 1px solid %4; border-top-right-radius: 6px; border-bottom-right-radius: 6px; background: transparent;"
        "}"
        "QWidget#scPageRoot QDateTimeEdit[scInput=\"true\"]::down-arrow {"
        "  image: url(\"%5\"); width: 14px; height: 14px;"
        "}"
        "QWidget#scPageRoot QCalendarWidget QWidget { background-color: %6; color: %2; alternate-background-color: %7; }"
        "QWidget#scPageRoot QCalendarWidget QAbstractItemView:enabled {"
        "  background-color: %6; color: %2; selection-background-color: #5576B2; selection-color: #FFFFFF;"
        "}"
    ).arg(inputBg, titleCol, inputBrd, divider, dropArrow, cardBg, secBg);

    css += QString(
        "QWidget#scPageRoot QComboBox#comboTemplates, QWidget#scPageRoot QComboBox#comboContracts, QWidget#scPageRoot QComboBox[scInput=\"true\"] {"
        "  combobox-popup: 0; background-color: %1; color: %2; border: 1.5px solid %3; border-radius: 7px;"
        "  padding: 0px 34px 0px 12px; font-size: 13px; font-weight: 500; text-align: left;"
        "}"
        "QWidget#scPageRoot QComboBox#comboTemplates:hover, QWidget#scPageRoot QComboBox#comboContracts:hover, QWidget#scPageRoot QComboBox[scInput=\"true\"]:hover {"
        "  border-color: #7695CD;"
        "}"
        "QWidget#scPageRoot QComboBox#comboTemplates:focus, QWidget#scPageRoot QComboBox#comboContracts:focus, QWidget#scPageRoot QComboBox[scInput=\"true\"]:focus,"
        "  QWidget#scPageRoot QComboBox#comboTemplates:on, QWidget#scPageRoot QComboBox#comboContracts:on, QWidget#scPageRoot QComboBox[scInput=\"true\"]:on {"
        "  border-color: #5576B2; background-color: %4;"
        "}"
        "QWidget#scPageRoot QComboBox#comboTemplates::drop-down, QWidget#scPageRoot QComboBox#comboContracts::drop-down, QWidget#scPageRoot QComboBox[scInput=\"true\"]::drop-down {"
        "  subcontrol-origin: padding; subcontrol-position: top right; width: 30px;"
        "  border: none; border-left: 1px solid %5; border-top-right-radius: 6px; border-bottom-right-radius: 6px; background: transparent;"
        "}"
        "QWidget#scPageRoot QComboBox#comboTemplates::down-arrow, QWidget#scPageRoot QComboBox#comboContracts::down-arrow, QWidget#scPageRoot QComboBox[scInput=\"true\"]::down-arrow {"
        "  image: url(\"%6\"); width: 16px; height: 16px;"
        "}"
        "QWidget#scPageRoot QComboBox QAbstractItemView {"
        "  background-color: %7; color: %2; border: 1px solid %8; border-radius: 6px; padding: 4px;"
        "  selection-background-color: #5576B2; selection-color: #FFFFFF; outline: none;"
        "}"
    ).arg(inputBg, titleCol, inputBrd, inputFocus, divider, dropArrow, cardBg, cardBrd);

    css += QString(
        "QWidget#scPageRoot QTextEdit#textEditYAML, QWidget#scPageRoot QTextEdit#textEditJSON, QWidget#scPageRoot QTextEdit#textEditDecompiled {"
        "  background-color: %1; color: %2; border: 1px solid %3; border-radius: 8px; padding: 8px;"
        "  font-family: monospace; font-size: 12px; selection-background-color: #5576B2; selection-color: #FFFFFF;"
        "}"
        "QWidget#scPageRoot QTextEdit#textEditYAML[scCodeEditable=\"true\"] {"
        "  background-color: %4; border: 1.5px solid #5576B2;"
        "}"
        "QWidget#scPageRoot QTreeWidget#treeCustomActions {"
        "  background-color: %1; color: %2; alternate-background-color: %5; border: 1px solid %3; border-radius: 8px; padding: 4px;"
        "  font-family: monospace; font-size: 12px;"
        "}"
        "QWidget#scPageRoot QTreeWidget#treeCustomActions QHeaderView::section {"
        "  background-color: %5; color: %6; border: none; border-bottom: 1px solid %7; padding: 6px 8px; font-weight: 600;"
        "}"
        "QWidget#scPageRoot QTreeWidget#treeCustomActions::item { color: %2; padding: 5px 6px; border-bottom: 1px solid %7; }"
        "QWidget#scPageRoot QTreeWidget#treeCustomActions::item:selected { background-color: #5576B2; color: #FFFFFF; }"
    ).arg(inputBg, titleCol, cardBrd, inputFocus, secBg, subCol, divider);

    css += QString(
        "QWidget#scPageRoot QCheckBox[scCheck=\"true\"] { color: %1; font-size: 13px; font-weight: 600; spacing: 8px; background: transparent; }"
        "QWidget#scPageRoot QCheckBox[scCheck=\"true\"]:focus { outline: none; color: %7; }"
        "QWidget#scPageRoot QCheckBox[scCheck=\"true\"]::indicator { width: 18px; height: 18px; }"
        "QWidget#scPageRoot QCheckBox[scCheck=\"true\"]::indicator:unchecked { image: url(\"://ic-check-box\"); }"
        "QWidget#scPageRoot QCheckBox[scCheck=\"true\"]::indicator:checked { image: url(\"://ic-check-active\"); }"
        "QWidget#scPageRoot QPushButton[scSegBtn=\"true\"] {"
        "  background-color: %2; color: %3; border: 1px solid %4; border-radius: 7px; padding: 0px 16px; font-size: 13px; font-weight: 600;"
        "}"
        "QWidget#scPageRoot QPushButton[scSegBtn=\"true\"]:hover { border-color: #7695CD; color: %1; }"
        "QWidget#scPageRoot QPushButton[scSegBtn=\"true\"]:checked { background-color: %5; color: %6; border: 1.5px solid #7695CD; }"
        "QWidget#scPageRoot QPushButton[scSegBtn=\"true\"]:focus { outline: none; border: 1.5px solid %7; }"
    ).arg(titleCol, secBg, subCol, secBrd, segActBg, segActFg, focCol);

    css += QString(
        "QWidget#scPageRoot QPushButton[scPrimary=\"true\"] {"
        "  background-color: %1; color: #FFFFFF; border: 1px solid %2; border-radius: 7px; padding: 0px 16px; font-size: 13px; font-weight: 600;"
        "}"
        "QWidget#scPageRoot QPushButton[scPrimary=\"true\"]:hover:enabled { background-color: %3; border-color: %3; }"
        "QWidget#scPageRoot QPushButton[scPrimary=\"true\"]:pressed:enabled { background-color: %4; border-color: %4; }"
        "QWidget#scPageRoot QPushButton[scPrimary=\"true\"]:disabled { background-color: %5; color: %6; border-color: %7; }"
        "QWidget#scPageRoot QPushButton[scPrimary=\"true\"]:focus { outline: none; border: 2px solid %8; }"
        "QWidget#scPageRoot QPushButton[scSecondary=\"true\"] {"
        "  background-color: %9; color: %10; border: 1px solid %7; border-radius: 7px; padding: 0px 14px; font-size: 13px; font-weight: 600;"
        "}"
    ).arg(primBg, primBrd, primHov, primPres, inputDisBg, inputDisFg, secBrd, priFocCol, secBg, secFg);

    css += QString(
        "QWidget#scPageRoot QPushButton[scSecondary=\"true\"]:hover { background-color: %1; border-color: #7695CD; color: %2; }"
        "QWidget#scPageRoot QPushButton[scSecondary=\"true\"]:disabled { background-color: %3; color: %4; border-color: %5; }"
        "QWidget#scPageRoot QPushButton[scSecondary=\"true\"]:focus { outline: none; border: 1.5px solid %9; }"
        "QWidget#scPageRoot QPushButton[scMiniBtn=\"true\"] {"
        "  background-color: %6; color: %7; border: 1px solid %8; border-radius: 6px; padding: 0px 10px; font-size: 11px; font-weight: 600;"
        "}"
        "QWidget#scPageRoot QPushButton[scMiniBtn=\"true\"]:hover { background-color: %1; border-color: #7695CD; color: %2; }"
        "QWidget#scPageRoot QPushButton[scMiniBtn=\"true\"]:focus { outline: none; border: 1.5px solid %9; }"
        "QWidget#scPageRoot QTabWidget#tabWidget QTabBar::tab:focus { outline: none; border: 1.5px solid %9; }"
        "QWidget#scPageRoot QTreeWidget#treeCustomActions:focus { outline: none; border: 1.5px solid %9; }"
    ).arg(secHov, titleCol, inputDisBg, inputDisFg, divider, secBg, subCol, secBrd, focCol);

    return css;
}

void SmartContractWidget::applyTheme(bool isLight)
{
    if (scRoot) {
        scRoot->setStyleSheet(buildSmartContractStyleSheet(isLight));
    }
    if (m_yamlHighlighter) {
        m_yamlHighlighter->setLightTheme(isLight);
    }
    const bool preserveDevYaml = (ui->cbDeveloperMode && ui->cbDeveloperMode->isChecked() && m_yamlDirty);
    if (!activeDoc.isNull() && !m_yamlHasError) {
        updatePreviewsForTheme(isLight, preserveDevYaml);
    }
    updateLockUntilPreview();
    updateOwnerPubKeyHints();
    updateContractSummary();
    if (ui->labelStatus && !ui->labelStatus->text().isEmpty()) {
        applyStatusLabelTone(ui->labelStatus, m_buildStatusTone, isLight);
    }
    if (ui->lblAutoSignStatus && !ui->lblAutoSignStatus->text().isEmpty()) {
        applyStatusLabelTone(ui->lblAutoSignStatus, m_autoSignTone, isLight);
    }
}

void SmartContractWidget::applyStatusLabelTone(QLabel* label, StatusTone tone, bool isLight)
{
    if (!label) return;
    QString fg;
    QString bg;
    QString brd;
    switch (tone) {
        case StatusTone::Success:
            fg  = isLight ? QStringLiteral("#3E5B94") : QStringLiteral("#93B4F0");
            bg  = isLight ? QStringLiteral("#EEF3FA") : QStringLiteral("#182642");
            brd = isLight ? QStringLiteral("#B8CBE8") : QStringLiteral("#2E4775");
            break;
        case StatusTone::Error:
            fg  = isLight ? QStringLiteral("#C94A3E") : QStringLiteral("#FCA5A5");
            bg  = isLight ? QStringLiteral("#FEF2F2") : QStringLiteral("#2A1519");
            brd = isLight ? QStringLiteral("#F5B5AE") : QStringLiteral("#6E2B2A");
            break;
        case StatusTone::Info:
            fg  = isLight ? QStringLiteral("#5576B2") : QStringLiteral("#7695CD");
            bg  = isLight ? QStringLiteral("#EEF3FA") : QStringLiteral("#182642");
            brd = isLight ? QStringLiteral("#B8CBE8") : QStringLiteral("#2E4775");
            break;
        case StatusTone::Warning:
            fg  = isLight ? QStringLiteral("#B85D3B") : QStringLiteral("#F5A584");
            bg  = isLight ? QStringLiteral("#FFF5F0") : QStringLiteral("#2B1B15");
            brd = isLight ? QStringLiteral("#F2C6B4") : QStringLiteral("#6E3F2C");
            break;
        case StatusTone::Neutral:
        default:
            fg  = isLight ? QStringLiteral("#475569") : QStringLiteral("#CBD5E1");
            bg  = isLight ? QStringLiteral("#F1F5F9") : QStringLiteral("#1C2942");
            brd = isLight ? QStringLiteral("#CBD5E1") : QStringLiteral("#33486E");
            break;
    }
    label->setStyleSheet(QStringLiteral(
        "QLabel { background-color: %1; color: %2; border: 1px solid %3; border-radius: 7px; padding: 7px 11px; font-size: 12px; font-weight: 600; }")
        .arg(bg, fg, brd));
    label->setVisible(true);
}

void SmartContractWidget::changeTheme(bool isLightTheme, QString& /*theme*/)
{
    applyTheme(isLightTheme);
    if (ui->comboContracts) {
        const int idx = ui->comboContracts->currentIndex();
        if (idx >= 0 && idx < m_utxos.size() && ui->textEditDecompiled) {
            const QString decOut = QString::fromStdString(m_utxos.at(idx).decompiled.write(2));
            const QString decHtml = highlightJsonHtml(decOut, isLightTheme);
            if (!decHtml.isEmpty()) ui->textEditDecompiled->setHtml(decHtml);
            else ui->textEditDecompiled->setPlainText(decOut);
        }
    }
}

void SmartContractWidget::updateRightPaneVisibility()
{
    const bool devOn = ui->cbDeveloperMode && ui->cbDeveloperMode->isChecked();
    if (ui->labelTitleRight) {
        ui->labelTitleRight->setText(devOn ? tr("Live Output Preview") : tr("Contract Summary"));
    }
    if (summaryCard) {
        summaryCard->setVisible(!devOn);
    }
    if (btnToggleTechSpec) {
        btnToggleTechSpec->setVisible(!devOn);
        btnToggleTechSpec->setText(m_showTechSpec
            ? tr("Hide Technical Spec (YAML / JSON)")
            : tr("Show Technical Spec (YAML / JSON)"));
    }
    if (devPreviewContainer) {
        devPreviewContainer->setVisible(devOn || m_showTechSpec);
    }
}

void SmartContractWidget::updateContractSummary()
{
    if (!lblSumContractVal || !lblSumAmountVal || !lblSumRuleVal || !lblSumKeysVal || !lblSumScriptVal) return;

    const int displayUnit = (walletModel && walletModel->getOptionsModel())
        ? walletModel->getOptionsModel()->getDisplayUnit()
        : BitcoinUnits::PIV;
    const QLocale loc;
    const bool devOn = ui->cbDeveloperMode && ui->cbDeveloperMode->isChecked();
    const int tabIdx = ui->tabWidget ? ui->tabWidget->currentIndex() : 0;

    QString typeName;
    if (devOn) {
        typeName = tr("Custom YAML Spec");
    } else if (tabIdx == 0) {
        typeName = tr("Time-Locked Deposit");
    } else if (tabIdx == 1) {
        typeName = tr("Multi-Signature");
    } else if (tabIdx == 2) {
        typeName = tr("Hash-Locked (HTLC)");
    } else {
        typeName = tr("Custom Builder");
    }
    const QString cName = activeName.empty() ? tr("Unnamed Contract") : QString::fromStdString(activeName);
    lblSumContractVal->setText(QString("%1 \xE2\x80\xA2 %2").arg(cName, typeName));

    CAmount fundAmt = 0;
    const QString rawAmt = ui->lineEditAmount ? ui->lineEditAmount->text().trimmed() : QString();
    const bool hasAmt = !rawAmt.isEmpty() && BitcoinUnits::parse(displayUnit, rawAmt, &fundAmt) && fundAmt > 0;
    QString spendStr = QStringLiteral("—");
    if (walletModel) {
        spendStr = BitcoinUnits::formatWithUnit(displayUnit, walletModel->getUnlockedBalance(), false, BitcoinUnits::separatorAlways);
    }
    lblSumAmountVal->setText(hasAmt
        ? tr("%1  (Spendable: %2)").arg(BitcoinUnits::formatWithUnit(displayUnit, fundAmt, false, BitcoinUnits::separatorAlways), spendStr)
        : tr("Not specified yet  (Spendable: %1)").arg(spendStr));

    const ParsedContractMeta meta = ParseContractMetadata(activeDoc);
    if (m_yamlHasError && devOn) {
        lblSumRuleVal->setText(tr("YAML syntax error — see status bar below."));
        lblSumKeysVal->setText(QStringLiteral("—"));
    } else if (!devOn && tabIdx == 0) {
        lblSumRuleVal->setText(lblLockUntilPreview && !lblLockUntilPreview->text().isEmpty()
            ? lblLockUntilPreview->text()
            : tr("Owner signature + TimeLock"));
        const QString pk = ui->lineEditPubkeyTimeLock ? ui->lineEditPubkeyTimeLock->text().trimmed() : QString();
        const QString hint = (lblTlPubkeyHint && !lblTlPubkeyHint->text().isEmpty()) ? lblTlPubkeyHint->text() : QString();
        lblSumKeysVal->setText(pk.isEmpty() ? tr("Missing Owner Public Key")
            : (!hint.isEmpty() ? hint : (pk.left(14) + "..." + pk.right(12))));
    } else if (!devOn && tabIdx == 1) {
        const int m = ui->lineEditMMultiSig ? ui->lineEditMMultiSig->text().toInt() : 0;
        const int n = ui->lineEditNMultiSig ? ui->lineEditNMultiSig->text().toInt() : 0;
        const int keyCount = ui->plainTextEditKeysMultiSig
            ? ui->plainTextEditKeysMultiSig->toPlainText().split('\n', QString::SkipEmptyParts).size()
            : 0;
        lblSumRuleVal->setText(tr("Requires %1 of %2 authorized signatures to spend").arg(m).arg(n));
        lblSumKeysVal->setText(tr("%1 public key(s) configured").arg(loc.toString(keyCount)));
    } else if (!devOn && tabIdx == 2) {
        const QString h160 = ui->lineEditHashHashLock ? ui->lineEditHashHashLock->text().trimmed() : QString();
        lblSumRuleVal->setText(tr("Requires secret preimage matching HASH160 (%1) + recipient signature")
            .arg(h160.length() > 16 ? (h160.left(8) + "..." + h160.right(8)) : h160));
        const QString pk = ui->lineEditPubkeyHashLock ? ui->lineEditPubkeyHashLock->text().trimmed() : QString();
        const QString hint = (lblHlPubkeyHint && !lblHlPubkeyHint->text().isEmpty()) ? lblHlPubkeyHint->text() : QString();
        lblSumKeysVal->setText(pk.isEmpty() ? tr("Missing Recipient Public Key")
            : (!hint.isEmpty() ? hint : (pk.left(14) + "..." + pk.right(12))));
    } else {
        QStringList parts;
        if (meta.isTimeLock) parts << tr("Time-Lock (%1)").arg(loc.toString(static_cast<qlonglong>(meta.lockTimeVal)));
        if (meta.isMultiSig) parts << tr("%1-of-%2 Multi-Sig").arg(meta.multiSigM).arg(meta.multiSigN);
        if (meta.isHashLock) parts << tr("Hash-Lock preimage");
        if (meta.isIfCond) parts << tr("IF/ELSE Branch");
        if (meta.isSigCheck) parts << tr("Owner Signature");
        if (parts.isEmpty() && !customActions.isNull() && customActions.size() > 0) {
            parts << tr("%1 custom step(s)").arg(loc.toString(static_cast<int>(customActions.size())));
        }
        lblSumRuleVal->setText(parts.isEmpty() ? tr("Custom MESCAL condition(s)") : parts.join(QStringLiteral(" + ")));
        if (!meta.timeLockOwnerPubkey.empty()) {
            const QString pk = QString::fromStdString(meta.timeLockOwnerPubkey);
            lblSumKeysVal->setText(pk.left(14) + "..." + pk.right(12));
        } else if (!meta.multiSigPubkeys.empty()) {
            lblSumKeysVal->setText(tr("%1 signer key(s)").arg(loc.toString((int)meta.multiSigPubkeys.size())));
        } else {
            lblSumKeysVal->setText(tr("Defined in YAML spec"));
        }
    }

    if (!m_lastCompileErr.isEmpty() || m_lastCompiledBytes <= 0) {
        lblSumScriptVal->setText(m_lastCompileErr.isEmpty() ? tr("Invalid / incomplete contract") : tr("Error: %1").arg(m_lastCompileErr));
    } else {
        const CAmount estFee = std::max<CAmount>(10000, ::minRelayTxFee.GetFee(190 + m_lastCompiledBytes + 34));
        lblSumScriptVal->setText(tr("%1 bytes MESCAL script \xE2\x80\xA2 Est. tx fee ~%2")
            .arg(loc.toString(m_lastCompiledBytes),
                 BitcoinUnits::formatWithUnit(displayUnit, estFee, false, BitcoinUnits::separatorAlways)));
    }
}

void SmartContractWidget::updatePreviews()
{
    updatePreviewsForTheme(isLightTheme(), false);
}

void SmartContractWidget::updatePreviewsForTheme(bool isLight, bool preserveYaml)
{
    const QString jsonOut = QString::fromStdString(activeDoc.write(2));
    const QString jsonHtml = highlightJsonHtml(jsonOut, isLight);
    if (!jsonHtml.isEmpty()) ui->textEditJSON->setHtml(jsonHtml);
    else ui->textEditJSON->setPlainText(jsonOut);
    if (!preserveYaml) {
        QSignalBlocker blocker(ui->textEditYAML);
        const QString yamlOut = QString::fromStdString(UniValueToYAML(activeDoc, 0));
        ui->textEditYAML->setPlainText(yamlOut);
        if (m_yamlHighlighter) {
            m_yamlHighlighter->rehighlight();
        }
        m_yamlBaseline = yamlOut;
        m_yamlDirty = false;
    }

    if (!ui->cbDeveloperMode->isChecked()) {
        m_yamlHasError = false;
        m_yamlErrorText.clear();
    }

    std::string compileErr;
    CScript compiled = CMescal::Compile(activeDoc.write(), compileErr);
    m_lastCompileErr = QString::fromStdString(compileErr);
    m_lastCompiledBytes = compileErr.empty() ? static_cast<int>(compiled.size()) : 0;
    if (!compileErr.empty()) {
        m_buildStatusTone = StatusTone::Error;
        applyStatusLabelTone(ui->labelStatus, m_buildStatusTone, isLight);
        ui->labelStatus->setText(tr("✗ Status: Compilation Error — %1").arg(QString::fromStdString(compileErr)));
        if (ui->btnPublish) ui->btnPublish->setEnabled(false);
    } else {
        m_buildStatusTone = StatusTone::Success;
        applyStatusLabelTone(ui->labelStatus, m_buildStatusTone, isLight);
        ui->labelStatus->setText(tr("✓ Status: Valid MESCAL Contract (%1 bytes)").arg(compiled.size()));
        if (ui->btnPublish) ui->btnPublish->setEnabled(!m_yamlHasError);
    }
    updateContractSummary();
}

void SmartContractWidget::onSaveClicked()
{
    if (activeName.empty()) {
        showThemedNotice(this, tr("Error"), tr("Contract name is empty."), true);
        return;
    }
    if (ui->cbDeveloperMode && ui->cbDeveloperMode->isChecked() && m_yamlHasError) {
        showThemedNotice(this, tr("YAML Parse Error"),
            tr("Fix the YAML syntax errors before exporting:\n%1").arg(m_yamlErrorText), true);
        return;
    }

    QString safeName = QString::fromStdString(activeName).trimmed();
    for (int i = 0; i < safeName.size(); ++i) {
        QChar c = safeName[i];
        if (c.unicode() < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            safeName[i] = '_';
        }
    }
    if (safeName.length() > 128) safeName = safeName.left(128);
    if (safeName.isEmpty()) safeName = QStringLiteral("contract");

    const QString yamlText = QString::fromStdString(UniValueToYAML(activeDoc, 0));
    const QString jsonText = QString::fromStdString(activeDoc.write(2));

    auto writeTextFile = [&](const QString& filePath, const QString& contents, QString& errOut) -> bool {
        QFile f(filePath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            errOut = f.errorString();
            return false;
        }
        QTextStream out(&f);
        out << contents;
        out.flush();
        f.close();
        if (f.error() != QFile::NoError) {
            errOut = f.errorString();
            return false;
        }
        return true;
    };

    const bool light = isLightTheme();
    QMenu menu(this);
    menu.setStyleSheet(QString(
        "QMenu { background-color: %1; color: %2; border: 1px solid %3; border-radius: 8px; padding: 6px; font-size: 12px; }"
        "QMenu::item { padding: 7px 14px; border-radius: 5px; }"
        "QMenu::item:selected { background-color: #5576B2; color: #FFFFFF; }")
        .arg(light ? "#FFFFFF" : "#111A2E",
             light ? "#0F172A" : "#E2E8F0",
             light ? "#CBD5E1" : "#263859"));

    QAction* actYaml = menu.addAction(tr("Save YAML Spec (.yaml)"));
    QAction* actJson = menu.addAction(tr("Save JSON Spec (.json)"));
    QAction* actBoth = menu.addAction(tr("Export Both (.yaml && .json) to Folder..."));

    QAction* chosen = menu.exec(ui->btnSave->mapToGlobal(QPoint(0, ui->btnSave->height() + 4)));
    if (!chosen) return;

    if (chosen == actYaml) {
        QString path = QFileDialog::getSaveFileName(this, tr("Save YAML Contract Spec"), safeName + ".yaml", tr("YAML Files (*.yaml *.yml)"));
        if (path.isEmpty()) return;
        QString err;
        if (!writeTextFile(path, yamlText, err)) {
            showThemedNotice(this, tr("Save Failed"), tr("Could not write YAML file:\n%1\n(%2)").arg(path, err), true);
            return;
        }
        inform(tr("YAML spec saved:\n%1").arg(path));
    } else if (chosen == actJson) {
        QString path = QFileDialog::getSaveFileName(this, tr("Save JSON Contract Spec"), safeName + ".json", tr("JSON Files (*.json)"));
        if (path.isEmpty()) return;
        QString err;
        if (!writeTextFile(path, jsonText, err)) {
            showThemedNotice(this, tr("Save Failed"), tr("Could not write JSON file:\n%1\n(%2)").arg(path, err), true);
            return;
        }
        inform(tr("JSON spec saved:\n%1").arg(path));
    } else if (chosen == actBoth) {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Select Folder for YAML & JSON Export"));
        if (dir.isEmpty()) return;
        const QString yamlPath = QDir(dir).filePath(safeName + ".yaml");
        const QString jsonPath = QDir(dir).filePath(safeName + ".json");
        if (QFile::exists(yamlPath) || QFile::exists(jsonPath)) {
            if (!ask(tr("Overwrite Existing Files?"),
                     tr("One or both contract spec files already exist in:\n%1\n\nOverwrite %2.yaml and %2.json?")
                         .arg(dir, safeName))) {
                return;
            }
        }
        QString errYaml, errJson;
        if (!writeTextFile(yamlPath, yamlText, errYaml)) {
            showThemedNotice(this, tr("Save Failed"), tr("Could not write YAML file:\n%1\n(%2)").arg(yamlPath, errYaml), true);
            return;
        }
        if (!writeTextFile(jsonPath, jsonText, errJson)) {
            showThemedNotice(this, tr("Save Failed"), tr("Could not write JSON file:\n%1\n(%2)").arg(jsonPath, errJson), true);
            return;
        }
        inform(tr("Contract specs saved:\n%1\n%2").arg(yamlPath, jsonPath));
    }
}

void SmartContractWidget::onPublishClicked()
{
    if (!walletModel) {
        inform(tr("Wallet model not loaded."));
        return;
    }

    if (ui->cbDeveloperMode && ui->cbDeveloperMode->isChecked() && m_yamlHasError) {
        showThemedNotice(this, tr("YAML Parse Error"),
            tr("The Developer Mode YAML specification currently has syntax errors:\n%1\n\nFix the YAML errors before publishing (publishing stale state is blocked).")
                .arg(m_yamlErrorText),
            true);
        return;
    }

    // 1. Compile
    std::string jsonStr = activeDoc.write();
    std::string errorStr;
    CScript script = CMescal::Compile(jsonStr, errorStr);
    if (!errorStr.empty()) {
        showThemedNotice(this, tr("Compilation Error"),
            tr("MESCAL compilation failed:\n%1").arg(QString::fromStdString(errorStr)), true);
        return;
    }

    // 2. Safety Guard: Block publishing if unedited template sample public keys or hashes remain in the contract!
    static const char* const kSamplePlaceholderPubKeys[] = {
        "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f",
        "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a",
        "02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234",
        "02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1236",
        "03ab89ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1235",
        "03de76ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1237",
        "02ef54ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1238",
        "03fa32ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1239",
        "03bb9f1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234a1"
    };
    for (const char* sampleKey : kSamplePlaceholderPubKeys) {
        if (jsonStr.find(sampleKey) != std::string::npos) {
            showThemedNotice(
                this,
                tr("Unedited Template Public Key Detected"),
                tr("This contract still contains an example placeholder public key:\n%1\n\n"
                   "Nobody holds the private key for template example keys — publishing with it would lock funds permanently.\n\n"
                   "Click 'Wallet PubKey ▾' above or paste your real participant public keys before publishing.")
                    .arg(QString::fromLatin1(sampleKey)),
                true);
            return;
        }
    }
    static const char* const kSamplePlaceholderHashes[] = {
        "b5a9c9f285d893ce71ab9de8f5c09d765ee982ba",
        "a5c9f285d893ce71ab9de8f5c09d765ee982ba34"
    };
    for (const char* sampleHash : kSamplePlaceholderHashes) {
        if (jsonStr.find(sampleHash) != std::string::npos) {
            showThemedNotice(
                this,
                tr("Unedited Template Hash160 Detected"),
                tr("This contract still contains an example placeholder Hash160:\n%1\n\n"
                   "Nobody knows the secret preimage for template example hashes — publishing with it would lock funds permanently.\n\n"
                   "Replace the Hash160 with a SHA-256 + RIPEMD-160 hash of your own secret preimage before publishing.")
                    .arg(QString::fromLatin1(sampleHash)),
                true);
            return;
        }
    }

    // 2b. Safety Guard: Validate Lock-Until bounds and warn on expired or multi-year locks
    {
        std::vector<UniValue> lockInputs;
        CollectLockTimeInputs(activeDoc, lockInputs);
        if (!lockInputs.empty()) {
            int curHeight = 0;
            {
                TRY_LOCK(cs_main, lockMain);
                if (lockMain) curHeight = chainActive.Height();
            }
            const int64_t nowTs = std::max<int64_t>(QDateTime::currentSecsSinceEpoch(), GetAdjustedTime());
            const qint64 targetSpacing = std::max<qint64>(1, Params().GetConsensus().nTargetSpacing);
            QStringList lockWarnings;

            for (const UniValue& lv : lockInputs) {
                if (!lv.isNum()) {
                    showThemedNotice(this, tr("Invalid Lock-Until Value"),
                        tr("Lock-Until value must be an integer block height (< 500,000,000) or UNIX timestamp (>= 500,000,000)."), true);
                    return;
                }
                int64_t val = 0;
                try {
                    val = lv.get_int64();
                } catch (...) {
                    showThemedNotice(this, tr("Invalid Lock-Until Value"),
                        tr("Lock-Until value must be an integer block height or UNIX timestamp."), true);
                    return;
                }
                if (val < 0 || val > 0xFFFFFFFFLL) {
                    showThemedNotice(this, tr("Unspendable Lock-Until Value"),
                        tr("Lock-Until value %1 is outside the 32-bit nLockTime range (0 .. 4,294,967,295) and would permanently burn funds.").arg(val), true);
                    return;
                }
                if (val >= LOCKTIME_THRESHOLD) {
                    const QString utcStr = QDateTime::fromSecsSinceEpoch(val, Qt::UTC).toString(QStringLiteral("yyyy-MM-dd HH:mm 'UTC'"));
                    if (val <= nowTs) {
                        lockWarnings << tr("• Timestamp %1 (%2) has ALREADY PASSED — funds can be spent immediately.").arg(val).arg(utcStr);
                    } else if ((val - nowTs) > 5LL * 365 * 86400) {
                        lockWarnings << tr("• Timestamp %1 (%2) locks funds for %3!").arg(val).arg(utcStr, FormatLockDurationApprox(val - nowTs));
                    }
                } else if (val > 0) {
                    if (curHeight > 0 && val <= curHeight) {
                        lockWarnings << tr("• Block height #%1 has ALREADY PASSED (current height #%2) — funds can be spent immediately.")
                            .arg(QLocale().toString(static_cast<qlonglong>(val)))
                            .arg(QLocale().toString(curHeight));
                    } else if (curHeight > 0 && (val - curHeight) * targetSpacing > 5LL * 365 * 86400) {
                        const qint64 estSecs = (val - curHeight) * targetSpacing;
                        lockWarnings << tr("• Block height #%1 is +%2 blocks ahead (%3 at %4s/block)! Verify you didn't enter a calendar date like YYYYMMDD.")
                            .arg(QLocale().toString(static_cast<qlonglong>(val)))
                            .arg(QLocale().toString(static_cast<qlonglong>(val - curHeight)))
                            .arg(FormatLockDurationApprox(estSecs))
                            .arg(targetSpacing);
                    }
                }
            }
            if (!lockWarnings.isEmpty()) {
                if (!showThemedConfirm(
                        this,
                        tr("Verify Time-Lock Horizon"),
                        tr("Please review these time-lock warnings before locking funds:\n\n%1\n\nAre you sure you want to proceed?")
                            .arg(lockWarnings.join("\n")),
                        tr("PROCEED"),
                        tr("CANCEL"),
                        true)) {
                    return;
                }
            }
        }
    }

    // 3. Validate Amount using exact satoshi integer parser (no floating-point rounding)
    CAmount amount = 0;
    const QString rawAmountStr = ui->lineEditAmount->text().trimmed();
    if (rawAmountStr.isEmpty() || !BitcoinUnits::parse(BitcoinUnits::PIV, rawAmountStr, &amount) || amount <= 0) {
        showThemedNotice(this, tr("Invalid Amount"),
            tr("Please enter a valid positive amount of KRISTA to fund this contract."), true);
        ui->lineEditAmount->setFocus();
        return;
    }

    // 4. Unlock Wallet
    WalletModel::UnlockContext ctx(walletModel->requestUnlock());
    if (!ctx.isValid()) {
        inform(tr("Wallet unlock cancelled"));
        return;
    }

    if (!pwalletMain) {
        inform(tr("Wallet database not initialized."));
        return;
    }

    std::vector<CRecipient> vecSend;
    vecSend.push_back(CRecipient{script, amount, false});

    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired = 0;
    int nChangePosInOut = -1;
    std::string strFailReason;
    CCoinControl coinControl;

    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        bool fCreated = pwalletMain->CreateTransaction(vecSend,
                                                      wtx,
                                                      reservekey,
                                                      nFeeRequired,
                                                      nChangePosInOut,
                                                      strFailReason,
                                                      &coinControl,
                                                      ALL_COINS,
                                                      true,
                                                      0);
        if (!fCreated) {
            inform(tr("Transaction creation failed: %1").arg(QString::fromStdString(strFailReason)));
            return;
        }
    }

    // 5. Explicit pre-broadcast confirmation dialog showing contract name, script size, amount & exact fee
    const int dispUnit = (walletModel->getOptionsModel()) ? walletModel->getOptionsModel()->getDisplayUnit() : BitcoinUnits::PIV;
    showHideOp(true);
    DefaultDialog* confirmDlg = new DefaultDialog(window);
    confirmDlg->setText(
        tr("Confirm Smart Contract Deployment"),
        tr("Deploy MESCAL smart contract '%1' to the KristaTech blockchain?\n\n"
           "• Compiled Script Size: %2 bytes\n"
           "• Contract Locked Amount: %3\n"
           "• Network Fee: %4\n\n"
           "Make sure you hold the private keys or preimages required to satisfy the contract's spending conditions.")
            .arg(QString::fromStdString(activeName.empty() ? "Contract" : activeName))
            .arg(script.size())
            .arg(BitcoinUnits::formatWithUnit(dispUnit, amount, false, BitcoinUnits::separatorAlways))
            .arg(BitcoinUnits::formatWithUnit(dispUnit, nFeeRequired, false, BitcoinUnits::separatorAlways)),
        tr("DEPLOY CONTRACT"),
        tr("CANCEL")
    );
    confirmDlg->adjustSize();
    const bool confirmedDeploy = openDialogWithOpaqueBackgroundY(confirmDlg, window, 3, 5);
    confirmDlg->deleteLater();
    if (!confirmedDeploy) {
        reservekey.ReturnKey();
        return;
    }

    CWallet::CommitResult res = pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get());
    if (res.status != CWallet::CommitStatus::OK) {
        showThemedNotice(this, tr("Commit Failed"),
            tr("Transaction commit failed:\n%1").arg(QString::fromStdString(res.ToString())), true);
        return;
    }

    // 6. Notify UI
    walletModel->emitBalanceChanged();

    std::string txid = res.hashTx.GetHex();
    m_buildStatusTone = StatusTone::Success;
    applyStatusLabelTone(ui->labelStatus, m_buildStatusTone, isLightTheme());
    ui->labelStatus->setText(tr("Status: Published! TxID: %1").arg(QString::fromStdString(txid)));
    inform(tr("Contract published successfully!\nTxID: %1").arg(QString::fromStdString(txid)));
}

void SmartContractWidget::onTemplateSelected(int index)
{
    if (index == 0) {
        // Custom / Blank
        return;
    }

    struct SyncGuard {
        bool& flag;
        explicit SyncGuard(bool& f) : flag(f) { flag = true; }
        ~SyncGuard() { flag = false; }
    } syncGuard(m_syncingTemplateOrTab);

    QSignalBlocker blockTabs(ui->tabWidget);

    const int64_t nowTs = std::max<int64_t>(QDateTime::currentSecsSinceEpoch(), GetAdjustedTime());
    const int64_t lock7dTs = nowTs + 7LL * 86400LL;
    const int64_t lock10dTs = nowTs + 10LL * 86400LL;
    const int64_t lock14dTs = nowTs + 14LL * 86400LL;
    const int64_t lock21dTs = nowTs + 21LL * 86400LL;
    const int64_t lock30dTs = nowTs + 30LL * 86400LL;
    const int64_t lock60dTs = nowTs + 60LL * 86400LL;
    const int64_t lock90dTs = nowTs + 90LL * 86400LL;

    if (index == 1) { // Time-Locked Deposit
        ui->tabWidget->setCurrentIndex(0);
        ui->lineEditNameTimeLock->setText("TimeLockedDeposit");
        if (comboLockMode) {
            QSignalBlocker bMode(comboLockMode);
            comboLockMode->setCurrentIndex(0); // Timestamp (UTC)
        }
        if (dateTimeEditLockTime) {
            QSignalBlocker bDt(dateTimeEditLockTime);
            dateTimeEditLockTime->setDateTime(QDateTime::fromSecsSinceEpoch(lock30dTs, Qt::UTC));
        }
        {
            QSignalBlocker bEd(ui->lineEditExpiryTimeLock);
            ui->lineEditExpiryTimeLock->setText(QString::number(lock30dTs));
        }
        syncLockModeUi();
        updateOwnerPubKeyHints();
        generateContract();
    }
    else if (index == 2) { // Escrow Multi-Signature (2-of-3)
        ui->tabWidget->setCurrentIndex(1);
        ui->lineEditNameMultiSig->setText("Escrow2of3");
        ui->lineEditNMultiSig->setText("3");
        ui->lineEditMMultiSig->setText("2");
        QStringList sampleKeys;
        sampleKeys << "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f"
                   << "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"
                   << "02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234";
        ui->plainTextEditKeysMultiSig->setPlainText(sampleKeys.join("\n"));
        generateContract();
    }
    else if (index == 3) { // Hash-Locked Claim
        ui->tabWidget->setCurrentIndex(2);
        ui->lineEditNameHashLock->setText("HashLockedClaim");
        ui->lineEditHashHashLock->setText("b5a9c9f285d893ce71ab9de8f5c09d765ee982ba");
        generateContract();
    }
    else {
        // Custom Builder advanced templates
        ui->tabWidget->setCurrentIndex(3);
        
        customActions.clear();
        customActions.setArray();
        
        if (index == 4) { // Dead Man's Switch (Inheritance)
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "check-signature-verification");
            UniValue exprInputs(UniValue::VARR);
            UniValue exprInp(UniValue::VOBJ);
            exprInp.pushKV("name", "Pubkey");
            exprInp.pushKV("type", "pubkey");
            exprInp.pushKV("value", "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f"); // Heir
            exprInputs.push_back(exprInp);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VOBJ);
            trueAct.pushKV("role", "lock-time");
            UniValue trueInputs(UniValue::VARR);
            UniValue trueInp(UniValue::VOBJ);
            trueInp.pushKV("name", "Lock-Until");
            trueInp.pushKV("type", "timestamp-or-block-height");
            trueInp.pushKV("value", lock30dTs); // Expiry
            trueInputs.push_back(trueInp);
            trueAct.pushKV("inputs", trueInputs);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VOBJ);
            falseAct.pushKV("role", "check-signature-verification");
            UniValue falseInputs(UniValue::VARR);
            UniValue falseInp(UniValue::VOBJ);
            falseInp.pushKV("name", "Pubkey");
            falseInp.pushKV("type", "pubkey");
            falseInp.pushKV("value", "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"); // Owner
            falseInputs.push_back(falseInp);
            falseAct.pushKV("inputs", falseInputs);
            condNode.pushKV("false_action", falseAct);

            customActions.push_back(condNode);
        }
        else if (index == 5) { // Dual-Signature Escrow with Mediator (2-of-3)
            UniValue act(UniValue::VOBJ);
            act.pushKV("role", "multi-signature");
            UniValue inputs(UniValue::VARR);

            UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", 2); inputs.push_back(inpM);
            UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", 3); inputs.push_back(inpN);

            UniValue keysArray(UniValue::VARR);
            keysArray.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f"); // Buyer
            keysArray.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"); // Seller
            keysArray.push_back("02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234"); // Mediator
            UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); inputs.push_back(inpSigs);

            act.pushKV("inputs", inputs);
            customActions.push_back(act);
        }
        else if (index == 6) { // 2-Factor Authentication (2FA) Wallet
            UniValue act(UniValue::VOBJ);
            act.pushKV("role", "multi-signature");
            UniValue inputs(UniValue::VARR);

            UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", 2); inputs.push_back(inpM);
            UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", 2); inputs.push_back(inpN);

            UniValue keysArray(UniValue::VARR);
            keysArray.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f"); // Primary
            keysArray.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"); // Backup
            UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); inputs.push_back(inpSigs);

            act.pushKV("inputs", inputs);
            customActions.push_back(act);
        }
        else if (index == 7) { // Hash Time-Locked Swap (HTLC)
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "hash160");
            expr.pushKV("mode", "htlc-equal");
            UniValue exprInputs(UniValue::VARR);
            UniValue exprInp(UniValue::VOBJ);
            exprInp.pushKV("name", "hash160");
            exprInp.pushKV("type", "hash160");
            exprInp.pushKV("value", "b5a9c9f285d893ce71ab9de8f5c09d765ee982ba");
            exprInputs.push_back(exprInp);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VOBJ);
            trueAct.pushKV("role", "check-signature-verification");
            UniValue trueInputs(UniValue::VARR);
            UniValue trueInp(UniValue::VOBJ);
            trueInp.pushKV("name", "Pubkey");
            trueInp.pushKV("type", "pubkey");
            trueInp.pushKV("value", "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f"); // Recipient
            trueInputs.push_back(trueInp);
            trueAct.pushKV("inputs", trueInputs);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VOBJ);
            falseAct.pushKV("role", "check-signature-verification");
            UniValue falseInputs(UniValue::VARR);
            UniValue falseInp(UniValue::VOBJ);
            falseInp.pushKV("name", "Pubkey");
            falseInp.pushKV("type", "pubkey");
            falseInp.pushKV("value", "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"); // Sender
            falseInputs.push_back(falseInp);
            falseAct.pushKV("inputs", falseInputs);
            condNode.pushKV("false_action", falseAct);

            customActions.push_back(condNode);

            // HTLC also has a lock time component for timeout. In customActions, we can append a step for Timeout Check!
            UniValue timeNode(UniValue::VOBJ);
            timeNode.pushKV("role", "lock-time");
            UniValue timeInputs(UniValue::VARR);
            UniValue timeInp(UniValue::VOBJ);
            timeInp.pushKV("name", "Lock-Until");
            timeInp.pushKV("type", "timestamp-or-block-height");
            timeInp.pushKV("value", lock30dTs); // Expiry
            timeInputs.push_back(timeInp);
            timeNode.pushKV("inputs", timeInputs);
            
            customActions.push_back(timeNode);
        }
        else if (index == 8) { // Multi-Path Security Recovery
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "multi-signature");
            UniValue exprInputs(UniValue::VARR);
            UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", 2); exprInputs.push_back(inpM);
            UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", 3); exprInputs.push_back(inpN);
            UniValue keysArray(UniValue::VARR);
            keysArray.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"); // Recovery Key 1
            keysArray.push_back("02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234"); // Recovery Key 2
            keysArray.push_back("03ab89ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1235"); // Recovery Key 3
            UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); exprInputs.push_back(inpSigs);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VOBJ);
            trueAct.pushKV("role", "lock-time");
            UniValue trueInputs(UniValue::VARR);
            UniValue trueInp(UniValue::VOBJ);
            trueInp.pushKV("name", "Lock-Until");
            trueInp.pushKV("type", "timestamp-or-block-height");
            trueInp.pushKV("value", lock30dTs); // Delay
            trueInputs.push_back(trueInp);
            trueAct.pushKV("inputs", trueInputs);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VOBJ);
            falseAct.pushKV("role", "check-signature-verification");
            UniValue falseInputs(UniValue::VARR);
            UniValue falseInp(UniValue::VOBJ);
            falseInp.pushKV("name", "Pubkey");
            falseInp.pushKV("type", "pubkey");
            falseInp.pushKV("value", "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f"); // Owner
            falseInputs.push_back(falseInp);
            falseAct.pushKV("inputs", falseInputs);
            condNode.pushKV("false_action", falseAct);

            customActions.push_back(condNode);
        }
        else if (index == 9) { // Tokenized Asset (Escrow & Compliance)
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            // Expression: Seller Signature verification (Owner/Seller key)
            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "check-signature-verification");
            UniValue exprInputs(UniValue::VARR);
            UniValue exprInp(UniValue::VOBJ);
            exprInp.pushKV("name", "Pubkey");
            exprInp.pushKV("type", "pubkey");
            exprInp.pushKV("value", "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f"); // Seller
            exprInputs.push_back(exprInp);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            // True Action: lock-time (Dispute Timeout)
            UniValue trueAct(UniValue::VOBJ);
            trueAct.pushKV("role", "lock-time");
            UniValue trueInputs(UniValue::VARR);
            UniValue trueInp(UniValue::VOBJ);
            trueInp.pushKV("name", "Lock-Until");
            trueInp.pushKV("type", "timestamp-or-block-height");
            trueInp.pushKV("value", lock30dTs); // Expiry
            trueInputs.push_back(trueInp);
            trueAct.pushKV("inputs", trueInputs);
            condNode.pushKV("true_action", trueAct);

            // False Action: 2-of-3 multi-signature (Seller, Buyer, Mediator)
            UniValue falseAct(UniValue::VOBJ);
            falseAct.pushKV("role", "multi-signature");
            UniValue falseInputs(UniValue::VARR);
            UniValue falseM(UniValue::VOBJ); falseM.pushKV("name", "m"); falseM.pushKV("type", "number"); falseM.pushKV("value", 2); falseInputs.push_back(falseM);
            UniValue falseN(UniValue::VOBJ); falseN.pushKV("name", "n"); falseN.pushKV("type", "number"); falseN.pushKV("value", 3); falseInputs.push_back(falseN);

            UniValue keysArray(UniValue::VARR);
            keysArray.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f"); // Seller
            keysArray.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"); // Buyer
            keysArray.push_back("02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234"); // Mediator
            UniValue falseSigs(UniValue::VOBJ); falseSigs.pushKV("name", "Signatures"); falseSigs.pushKV("type", "array"); falseSigs.pushKV("value", keysArray); falseInputs.push_back(falseSigs);
            falseAct.pushKV("inputs", falseInputs);
            condNode.pushKV("false_action", falseAct);

            customActions.push_back(condNode);
        }
        else if (index == 10) { // Miner Registration (Coin-Lock)
            UniValue act(UniValue::VOBJ);
            act.pushKV("role", "coin-lock-miner");
            UniValue inputs(UniValue::VARR);

            CPubKey pubKey;
            std::string pubkeyHex = "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f";
            std::string pubkeyHashHex = "b5a9c9f285d893ce71ab9de8f5c09d765ee982ba";
            if (pwalletMain && pwalletMain->GetKeyFromPool(pubKey)) {
                pubkeyHex = HexStr(pubKey.begin(), pubKey.end());
                pubkeyHashHex = HexStr(pubKey.GetID());
            }

            int64_t minerLockHeight = 2900;
            {
                LOCK(cs_main);
                minerLockHeight = std::max<int64_t>(0, chainActive.Height()) + 2900;
            }

            UniValue inpPk(UniValue::VOBJ);
            inpPk.pushKV("name", "Pubkey");
            inpPk.pushKV("type", "pubkey");
            inpPk.pushKV("value", pubkeyHex);
            inputs.push_back(inpPk);

            UniValue inpLt(UniValue::VOBJ);
            inpLt.pushKV("name", "Lock-Until");
            inpLt.pushKV("type", "timestamp-or-block-height");
            inpLt.pushKV("value", minerLockHeight); // Block height (~24h CLTV lock)
            inputs.push_back(inpLt);

            UniValue inpPkh(UniValue::VOBJ);
            inpPkh.pushKV("name", "PubkeyHash");
            inpPkh.pushKV("type", "pubkeyhash");
            inpPkh.pushKV("value", pubkeyHashHex);
            inputs.push_back(inpPkh);

            act.pushKV("inputs", inputs);
            customActions.push_back(act);
        }
        else if (index == 11) { // Miner Registration (PoW-Lock)
            UniValue act(UniValue::VOBJ);
            act.pushKV("role", "pow-miner");
            UniValue inputs(UniValue::VARR);

            CPubKey pubKey;
            std::string pubkeyHex = "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f";
            std::string pubkeyHashHex = "b5a9c9f285d893ce71ab9de8f5c09d765ee982ba";
            if (pwalletMain && pwalletMain->GetKeyFromPool(pubKey)) {
                pubkeyHex = HexStr(pubKey.begin(), pubKey.end());
                pubkeyHashHex = HexStr(pubKey.GetID());
            }

            std::string tipHashHex = "0000000000000000000000000000000000000000000000000000000000000000";
            int64_t minerLockHeight = 2900;
            {
                LOCK(cs_main);
                if (chainActive.Tip()) {
                    tipHashHex = chainActive.Tip()->GetBlockHash().GetHex();
                }
                minerLockHeight = std::max<int64_t>(0, chainActive.Height()) + 2900;
            }

            UniValue inpNonce(UniValue::VOBJ);
            inpNonce.pushKV("name", "Nonce");
            inpNonce.pushKV("type", "nonce");
            inpNonce.pushKV("value", "00000000"); // 4-byte uint32 LE hex placeholder
            inputs.push_back(inpNonce);

            UniValue inpChallenge(UniValue::VOBJ);
            inpChallenge.pushKV("name", "Challenge");
            inpChallenge.pushKV("type", "challenge");
            inpChallenge.pushKV("value", tipHashHex);
            inputs.push_back(inpChallenge);

            UniValue inpPk(UniValue::VOBJ);
            inpPk.pushKV("name", "Pubkey");
            inpPk.pushKV("type", "pubkey");
            inpPk.pushKV("value", pubkeyHex);
            inputs.push_back(inpPk);

            UniValue inpLt(UniValue::VOBJ);
            inpLt.pushKV("name", "Lock-Until");
            inpLt.pushKV("type", "timestamp-or-block-height");
            inpLt.pushKV("value", minerLockHeight); // Block height (~24h CLTV lock)
            inputs.push_back(inpLt);

            UniValue inpPkh(UniValue::VOBJ);
            inpPkh.pushKV("name", "PubkeyHash");
            inpPkh.pushKV("type", "pubkeyhash");
            inpPkh.pushKV("value", pubkeyHashHex);
            inputs.push_back(inpPkh);

            act.pushKV("inputs", inputs);
            customActions.push_back(act);
        }
        else if (index == 12) { // Time-Delayed Vault (Anti-Theft)
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "check-signature-verification");
            UniValue exprInputs(UniValue::VARR);
            UniValue exprInp(UniValue::VOBJ);
            exprInp.pushKV("name", "Pubkey");
            exprInp.pushKV("type", "pubkey");
            exprInp.pushKV("value", "03bb9f1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234a1");
            exprInputs.push_back(exprInp);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VARR);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VARR);
            
            UniValue falseAct1(UniValue::VOBJ);
            falseAct1.pushKV("role", "lock-time");
            UniValue falseInputs1(UniValue::VARR);
            UniValue falseInp1(UniValue::VOBJ);
            falseInp1.pushKV("name", "Lock-Until");
            falseInp1.pushKV("type", "timestamp-or-block-height");
            falseInp1.pushKV("value", lock7dTs);
            falseInputs1.push_back(falseInp1);
            falseAct1.pushKV("inputs", falseInputs1);
            falseAct.push_back(falseAct1);

            UniValue falseAct2(UniValue::VOBJ);
            falseAct2.pushKV("role", "check-signature-verification");
            UniValue falseInputs2(UniValue::VARR);
            UniValue falseInp2(UniValue::VOBJ);
            falseInp2.pushKV("name", "Pubkey");
            falseInp2.pushKV("type", "pubkey");
            falseInp2.pushKV("value", "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            falseInputs2.push_back(falseInp2);
            falseAct2.pushKV("inputs", falseInputs2);
            falseAct.push_back(falseAct2);

            condNode.pushKV("false_action", falseAct);
            customActions.push_back(condNode);
        }
        else if (index == 13) { // Social Recovery Wallet
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "check-signature-verification");
            UniValue exprInputs(UniValue::VARR);
            UniValue exprInp(UniValue::VOBJ);
            exprInp.pushKV("name", "Pubkey");
            exprInp.pushKV("type", "pubkey");
            exprInp.pushKV("value", "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            exprInputs.push_back(exprInp);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VARR);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VARR);

            UniValue falseAct1(UniValue::VOBJ);
            falseAct1.pushKV("role", "multi-signature");
            UniValue falseInputs1(UniValue::VARR);
            UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", 3); falseInputs1.push_back(inpM);
            UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", 5); falseInputs1.push_back(inpN);
            UniValue keysArray(UniValue::VARR);
            keysArray.push_back("03ab89ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1235");
            keysArray.push_back("02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1236");
            keysArray.push_back("03de76ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1237");
            keysArray.push_back("02ef54ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1238");
            keysArray.push_back("03fa32ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1239");
            UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); falseInputs1.push_back(inpSigs);
            falseAct1.pushKV("inputs", falseInputs1);
            falseAct.push_back(falseAct1);

            UniValue falseAct2(UniValue::VOBJ);
            falseAct2.pushKV("role", "lock-time");
            UniValue falseInputs2(UniValue::VARR);
            UniValue falseInp2(UniValue::VOBJ);
            falseInp2.pushKV("name", "Lock-Until");
            falseInp2.pushKV("type", "timestamp-or-block-height");
            falseInp2.pushKV("value", lock60dTs);
            falseInputs2.push_back(falseInp2);
            falseAct2.pushKV("inputs", falseInputs2);
            falseAct.push_back(falseAct2);

            condNode.pushKV("false_action", falseAct);
            customActions.push_back(condNode);
        }
        else if (index == 14) { // Milestone Vesting Escrow
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "lock-time");
            UniValue exprInputs(UniValue::VARR);
            UniValue exprInp(UniValue::VOBJ);
            exprInp.pushKV("name", "Lock-Until");
            exprInp.pushKV("type", "timestamp-or-block-height");
            exprInp.pushKV("value", lock30dTs);
            exprInputs.push_back(exprInp);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VOBJ);
            trueAct.pushKV("role", "multi-signature");
            UniValue trueInputs(UniValue::VARR);
            UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", 2); trueInputs.push_back(inpM);
            UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", 2); trueInputs.push_back(inpN);
            UniValue keysArray(UniValue::VARR);
            keysArray.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            keysArray.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); trueInputs.push_back(inpSigs);
            trueAct.pushKV("inputs", trueInputs);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VOBJ);
            falseAct.pushKV("role", "check-signature-verification");
            UniValue falseInputs(UniValue::VARR);
            UniValue falseInp(UniValue::VOBJ);
            falseInp.pushKV("name", "Pubkey");
            falseInp.pushKV("type", "pubkey");
            falseInp.pushKV("value", "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            falseInputs.push_back(falseInp);
            falseAct.pushKV("inputs", falseInputs);
            condNode.pushKV("false_action", falseAct);

            customActions.push_back(condNode);
        }
        else if (index == 15) { // Collateralized Loan Liquidation
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "multi-signature");
            UniValue exprInputs(UniValue::VARR);
            UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", 2); exprInputs.push_back(inpM);
            UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", 2); exprInputs.push_back(inpN);
            UniValue keysArray(UniValue::VARR);
            keysArray.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            keysArray.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); exprInputs.push_back(inpSigs);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VARR);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VARR);

            UniValue falseAct1(UniValue::VOBJ);
            falseAct1.pushKV("role", "lock-time");
            UniValue falseInputs1(UniValue::VARR);
            UniValue falseInp1(UniValue::VOBJ);
            falseInp1.pushKV("name", "Lock-Until");
            falseInp1.pushKV("type", "timestamp-or-block-height");
            falseInp1.pushKV("value", lock90dTs);
            falseInputs1.push_back(falseInp1);
            falseAct1.pushKV("inputs", falseInputs1);
            falseAct.push_back(falseAct1);

            UniValue falseAct2(UniValue::VOBJ);
            falseAct2.pushKV("role", "check-signature-verification");
            UniValue falseInputs2(UniValue::VARR);
            UniValue falseInp2(UniValue::VOBJ);
            falseInp2.pushKV("name", "Pubkey");
            falseInp2.pushKV("type", "pubkey");
            falseInp2.pushKV("value", "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            falseInputs2.push_back(falseInp2);
            falseAct2.pushKV("inputs", falseInputs2);
            falseAct.push_back(falseAct2);

            condNode.pushKV("false_action", falseAct);
            customActions.push_back(condNode);
        }
        else if (index == 16) { // Double-Deposit P2P Escrow
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "multi-signature");
            UniValue exprInputs(UniValue::VARR);
            UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", 2); exprInputs.push_back(inpM);
            UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", 2); exprInputs.push_back(inpN);
            UniValue keysArray(UniValue::VARR);
            keysArray.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            keysArray.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); exprInputs.push_back(inpSigs);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VARR);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VARR);

            UniValue falseAct1(UniValue::VOBJ);
            falseAct1.pushKV("role", "lock-time");
            UniValue falseInputs1(UniValue::VARR);
            UniValue falseInp1(UniValue::VOBJ);
            falseInp1.pushKV("name", "Lock-Until");
            falseInp1.pushKV("type", "timestamp-or-block-height");
            falseInp1.pushKV("value", lock14dTs);
            falseInputs1.push_back(falseInp1);
            falseAct1.pushKV("inputs", falseInputs1);
            falseAct.push_back(falseAct1);

            UniValue falseAct2(UniValue::VOBJ);
            falseAct2.pushKV("role", "check-signature-verification");
            UniValue falseInputs2(UniValue::VARR);
            UniValue falseInp2(UniValue::VOBJ);
            falseInp2.pushKV("name", "Pubkey");
            falseInp2.pushKV("type", "pubkey");
            falseInp2.pushKV("value", "02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234");
            falseInputs2.push_back(falseInp2);
            falseAct2.pushKV("inputs", falseInputs2);
            falseAct.push_back(falseAct2);

            condNode.pushKV("false_action", falseAct);
            customActions.push_back(condNode);
        }
        else if (index == 17) { // Pay-on-Delivery Supply Chain
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "hash160");
            expr.pushKV("mode", "htlc-equal");
            UniValue exprInputs(UniValue::VARR);
            UniValue exprInp(UniValue::VOBJ);
            exprInp.pushKV("name", "hash160");
            exprInp.pushKV("type", "hash160");
            exprInp.pushKV("value", "a5c9f285d893ce71ab9de8f5c09d765ee982ba34");
            exprInputs.push_back(exprInp);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VOBJ);
            trueAct.pushKV("role", "check-signature-verification");
            UniValue trueInputs(UniValue::VARR);
            UniValue trueInp(UniValue::VOBJ);
            trueInp.pushKV("name", "Pubkey");
            trueInp.pushKV("type", "pubkey");
            trueInp.pushKV("value", "02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234");
            trueInputs.push_back(trueInp);
            trueAct.pushKV("inputs", trueInputs);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VARR);

            UniValue falseAct1(UniValue::VOBJ);
            falseAct1.pushKV("role", "lock-time");
            UniValue falseInputs1(UniValue::VARR);
            UniValue falseInp1(UniValue::VOBJ);
            falseInp1.pushKV("name", "Lock-Until");
            falseInp1.pushKV("type", "timestamp-or-block-height");
            falseInp1.pushKV("value", lock21dTs);
            falseInputs1.push_back(falseInp1);
            falseAct1.pushKV("inputs", falseInputs1);
            falseAct.push_back(falseAct1);

            UniValue falseAct2(UniValue::VOBJ);
            falseAct2.pushKV("role", "check-signature-verification");
            UniValue falseInputs2(UniValue::VARR);
            UniValue falseInp2(UniValue::VOBJ);
            falseInp2.pushKV("name", "Pubkey");
            falseInp2.pushKV("type", "pubkey");
            falseInp2.pushKV("value", "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            falseInputs2.push_back(falseInp2);
            falseAct2.pushKV("inputs", falseInputs2);
            falseAct.push_back(falseAct2);

            condNode.pushKV("false_action", falseAct);
            customActions.push_back(condNode);
        }
        else if (index == 18) { // Subscription Pull Authorization
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VARR);

            UniValue expr1(UniValue::VOBJ);
            expr1.pushKV("role", "check-signature-verification");
            UniValue exprInputs1(UniValue::VARR);
            UniValue exprInp1(UniValue::VOBJ);
            exprInp1.pushKV("name", "Pubkey");
            exprInp1.pushKV("type", "pubkey");
            exprInp1.pushKV("value", "02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234");
            exprInputs1.push_back(exprInp1);
            expr1.pushKV("inputs", exprInputs1);
            expr.push_back(expr1);

            UniValue expr2(UniValue::VOBJ);
            expr2.pushKV("role", "lock-time");
            UniValue exprInputs2(UniValue::VARR);
            UniValue exprInp2(UniValue::VOBJ);
            exprInp2.pushKV("name", "Lock-Until");
            exprInp2.pushKV("type", "timestamp-or-block-height");
            exprInp2.pushKV("value", lock60dTs);
            exprInputs2.push_back(exprInp2);
            expr2.pushKV("inputs", exprInputs2);
            expr.push_back(expr2);

            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VOBJ);
            trueAct.pushKV("role", "check-signature-verification");
            UniValue trueInputs(UniValue::VARR);
            UniValue trueInp(UniValue::VOBJ);
            trueInp.pushKV("name", "Pubkey");
            trueInp.pushKV("type", "pubkey");
            trueInp.pushKV("value", "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            trueInputs.push_back(trueInp);
            trueAct.pushKV("inputs", trueInputs);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VOBJ);
            falseAct.pushKV("role", "check-signature-verification");
            UniValue falseInputs(UniValue::VARR);
            UniValue falseInp(UniValue::VOBJ);
            falseInp.pushKV("name", "Pubkey");
            falseInp.pushKV("type", "pubkey");
            falseInp.pushKV("value", "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            falseInputs.push_back(falseInp);
            falseAct.pushKV("inputs", falseInputs);
            condNode.pushKV("false_action", falseAct);

            customActions.push_back(condNode);
        }
        else if (index == 19) { // Real Estate Title Transfer Escrow
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "multi-signature");
            UniValue exprInputs(UniValue::VARR);
            UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", 3); exprInputs.push_back(inpM);
            UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", 3); exprInputs.push_back(inpN);
            UniValue keysArray(UniValue::VARR);
            keysArray.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            keysArray.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            keysArray.push_back("02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234");
            UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); exprInputs.push_back(inpSigs);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VARR);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VARR);

            UniValue falseAct1(UniValue::VOBJ);
            falseAct1.pushKV("role", "lock-time");
            UniValue falseInputs1(UniValue::VARR);
            UniValue falseInp1(UniValue::VOBJ);
            falseInp1.pushKV("name", "Lock-Until");
            falseInp1.pushKV("type", "timestamp-or-block-height");
            falseInp1.pushKV("value", lock14dTs);
            falseInputs1.push_back(falseInp1);
            falseAct1.pushKV("inputs", falseInputs1);
            falseAct.push_back(falseAct1);

            UniValue falseAct2(UniValue::VOBJ);
            falseAct2.pushKV("role", "check-signature-verification");
            UniValue falseInputs2(UniValue::VARR);
            UniValue falseInp2(UniValue::VOBJ);
            falseInp2.pushKV("name", "Pubkey");
            falseInp2.pushKV("type", "pubkey");
            falseInp2.pushKV("value", "03fa32ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1239");
            falseInputs2.push_back(falseInp2);
            falseAct2.pushKV("inputs", falseInputs2);
            falseAct.push_back(falseAct2);

            condNode.pushKV("false_action", falseAct);
            customActions.push_back(condNode);
        }
        else if (index == 20) { // Rental Deposit Trust
            UniValue act(UniValue::VOBJ);
            act.pushKV("role", "multi-signature");
            UniValue inputs(UniValue::VARR);

            UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", 2); inputs.push_back(inpM);
            UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", 3); inputs.push_back(inpN);

            UniValue keysArray(UniValue::VARR);
            keysArray.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            keysArray.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            keysArray.push_back("02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234");
            UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); inputs.push_back(inpSigs);

            act.pushKV("inputs", inputs);
            customActions.push_back(act);
        }
        else if (index == 21) { // Corporate Governance (3-of-5)
            UniValue act(UniValue::VOBJ);
            act.pushKV("role", "multi-signature");
            UniValue inputs(UniValue::VARR);

            UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", 3); inputs.push_back(inpM);
            UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", 5); inputs.push_back(inpN);

            UniValue keysArray(UniValue::VARR);
            keysArray.push_back("03ab89ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1235");
            keysArray.push_back("02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1236");
            keysArray.push_back("03de76ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1237");
            keysArray.push_back("02ef54ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1238");
            keysArray.push_back("03fa32ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1239");
            UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); inputs.push_back(inpSigs);

            act.pushKV("inputs", inputs);
            customActions.push_back(act);
        }
        else if (index == 22) { // Weighted Board Voting
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "multi-signature");
            UniValue exprInputs(UniValue::VARR);
            UniValue inpM1(UniValue::VOBJ); inpM1.pushKV("name", "m"); inpM1.pushKV("type", "number"); inpM1.pushKV("value", 2); exprInputs.push_back(inpM1);
            UniValue inpN1(UniValue::VOBJ); inpN1.pushKV("name", "n"); inpN1.pushKV("type", "number"); inpN1.pushKV("value", 2); exprInputs.push_back(inpN1);
            UniValue keysArray1(UniValue::VARR);
            keysArray1.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            keysArray1.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            UniValue inpSigs1(UniValue::VOBJ); inpSigs1.pushKV("name", "Signatures"); inpSigs1.pushKV("type", "array"); inpSigs1.pushKV("value", keysArray1); exprInputs.push_back(inpSigs1);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VARR);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VOBJ);
            falseAct.pushKV("role", "multi-signature");
            UniValue falseInputs(UniValue::VARR);
            UniValue inpM2(UniValue::VOBJ); inpM2.pushKV("name", "m"); inpM2.pushKV("type", "number"); inpM2.pushKV("value", 3); falseInputs.push_back(inpM2);
            UniValue inpN2(UniValue::VOBJ); inpN2.pushKV("name", "n"); inpN2.pushKV("type", "number"); inpN2.pushKV("value", 3); falseInputs.push_back(inpN2);
            UniValue keysArray2(UniValue::VARR);
            keysArray2.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            keysArray2.push_back("03de76ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1237");
            keysArray2.push_back("03fa32ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1239");
            UniValue inpSigs2(UniValue::VOBJ); inpSigs2.pushKV("name", "Signatures"); inpSigs2.pushKV("type", "array"); inpSigs2.pushKV("value", keysArray2); falseInputs.push_back(inpSigs2);
            falseAct.pushKV("inputs", falseInputs);
            condNode.pushKV("false_action", falseAct);

            condNode.pushKV("false_action", falseAct);
            customActions.push_back(condNode);
        }
        else if (index == 23) { // DAO Ragequit Covenant
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "check-signature-verification");
            UniValue exprInputs(UniValue::VARR);
            UniValue exprInp(UniValue::VOBJ);
            exprInp.pushKV("name", "Pubkey");
            exprInp.pushKV("type", "pubkey");
            exprInp.pushKV("value", "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            exprInputs.push_back(exprInp);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VARR);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VARR);

            UniValue falseAct1(UniValue::VOBJ);
            falseAct1.pushKV("role", "lock-time");
            UniValue falseInputs1(UniValue::VARR);
            UniValue falseInp1(UniValue::VOBJ);
            falseInp1.pushKV("name", "Lock-Until");
            falseInp1.pushKV("type", "timestamp-or-block-height");
            falseInp1.pushKV("value", lock7dTs);
            falseInputs1.push_back(falseInp1);
            falseAct1.pushKV("inputs", falseInputs1);
            falseAct.push_back(falseAct1);

            UniValue falseAct2(UniValue::VOBJ);
            falseAct2.pushKV("role", "multi-signature");
            UniValue falseInputs2(UniValue::VARR);
            UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", 3); falseInputs2.push_back(inpM);
            UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", 5); falseInputs2.push_back(inpN);
            UniValue keysArray(UniValue::VARR);
            keysArray.push_back("03ab89ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1235");
            keysArray.push_back("02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1236");
            keysArray.push_back("03de76ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1237");
            keysArray.push_back("02ef54ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1238");
            keysArray.push_back("03fa32ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1239");
            UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); falseInputs2.push_back(inpSigs);
            falseAct2.pushKV("inputs", falseInputs2);
            falseAct.push_back(falseAct2);

            condNode.pushKV("false_action", falseAct);
            customActions.push_back(condNode);
        }
        else if (index == 24) { // Dual-Custodian Audit Lock
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "multi-signature");
            UniValue exprInputs(UniValue::VARR);
            UniValue inpM1(UniValue::VOBJ); inpM1.pushKV("name", "m"); inpM1.pushKV("type", "number"); inpM1.pushKV("value", 2); exprInputs.push_back(inpM1);
            UniValue inpN1(UniValue::VOBJ); inpN1.pushKV("name", "n"); inpN1.pushKV("type", "number"); inpN1.pushKV("value", 2); exprInputs.push_back(inpN1);
            UniValue keysArray1(UniValue::VARR);
            keysArray1.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            keysArray1.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            UniValue inpSigs1(UniValue::VOBJ); inpSigs1.pushKV("name", "Signatures"); inpSigs1.pushKV("type", "array"); inpSigs1.pushKV("value", keysArray1); exprInputs.push_back(inpSigs1);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VARR);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VARR);

            UniValue falseAct1(UniValue::VOBJ);
            falseAct1.pushKV("role", "lock-time");
            UniValue falseInputs1(UniValue::VARR);
            UniValue falseInp1(UniValue::VOBJ);
            falseInp1.pushKV("name", "Lock-Until");
            falseInp1.pushKV("type", "timestamp-or-block-height");
            falseInp1.pushKV("value", (int64_t)1793310400);
            falseInputs1.push_back(falseInp1);
            falseAct1.pushKV("inputs", falseInputs1);
            falseAct.push_back(falseAct1);

            UniValue falseAct2(UniValue::VOBJ);
            falseAct2.pushKV("role", "multi-signature");
            UniValue falseInputs2(UniValue::VARR);
            UniValue inpM2(UniValue::VOBJ); inpM2.pushKV("name", "m"); inpM2.pushKV("type", "number"); inpM2.pushKV("value", 2); falseInputs2.push_back(inpM2);
            UniValue inpN2(UniValue::VOBJ); inpN2.pushKV("name", "n"); inpN2.pushKV("type", "number"); inpN2.pushKV("value", 2); falseInputs2.push_back(inpN2);
            UniValue keysArray2(UniValue::VARR);
            keysArray2.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            keysArray2.push_back("02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234");
            UniValue inpSigs2(UniValue::VOBJ); inpSigs2.pushKV("name", "Signatures"); inpSigs2.pushKV("type", "array"); inpSigs2.pushKV("value", keysArray2); falseInputs2.push_back(inpSigs2);
            falseAct2.pushKV("inputs", falseInputs2);
            falseAct.push_back(falseAct2);

            condNode.pushKV("false_action", falseAct);
            customActions.push_back(condNode);
        }
        else if (index == 25) { // P2P Prediction Market Escrow
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "multi-signature");
            UniValue exprInputs(UniValue::VARR);
            UniValue inpM1(UniValue::VOBJ); inpM1.pushKV("name", "m"); inpM1.pushKV("type", "number"); inpM1.pushKV("value", 2); exprInputs.push_back(inpM1);
            UniValue inpN1(UniValue::VOBJ); inpN1.pushKV("name", "n"); inpN1.pushKV("type", "number"); inpN1.pushKV("value", 3); exprInputs.push_back(inpN1);
            UniValue keysArray1(UniValue::VARR);
            keysArray1.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            keysArray1.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            keysArray1.push_back("02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234");
            UniValue inpSigs1(UniValue::VOBJ); inpSigs1.pushKV("name", "Signatures"); inpSigs1.pushKV("type", "array"); inpSigs1.pushKV("value", keysArray1); exprInputs.push_back(inpSigs1);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VARR);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VARR);

            UniValue falseAct1(UniValue::VOBJ);
            falseAct1.pushKV("role", "lock-time");
            UniValue falseInputs1(UniValue::VARR);
            UniValue falseInp1(UniValue::VOBJ);
            falseInp1.pushKV("name", "Lock-Until");
            falseInp1.pushKV("type", "timestamp-or-block-height");
            falseInp1.pushKV("value", lock10dTs);
            falseInputs1.push_back(falseInp1);
            falseAct1.pushKV("inputs", falseInputs1);
            falseAct.push_back(falseAct1);

            UniValue falseAct2(UniValue::VOBJ);
            falseAct2.pushKV("role", "multi-signature");
            UniValue falseInputs2(UniValue::VARR);
            UniValue inpM2(UniValue::VOBJ); inpM2.pushKV("name", "m"); inpM2.pushKV("type", "number"); inpM2.pushKV("value", 2); falseInputs2.push_back(inpM2);
            UniValue inpN2(UniValue::VOBJ); inpN2.pushKV("name", "n"); inpN2.pushKV("type", "number"); inpN2.pushKV("value", 2); falseInputs2.push_back(inpN2);
            UniValue keysArray2(UniValue::VARR);
            keysArray2.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            keysArray2.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            UniValue inpSigs2(UniValue::VOBJ); inpSigs2.pushKV("name", "Signatures"); inpSigs2.pushKV("type", "array"); inpSigs2.pushKV("value", keysArray2); falseInputs2.push_back(inpSigs2);
            falseAct2.pushKV("inputs", falseInputs2);
            falseAct.push_back(falseAct2);

            condNode.pushKV("false_action", falseAct);
            customActions.push_back(condNode);
        }
        else if (index == 26) { // Gaming Tournament Prize Pool
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "multi-signature");
            UniValue exprInputs(UniValue::VARR);
            UniValue inpM1(UniValue::VOBJ); inpM1.pushKV("name", "m"); inpM1.pushKV("type", "number"); inpM1.pushKV("value", 2); exprInputs.push_back(inpM1);
            UniValue inpN1(UniValue::VOBJ); inpN1.pushKV("name", "n"); inpN1.pushKV("type", "number"); inpN1.pushKV("value", 2); exprInputs.push_back(inpN1);
            UniValue keysArray1(UniValue::VARR);
            keysArray1.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            keysArray1.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            UniValue inpSigs1(UniValue::VOBJ); inpSigs1.pushKV("name", "Signatures"); inpSigs1.pushKV("type", "array"); inpSigs1.pushKV("value", keysArray1); exprInputs.push_back(inpSigs1);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VARR);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VARR);

            UniValue falseAct1(UniValue::VOBJ);
            falseAct1.pushKV("role", "lock-time");
            UniValue falseInputs1(UniValue::VARR);
            UniValue falseInp1(UniValue::VOBJ);
            falseInp1.pushKV("name", "Lock-Until");
            falseInp1.pushKV("type", "timestamp-or-block-height");
            falseInp1.pushKV("value", lock14dTs);
            falseInputs1.push_back(falseInp1);
            falseAct1.pushKV("inputs", falseInputs1);
            falseAct.push_back(falseAct1);

            UniValue falseAct2(UniValue::VOBJ);
            falseAct2.pushKV("role", "multi-signature");
            UniValue falseInputs2(UniValue::VARR);
            UniValue inpM2(UniValue::VOBJ); inpM2.pushKV("name", "m"); inpM2.pushKV("type", "number"); inpM2.pushKV("value", 3); falseInputs2.push_back(inpM2);
            UniValue inpN2(UniValue::VOBJ); inpN2.pushKV("name", "n"); inpN2.pushKV("type", "number"); inpN2.pushKV("value", 4); falseInputs2.push_back(inpN2);
            UniValue keysArray2(UniValue::VARR);
            keysArray2.push_back("03ab89ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1235");
            keysArray2.push_back("02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1236");
            keysArray2.push_back("03de76ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1237");
            keysArray2.push_back("02ef54ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1238");
            UniValue inpSigs2(UniValue::VOBJ); inpSigs2.pushKV("name", "Signatures"); inpSigs2.pushKV("type", "array"); inpSigs2.pushKV("value", keysArray2); falseInputs2.push_back(inpSigs2);
            falseAct2.pushKV("inputs", falseInputs2);
            falseAct.push_back(falseAct2);

            condNode.pushKV("false_action", falseAct);
            customActions.push_back(condNode);
        }
        else if (index == 27) { // IoT EV Charging Pay-per-Use
            UniValue act1(UniValue::VOBJ);
            act1.pushKV("role", "hash160");
            act1.pushKV("mode", "hash-equalverify");
            UniValue inputs1(UniValue::VARR);
            UniValue inp1(UniValue::VOBJ);
            inp1.pushKV("name", "hash160");
            inp1.pushKV("type", "hash160");
            inp1.pushKV("value", "b5a9c9f285d893ce71ab9de8f5c09d765ee982ba");
            inputs1.push_back(inp1);
            act1.pushKV("inputs", inputs1);
            customActions.push_back(act1);

            UniValue act2(UniValue::VOBJ);
            act2.pushKV("role", "check-signature-verification");
            UniValue inputs2(UniValue::VARR);
            UniValue inp2(UniValue::VOBJ);
            inp2.pushKV("name", "Pubkey");
            inp2.pushKV("type", "pubkey");
            inp2.pushKV("value", "02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234");
            inputs2.push_back(inp2);
            act2.pushKV("inputs", inputs2);
            customActions.push_back(act2);
        }
        else if (index == 28) { // Parametric Insurance Claim
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "hash160");
            expr.pushKV("mode", "htlc-equal");
            UniValue exprInputs(UniValue::VARR);
            UniValue exprInp(UniValue::VOBJ);
            exprInp.pushKV("name", "hash160");
            exprInp.pushKV("type", "hash160");
            exprInp.pushKV("value", "b5a9c9f285d893ce71ab9de8f5c09d765ee982ba");
            exprInputs.push_back(exprInp);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VOBJ);
            trueAct.pushKV("role", "check-signature-verification");
            UniValue trueInputs(UniValue::VARR);
            UniValue trueInp(UniValue::VOBJ);
            trueInp.pushKV("name", "Pubkey");
            trueInp.pushKV("type", "pubkey");
            trueInp.pushKV("value", "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            trueInputs.push_back(trueInp);
            trueAct.pushKV("inputs", trueInputs);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VARR);

            UniValue falseAct1(UniValue::VOBJ);
            falseAct1.pushKV("role", "lock-time");
            UniValue falseInputs1(UniValue::VARR);
            UniValue falseInp1(UniValue::VOBJ);
            falseInp1.pushKV("name", "Lock-Until");
            falseInp1.pushKV("type", "timestamp-or-block-height");
            falseInp1.pushKV("value", lock14dTs);
            falseInputs1.push_back(falseInp1);
            falseAct1.pushKV("inputs", falseInputs1);
            falseAct.push_back(falseAct1);

            UniValue falseAct2(UniValue::VOBJ);
            falseAct2.pushKV("role", "check-signature-verification");
            UniValue falseInputs2(UniValue::VARR);
            UniValue falseInp2(UniValue::VOBJ);
            falseInp2.pushKV("name", "Pubkey");
            falseInp2.pushKV("type", "pubkey");
            falseInp2.pushKV("value", "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            falseInputs2.push_back(falseInp2);
            falseAct2.pushKV("inputs", falseInputs2);
            falseAct.push_back(falseAct2);

            condNode.pushKV("false_action", falseAct);
            customActions.push_back(condNode);
        }

        updateCustomTree();
        buildContractFromCustom();
        if (index == 10) {
            m_buildStatusTone = StatusTone::Info;
            applyStatusLabelTone(ui->labelStatus, m_buildStatusTone, isLightTheme());
            ui->labelStatus->setText(tr("✓ Coin-Lock (250 KRISTA) template ready. Tip: Use Dashboard → Miner Hub for 1-click registration."));
        } else if (index == 11) {
            m_buildStatusTone = StatusTone::Warning;
            applyStatusLabelTone(ui->labelStatus, m_buildStatusTone, isLightTheme());
            ui->labelStatus->setText(tr("ℹ PoW-Lock requires a solved 4-byte PoW nonce. Use Dashboard → Miner Hub → Register PoW Seat for 1-click PoW solving."));
        }
    }
}

// ═══════════════════════════════════════════════════════════════════
//  RUN TAB
// ═══════════════════════════════════════════════════════════════════

void SmartContractWidget::onRefreshContractsClicked()
{
    m_contractsDirty = false;
    if (!pwalletMain) {
        ui->labelRunStatus->setText(tr("Status: Wallet not loaded."));
        return;
    }

    const bool showZeroPow = ui->chkShowZeroPow && ui->chkShowZeroPow->isChecked();

    // Preserve previous selection if possible
    uint256 prevTxid;
    int prevVout = -1;
    int prevIdx = ui->comboContracts->currentIndex();
    if (prevIdx >= 0 && prevIdx < m_utxos.size()) {
        prevTxid = m_utxos[prevIdx].txid;
        prevVout = m_utxos[prevIdx].vout;
    }

    int hiddenZeroPowCount = 0;
    m_utxos.clear();
    {
        QSignalBlocker blocker(ui->comboContracts);
        ui->comboContracts->clear();

        LOCK2(cs_main, pwalletMain->cs_wallet);

        // Bound decompilation cache to keep memory usage steady on active wallets
        if (m_scriptCache.size() > 512) {
            m_scriptCache.clear();
        }

        for (const auto& kv : pwalletMain->mapWallet) {
            const CWalletTx& wtx = kv.second;
            int depth = wtx.GetDepthInMainChain();
            if (depth < 0)
                continue; // skip conflicted/abandoned transactions

            for (int i = 0; i < (int)wtx.vout.size(); i++) {
                const CTxOut& out = wtx.vout[i];

                // Skip if already spent
                if (pwalletMain->IsSpent(kv.first, i))
                    continue;

                // Accept MESCAL contracts, ADAM lock contracts, multisig contracts, and non-standard scripts;
                // skip regular P2PKH, P2PK, P2SH, OP_RETURN, and Zerocoin outputs.
                txnouttype scriptType;
                std::vector<std::vector<unsigned char>> vSolutions;
                if (Solver(out.scriptPubKey, scriptType, vSolutions)) {
                    if (scriptType != TX_NONSTANDARD &&
                        scriptType != TX_MESCAL_CONTRACT &&
                        scriptType != TX_ADAM_COINLOCK &&
                        scriptType != TX_ADAM_POWLOCK &&
                        scriptType != TX_MULTISIG)
                        continue;
                }

                // Look up or compute decompilation + round-trip check in cache
                auto cacheIt = m_scriptCache.find(out.scriptPubKey);
                if (cacheIt == m_scriptCache.end()) {
                    CachedDecompile entry;
                    std::string decompileErr;
                    entry.decompiled = CMescal::Decompile(out.scriptPubKey, decompileErr);
                    if (decompileErr.empty() && entry.decompiled.exists("actions")) {
                        const UniValue& acts = entry.decompiled["actions"];
                        if (acts.isArray() && !acts.empty()) {
                            std::string recompileErr;
                            CScript recompiled = CMescal::Compile(entry.decompiled.write(), recompileErr);
                            if (recompileErr.empty() && recompiled == out.scriptPubKey) {
                                entry.isValidContract = true;
                            }
                        }
                    }
                    cacheIt = m_scriptCache.emplace(out.scriptPubKey, std::move(entry)).first;
                }

                if (!cacheIt->second.isValidContract)
                    continue;

                const ParsedContractMeta meta = ParseContractMetadata(cacheIt->second.decompiled);
                const bool isZeroValueRecord = (out.nValue <= 0);
                if (isZeroValueRecord && !showZeroPow) {
                    hiddenZeroPowCount++;
                    continue;
                }

                ContractUtxo utxo;
                utxo.txid         = kv.first;
                utxo.vout         = i;
                utxo.amount       = out.nValue;
                utxo.depth        = depth;
                utxo.scriptPubKey = out.scriptPubKey;
                utxo.decompiled   = cacheIt->second.decompiled;

                m_utxos.append(utxo);

                const QString tag = ClassifyContractTag(meta, out.nValue);
                QString label = QString("%1 %2...:%3  |  %4 KRISTA  (%5 confs)")
                    .arg(tag)
                    .arg(QString::fromStdString(kv.first.GetHex()).left(8))
                    .arg(i)
                    .arg(out.nValue / (double)COIN, 0, 'f', 4)
                    .arg(depth);

                ui->comboContracts->addItem(label);
            }
        }
    }

    if (m_utxos.isEmpty()) {
        clearRunDetails();
        if (hiddenZeroPowCount > 0) {
            ui->labelRunStatus->setText(tr("Status: No spendable contract UTXOs found (%1 zero-coin PoW record(s) hidden).").arg(hiddenZeroPowCount));
        } else {
            ui->labelRunStatus->setText(tr("Status: No unspent contract UTXOs found in wallet."));
        }
    } else {
        if (hiddenZeroPowCount > 0) {
            ui->labelRunStatus->setText(tr("Status: Found %1 contract UTXO(s) (%2 zero-coin PoW hidden).").arg(m_utxos.size()).arg(hiddenZeroPowCount));
        } else {
            ui->labelRunStatus->setText(tr("Status: Found %1 contract UTXO(s).").arg(m_utxos.size()));
        }
        int selectIdx = 0;
        if (prevVout >= 0) {
            for (int i = 0; i < m_utxos.size(); ++i) {
                if (m_utxos[i].txid == prevTxid && m_utxos[i].vout == prevVout) {
                    selectIdx = i;
                    break;
                }
            }
        }
        {
            QSignalBlocker blocker(ui->comboContracts);
            ui->comboContracts->setCurrentIndex(selectIdx);
        }
        // Only reset input fields if the selected contract changed or if details were empty
        if (prevVout != m_utxos[selectIdx].vout || prevTxid != m_utxos[selectIdx].txid || ui->labelUtxoTxId->text() == "—") {
            onContractSelected(selectIdx);
        } else {
            ui->labelUtxoConfs->setText(QString::number(m_utxos[selectIdx].depth));
        }
    }
}

void SmartContractWidget::onContractSelected(int index)
{
    if (index < 0 || index >= m_utxos.size()) {
        clearRunDetails();
        return;
    }
    populateRunDetails(m_utxos[index]);
}

void SmartContractWidget::populateRunDetails(const ContractUtxo& utxo)
{
    ui->labelUtxoTxId->setText(QString::fromStdString(utxo.txid.GetHex()));
    ui->labelUtxoVout->setText(QString::number(utxo.vout));
    ui->labelUtxoAmount->setText(tr("%1 KRISTA").arg(utxo.amount / (double)COIN, 0, 'f', 8));
    ui->labelUtxoConfs->setText(QString::number(utxo.depth));

    // Decompiled JSON
    const QString decOut = QString::fromStdString(utxo.decompiled.write(2));
    const QString decHtml = highlightJsonHtml(decOut, isLightTheme());
    if (!decHtml.isEmpty()) ui->textEditDecompiled->setHtml(decHtml);
    else ui->textEditDecompiled->setPlainText(decOut);

    // Raw script hex
    std::vector<unsigned char> scriptBytes(utxo.scriptPubKey.begin(), utxo.scriptPubKey.end());
    ui->lineEditRawScript->setText(QString::fromStdString(HexStr(scriptBytes)));

    detectAutoSignCapabilities(utxo);
}

void SmartContractWidget::clearRunDetails()
{
    ui->labelUtxoTxId->setText("—");
    ui->labelUtxoVout->setText("—");
    ui->labelUtxoAmount->setText("—");
    ui->labelUtxoConfs->setText("—");
    ui->textEditDecompiled->clear();
    ui->lineEditRawScript->clear();

    m_autoSignTone = StatusTone::Neutral;
    ui->lblAutoSignStatus->setText("");
    ui->lblAutoSignStatus->setStyleSheet("");
    ui->lblAutoSignStatus->setVisible(false);
    ui->btnRunContract->setEnabled(false);
    ui->btnRunContract->setToolTip("");
    ui->lineEditParam1->setEnabled(true);
    ui->lineEditParam1->clear();
    ui->lineEditParam1->setPlaceholderText(tr("DER-encoded signature or preimage (hex)"));
    ui->lineEditParam2->setEnabled(true);
    ui->lineEditParam2->clear();
    ui->lineEditParam2->setPlaceholderText(tr("Compressed public key (hex, for sig-check contracts)"));
    ui->plainTextExtraSigs->setEnabled(true);
    ui->plainTextExtraSigs->clear();
    ui->plainTextExtraSigs->setPlaceholderText(tr("Additional DER signatures for multi-sig (one hex per line)"));
}

void SmartContractWidget::detectAutoSignCapabilities(const ContractUtxo& utxo)
{
    // Reset status and inputs
    m_autoSignTone = StatusTone::Neutral;
    ui->lblAutoSignStatus->setStyleSheet("");
    ui->lblAutoSignStatus->setText("");
    ui->lblAutoSignStatus->setVisible(false);
    ui->btnRunContract->setEnabled(true);
    ui->btnRunContract->setToolTip("");
    ui->lineEditParam1->setEnabled(true);
    ui->lineEditParam1->clear();
    ui->lineEditParam1->setPlaceholderText(tr("DER-encoded signature or preimage (hex)"));
    ui->lineEditParam2->setEnabled(true);
    ui->lineEditParam2->clear();
    ui->lineEditParam2->setPlaceholderText(tr("Compressed public key (hex, for sig-check contracts)"));
    ui->plainTextExtraSigs->setEnabled(true);
    ui->plainTextExtraSigs->clear();

    if (!pwalletMain) return;
    const bool light = isLightTheme();

    if (utxo.amount <= 0) {
        m_autoSignTone = StatusTone::Neutral;
        applyStatusLabelTone(ui->lblAutoSignStatus, m_autoSignTone, light);
        ui->lblAutoSignStatus->setText(tr("ℹ Zero-Coin PoW (0 KRISTA) registration — contains no locked deposit to withdraw."));
        ui->btnRunContract->setEnabled(false);
        ui->btnRunContract->setToolTip(tr("0 KRISTA Zero-Coin PoW records hold no withdrawable balance."));
        ui->lineEditParam1->setEnabled(false);
        ui->lineEditParam2->setEnabled(false);
        ui->plainTextExtraSigs->setEnabled(false);
        return;
    }

    const ParsedContractMeta meta = ParseContractMetadata(utxo.decompiled);
    const bool& isTimeLock = meta.isTimeLock;
    const bool& isSigCheck = meta.isSigCheck;
    const bool& isPkhCheck = meta.isPkhCheck;
    const bool& isMultiSig = meta.isMultiSig;
    const bool& isHashLock = meta.isHashLock;
    const bool& isMinerReg = meta.isMinerReg;
    const bool& isIfCond   = meta.isIfCond;

    const std::string& timeLockOwnerPubkey = meta.timeLockOwnerPubkey;
    const std::string& pkhHex = meta.pkhHex;
    const std::vector<std::string>& multiSigPubkeys = meta.multiSigPubkeys;
    const int& multiSigM = meta.multiSigM;
    const int& multiSigN = meta.multiSigN;
    const std::string& hashLockPubkey = meta.hashLockPubkey;
    const int64_t& lockTimeVal = meta.lockTimeVal;
    const bool& ifExprIsHashLock = meta.ifExprIsHashLock;
    const std::string& ifTruePubkey = meta.ifTruePubkey;
    const std::string& ifFalsePubkey = meta.ifFalsePubkey;

    LOCK(pwalletMain->cs_wallet);

    if ((isTimeLock || isMinerReg || (isSigCheck && !isHashLock)) && !timeLockOwnerPubkey.empty()) {
        CPubKey pubKey(ParseHex(timeLockOwnerPubkey));
        if (pubKey.IsValid() && pwalletMain->HaveKey(pubKey.GetID())) {
            if (isMinerReg) {
                int64_t nHeight = chainActive.Height();
                int64_t remaining = lockTimeVal - nHeight;
                const int64_t warnThreshold = (utxo.amount == 0) ? 24 : 240;
                if (remaining > 0) {
                    if (remaining <= warnThreshold) { // Expiring warning
                        m_autoSignTone = StatusTone::Warning;
                        applyStatusLabelTone(ui->lblAutoSignStatus, m_autoSignTone, light);
                        ui->lblAutoSignStatus->setText(tr("⚠ Miner registration expiring soon! Remaining lock: %1 blocks (~%2 min). Auto-signing is ready.").arg(remaining).arg(remaining * 30 / 60));
                    } else { // Active
                        m_autoSignTone = StatusTone::Info;
                        applyStatusLabelTone(ui->lblAutoSignStatus, m_autoSignTone, light);
                        ui->lblAutoSignStatus->setText(tr("✓ Signing key found in wallet. Remaining lock: %1 blocks (~%2 hours). Auto-signing is ready.").arg(remaining).arg(QString::number(remaining * 30 / 3600.0, 'f', 1)));
                    }
                } else { // Expired
                    m_autoSignTone = StatusTone::Success;
                    applyStatusLabelTone(ui->lblAutoSignStatus, m_autoSignTone, light);
                    ui->lblAutoSignStatus->setText(tr("✓ Lock period expired! You can now withdraw your miner deposit with automatic wallet signing."));
                }
            } else {
                m_autoSignTone = StatusTone::Info;
                applyStatusLabelTone(ui->lblAutoSignStatus, m_autoSignTone, light);
                ui->lblAutoSignStatus->setText(tr("✓ Required signing key found in wallet. This transaction will be signed automatically."));
            }
            
            // Disable manual parameters since they are not needed
            ui->lineEditParam1->setEnabled(false);
            ui->lineEditParam1->setPlaceholderText(tr("(Auto-signed by wallet)"));
            ui->lineEditParam2->setText(QString::fromStdString(timeLockOwnerPubkey));
            ui->lineEditParam2->setEnabled(false);
            ui->plainTextExtraSigs->setEnabled(false);
        } else {
            m_autoSignTone = StatusTone::Warning;
            applyStatusLabelTone(ui->lblAutoSignStatus, m_autoSignTone, light);
            if (isMinerReg) {
                int64_t nHeight = chainActive.Height();
                int64_t remaining = lockTimeVal - nHeight;
                if (remaining > 0) {
                    ui->lblAutoSignStatus->setText(tr("⚠ Spending key is not in this wallet. Remaining lock: %1 blocks. Manual signature required.").arg(remaining));
                } else {
                    ui->lblAutoSignStatus->setText(tr("⚠ Spending key is not in this wallet. Lock expired — enter manual signature to withdraw."));
                }
            } else {
                ui->lblAutoSignStatus->setText(tr("⚠ Spending key is not in this wallet. Enter signature parameters manually."));
            }
        }
    } else if (isPkhCheck && pkhHex.size() == 40 && IsHex(pkhHex)) {
        std::vector<unsigned char> pkhBytes = ParseHex(pkhHex);
        CKeyID keyId{uint160(pkhBytes)};
        CPubKey pubKey;
        if (pwalletMain->HaveKey(keyId) && pwalletMain->GetPubKey(keyId, pubKey)) {
            m_autoSignTone = StatusTone::Info;
            applyStatusLabelTone(ui->lblAutoSignStatus, m_autoSignTone, light);
            ui->lblAutoSignStatus->setText(tr("✓ PubKeyHash key found in wallet. This transaction will be signed automatically."));
            ui->lineEditParam1->setEnabled(false);
            ui->lineEditParam1->setPlaceholderText(tr("(Auto-signed by wallet)"));
            ui->lineEditParam2->setText(QString::fromStdString(HexStr(pubKey.begin(), pubKey.end())));
            ui->lineEditParam2->setEnabled(false);
            ui->plainTextExtraSigs->setEnabled(false);
        } else {
            m_autoSignTone = StatusTone::Warning;
            applyStatusLabelTone(ui->lblAutoSignStatus, m_autoSignTone, light);
            ui->lblAutoSignStatus->setText(tr("⚠ PubKeyHash key is not in this wallet. Enter signature and public key manually."));
        }
    } else if (isMultiSig) {
        int ownedKeys = 0;
        for (const auto& pkHex : multiSigPubkeys) {
            CPubKey pubKey(ParseHex(pkHex));
            if (pubKey.IsValid() && pwalletMain->HaveKey(pubKey.GetID())) {
                ownedKeys++;
            }
        }

        if (ownedKeys >= multiSigM) {
            m_autoSignTone = StatusTone::Info;
            applyStatusLabelTone(ui->lblAutoSignStatus, m_autoSignTone, light);
            ui->lblAutoSignStatus->setText(tr("✓ Found %1/%2 signing keys in wallet (%3 required). This transaction will be signed automatically.").arg(ownedKeys).arg(multiSigN).arg(multiSigM));
            ui->lineEditParam1->setEnabled(false);
            ui->lineEditParam2->setEnabled(false);
            ui->plainTextExtraSigs->setEnabled(false);
        } else if (ownedKeys > 0) {
            m_autoSignTone = StatusTone::Warning;
            applyStatusLabelTone(ui->lblAutoSignStatus, m_autoSignTone, light);
            ui->lblAutoSignStatus->setText(tr("⚠ Found %1/%2 signing keys in wallet. Please enter the remaining %3 signature(s) below.").arg(ownedKeys).arg(multiSigN).arg(multiSigM - ownedKeys));
            ui->lineEditParam1->setEnabled(false);
            ui->lineEditParam2->setEnabled(false);
            ui->plainTextExtraSigs->setPlaceholderText(tr("Enter the remaining %1 signature(s) here (one hex signature per line)").arg(multiSigM - ownedKeys));
        } else {
            m_autoSignTone = StatusTone::Error;
            applyStatusLabelTone(ui->lblAutoSignStatus, m_autoSignTone, light);
            ui->lblAutoSignStatus->setText(tr("✗ No signing keys found in wallet. Please enter all %1 signatures manually.").arg(multiSigM));
        }
    } else if (isHashLock) {
        ui->lineEditParam1->setPlaceholderText(tr("Enter secret Preimage (text or hex)"));
        ui->lineEditParam2->setEnabled(false);
        if (!hashLockPubkey.empty()) {
            CPubKey pubKey(ParseHex(hashLockPubkey));
            if (pubKey.IsValid() && pwalletMain->HaveKey(pubKey.GetID())) {
                m_autoSignTone = StatusTone::Info;
                applyStatusLabelTone(ui->lblAutoSignStatus, m_autoSignTone, light);
                ui->lblAutoSignStatus->setText(tr("✓ Recipient key found in wallet. Enter only the secret Preimage above."));
                ui->lineEditParam2->setText(QString::fromStdString(hashLockPubkey));
            } else {
                m_autoSignTone = StatusTone::Warning;
                applyStatusLabelTone(ui->lblAutoSignStatus, m_autoSignTone, light);
                ui->lblAutoSignStatus->setText(tr("⚠ Recipient key not in wallet. Enter secret Preimage and manual Recipient Signature."));
                ui->lineEditParam2->setEnabled(true);
            }
        }
    } else if (isIfCond) {
        CPubKey truePk(ParseHex(ifTruePubkey));
        CPubKey falsePk(ParseHex(ifFalsePubkey));
        bool hasTrueKey = truePk.IsValid() && pwalletMain->HaveKey(truePk.GetID());
        bool hasFalseKey = falsePk.IsValid() && pwalletMain->HaveKey(falsePk.GetID());
        if (hasTrueKey && ifExprIsHashLock) {
            m_autoSignTone = StatusTone::Info;
            applyStatusLabelTone(ui->lblAutoSignStatus, m_autoSignTone, light);
            ui->lblAutoSignStatus->setText(tr("✓ Conditional (IF) contract key found in wallet. Enter Preimage for TRUE branch (or leave blank for FALSE branch)."));
            ui->lineEditParam1->setPlaceholderText(tr("Enter Preimage for TRUE branch (leave blank for FALSE/refund branch)"));
            ui->lineEditParam2->setEnabled(false);
            ui->plainTextExtraSigs->setEnabled(false);
        } else if (hasFalseKey) {
            m_autoSignTone = StatusTone::Info;
            applyStatusLabelTone(ui->lblAutoSignStatus, m_autoSignTone, light);
            ui->lblAutoSignStatus->setText(tr("✓ Conditional (ELSE/Refund) contract key found in wallet. Auto-signing is ready."));
            ui->lineEditParam1->setEnabled(false);
            ui->lineEditParam2->setEnabled(false);
            ui->plainTextExtraSigs->setEnabled(false);
        }
    }
}

void SmartContractWidget::onRunContractClicked()
{
    int idx = ui->comboContracts->currentIndex();
    if (idx < 0 || idx >= m_utxos.size()) {
        ui->labelRunStatus->setText(tr("Status: No contract selected."));
        return;
    }
    if (!walletModel || !pwalletMain) {
        ui->labelRunStatus->setText(tr("Status: Wallet not loaded."));
        return;
    }

    const ContractUtxo& utxo = m_utxos[idx];

    if (utxo.amount <= 0) {
        showThemedNotice(this, tr("Zero-Balance Contract"),
            tr("This UTXO is a 0.0000 KRISTA Zero-Coin PoW registration record.\nIt contains no withdrawable deposit."));
        return;
    }

    // ── Validate recipient ────────────────────────────────────────────────────
    QString recipAddrStr = ui->lineEditRecipAddr->text().trimmed();
    if (recipAddrStr.isEmpty()) {
        showThemedNotice(this, tr("Missing Input"), tr("Please enter a recipient address."), true);
        return;
    }
    CTxDestination dest = DecodeDestination(recipAddrStr.toStdString());
    if (!IsValidDestination(dest)) {
        showThemedNotice(this, tr("Invalid Address"), tr("The recipient address is not valid."), true);
        return;
    }
    CScript recipScript = GetScriptForDestination(dest);

    const ParsedContractMeta meta = ParseContractMetadata(utxo.decompiled);
    const bool& isTimeLock = meta.isTimeLock;
    const bool& isSigCheck = meta.isSigCheck;
    const bool& isPkhCheck = meta.isPkhCheck;
    const bool& isMultiSig = meta.isMultiSig;
    const bool& isHashLock = meta.isHashLock;
    const bool& isMinerReg = meta.isMinerReg;
    const bool& isIfCond   = meta.isIfCond;
    const int64_t& lockTimeVal = meta.lockTimeVal;
    const std::string& timeLockOwnerPubkey = meta.timeLockOwnerPubkey;
    const std::string& pkhHex = meta.pkhHex;
    const std::vector<std::string>& multiSigPubkeys = meta.multiSigPubkeys;
    const int& multiSigM = meta.multiSigM;
    const std::string& hashLockPubkey = meta.hashLockPubkey;
    const bool& ifExprIsHashLock = meta.ifExprIsHashLock;
    const std::string& ifTruePubkey = meta.ifTruePubkey;
    const std::string& ifFalsePubkey = meta.ifFalsePubkey;
    const int64_t& ifFalseLockTime = meta.ifFalseLockTime;

    QString param1Check = ui->lineEditParam1->text().trimmed();
    const int64_t activeLockVal = (isIfCond && param1Check.isEmpty() && ifFalseLockTime > 0) ? ifFalseLockTime : lockTimeVal;
    if (((isTimeLock || isMinerReg) || (isIfCond && param1Check.isEmpty())) && activeLockVal > 0) {
        if (activeLockVal < LOCKTIME_THRESHOLD) {
            int curHeight = 0;
            {
                LOCK(cs_main);
                curHeight = chainActive.Height();
            }
            if (curHeight < activeLockVal) {
                showThemedNotice(this, tr("Contract Still Locked"),
                    tr("This contract is time-locked until block #%1.\nCurrent block height: #%2 (%3 blocks remaining).")
                        .arg(activeLockVal)
                        .arg(curHeight)
                        .arg(activeLockVal - curHeight));
                return;
            }
        } else {
            const int64_t curAdjustedTime = GetAdjustedTime();
            if (curAdjustedTime < activeLockVal) {
                const int64_t remSecs = activeLockVal - curAdjustedTime;
                showThemedNotice(this, tr("Contract Still Locked"),
                    tr("This contract is time-locked until UNIX timestamp %1 (%2 UTC).\nTime remaining: ~%3 hours.")
                        .arg(activeLockVal)
                        .arg(QDateTime::fromSecsSinceEpoch(activeLockVal, Qt::UTC).toString("yyyy-MM-dd hh:mm"))
                        .arg(QString::number(remSecs / 3600.0, 'f', 1)));
                return;
            }
        }
    }

    // ── Build spending transaction ────────────────────────────────────────────
    CMutableTransaction rawTx;
    rawTx.nVersion = 1;

    // Input: spend the contract UTXO (start with empty scriptSig)
    CTxIn txIn(COutPoint(utxo.txid, utxo.vout));
    // For time-locked contracts, set nSequence to allow CLTV
    txIn.nSequence = std::numeric_limits<uint32_t>::max() - 1;
    rawTx.vin.push_back(txIn);

    // Estimate fee dynamically from script size while preserving 10,000 satoshi minimum
    const size_t estTxBytes = 350 + utxo.scriptPubKey.size() + recipScript.size();
    const CAmount feeEstimate = std::max<CAmount>(10000, ::minRelayTxFee.GetFee(estTxBytes));
    const CAmount sendAmount = utxo.amount - feeEstimate;
    const CTxOut outRecip(sendAmount, recipScript);
    if (sendAmount <= 0 || outRecip.IsDust(::minRelayTxFee)) {
        showThemedNotice(this, tr("Insufficient Contract Balance"),
            tr("UTXO amount (%1 KRISTA) is too small to cover the required network fee (%2 KRISTA) and dust limit.")
                .arg(utxo.amount / (double)COIN, 0, 'f', 8)
                .arg(feeEstimate / (double)COIN, 0, 'f', 8), true);
        return;
    }

    // Output: recipient
    rawTx.vout.push_back(outRecip);

    QString param1Input = ui->lineEditParam1->text().trimmed();
    if ((isTimeLock || isMinerReg) && lockTimeVal > 0) {
        if (lockTimeVal > 0xFFFFFFFFLL) {
            showThemedNotice(this, tr("Invalid Lock Time"),
                tr("Contract lock time (%1) exceeds 32-bit nLockTime maximum.").arg(lockTimeVal), true);
            return;
        }
        rawTx.nLockTime = static_cast<uint32_t>(lockTimeVal);
    } else if (isIfCond && param1Input.isEmpty() && ifFalseLockTime > 0) {
        if (ifFalseLockTime > 0xFFFFFFFFLL) {
            showThemedNotice(this, tr("Invalid Lock Time"),
                tr("Contract refund lock time (%1) exceeds 32-bit nLockTime maximum.").arg(ifFalseLockTime), true);
            return;
        }
        rawTx.nLockTime = static_cast<uint32_t>(ifFalseLockTime);
    }

    // ── Unlock Wallet ─────────────────────────────────────────────────────────
    WalletModel::UnlockContext ctx(walletModel->requestUnlock());
    if (!ctx.isValid()) {
        ui->labelRunStatus->setText(tr("Status: Wallet unlock failed."));
        return;
    }

    // ── Build scriptSig (unlock) ──────────────────────────────────────────────
    CScript scriptSig;

    // Check if auto-signing is possible
    bool useAutoSign = false;
    int ownedMultiSigKeys = 0;
    {
        LOCK(pwalletMain->cs_wallet);
        if ((isTimeLock || isMinerReg || (isSigCheck && !isHashLock)) && !timeLockOwnerPubkey.empty()) {
            CPubKey pubKey(ParseHex(timeLockOwnerPubkey));
            if (pubKey.IsValid() && pwalletMain->HaveKey(pubKey.GetID())) {
                useAutoSign = true;
            }
        } else if (isPkhCheck && pkhHex.size() == 40 && IsHex(pkhHex)) {
            CKeyID keyId(uint160(ParseHex(pkhHex)));
            if (pwalletMain->HaveKey(keyId)) {
                useAutoSign = true;
            }
        } else if (isMultiSig) {
            for (const auto& pkHex : multiSigPubkeys) {
                CPubKey pubKey(ParseHex(pkHex));
                if (pubKey.IsValid() && pwalletMain->HaveKey(pubKey.GetID())) {
                    ownedMultiSigKeys++;
                }
            }
            if (ownedMultiSigKeys > 0) {
                useAutoSign = true;
            }
        } else if (isHashLock && !hashLockPubkey.empty()) {
            CPubKey pubKey(ParseHex(hashLockPubkey));
            if (pubKey.IsValid() && pwalletMain->HaveKey(pubKey.GetID())) {
                useAutoSign = true;
            }
        } else if (isIfCond) {
            CPubKey truePk(ParseHex(ifTruePubkey));
            CPubKey falsePk(ParseHex(ifFalsePubkey));
            if ((!param1Input.isEmpty() && truePk.IsValid() && pwalletMain->HaveKey(truePk.GetID())) ||
                (param1Input.isEmpty() && falsePk.IsValid() && pwalletMain->HaveKey(falsePk.GetID()))) {
                useAutoSign = true;
            }
        }
    }

    if (useAutoSign) {
        LOCK2(cs_main, pwalletMain->cs_wallet);

        uint256 hash = SignatureHash(utxo.scriptPubKey, rawTx, 0, SIGHASH_ALL, 0, SIGVERSION_BASE);

        if (isMinerReg) {
            CPubKey pubKey(ParseHex(timeLockOwnerPubkey));
            CKey key;
            if (pwalletMain->GetKey(pubKey.GetID(), key)) {
                std::vector<unsigned char> vchSig;
                if (key.Sign(hash, vchSig)) {
                    vchSig.push_back((unsigned char)SIGHASH_ALL);
                    // Pushes [sig] [pubkey]
                    scriptSig << vchSig << ParseHex(timeLockOwnerPubkey);
                }
            }
        } else if (isTimeLock || (isSigCheck && !isHashLock)) {
            CPubKey pubKey(ParseHex(timeLockOwnerPubkey));
            CKey key;
            if (pwalletMain->GetKey(pubKey.GetID(), key)) {
                std::vector<unsigned char> vchSig;
                if (key.Sign(hash, vchSig)) {
                    vchSig.push_back((unsigned char)SIGHASH_ALL);
                    scriptSig << OP_1 << vchSig;
                }
            }
        } else if (isPkhCheck) {
            CKeyID keyId(uint160(ParseHex(pkhHex)));
            CKey key;
            CPubKey pubKey;
            if (pwalletMain->GetKey(keyId, key) && pwalletMain->GetPubKey(keyId, pubKey)) {
                std::vector<unsigned char> vchSig;
                if (key.Sign(hash, vchSig)) {
                    vchSig.push_back((unsigned char)SIGHASH_ALL);
                    scriptSig << vchSig << ToByteVector(pubKey);
                }
            }
        } else if (isMultiSig) {
            std::vector<std::vector<unsigned char>> sigs;
            int foundSigs = 0;
            for (const auto& pkHex : multiSigPubkeys) {
                CPubKey pubKey(ParseHex(pkHex));
                CKey key;
                if (pwalletMain->GetKey(pubKey.GetID(), key)) {
                    std::vector<unsigned char> vchSig;
                    if (key.Sign(hash, vchSig)) {
                        vchSig.push_back((unsigned char)SIGHASH_ALL);
                        sigs.push_back(vchSig);
                        foundSigs++;
                    }
                }
                if (foundSigs >= multiSigM) break;
            }
            // Append any extra signatures provided manually by user
            QString extraSigsText = ui->plainTextExtraSigs->toPlainText().trimmed();
            if (!extraSigsText.isEmpty() && foundSigs < multiSigM) {
                QStringList extraSigs = extraSigsText.split('\n', QString::SkipEmptyParts);
                for (const QString& sig : extraSigs) {
                    std::string sigHex = sig.trimmed().toStdString();
                    if (!IsHex(sigHex)) {
                        showThemedNotice(this, tr("Invalid Input"),
                            tr("Extra signature is not valid hex: %1").arg(sig), true);
                        return;
                    }
                    sigs.push_back(ParseHex(sigHex));
                    foundSigs++;
                    if (foundSigs >= multiSigM) break;
                }
            }
            scriptSig << OP_0;
            for (const auto& sig : sigs) {
                scriptSig << sig;
            }
        } else if (isHashLock) {
            // Get preimage from Param1
            QString preimageStr = ui->lineEditParam1->text().trimmed();
            std::vector<unsigned char> preimageBytes;
            if (IsHex(preimageStr.toStdString())) {
                preimageBytes = ParseHex(preimageStr.toStdString());
            } else {
                std::string s = preimageStr.toStdString();
                preimageBytes.assign(s.begin(), s.end());
            }

            CPubKey pubKey(ParseHex(hashLockPubkey));
            CKey key;
            if (pwalletMain->GetKey(pubKey.GetID(), key)) {
                std::vector<unsigned char> vchSig;
                if (key.Sign(hash, vchSig)) {
                    vchSig.push_back((unsigned char)SIGHASH_ALL);
                    // Stack: OP_1 <sig> <preimage>
                    scriptSig << OP_1 << vchSig << preimageBytes;
                }
            }
        } else if (isIfCond) {
            if (!param1Input.isEmpty() && ifExprIsHashLock) {
                std::vector<unsigned char> preimageBytes;
                if (IsHex(param1Input.toStdString())) {
                    preimageBytes = ParseHex(param1Input.toStdString());
                } else {
                    std::string s = param1Input.toStdString();
                    preimageBytes.assign(s.begin(), s.end());
                }
                CPubKey pubKey(ParseHex(ifTruePubkey));
                CKey key;
                if (pwalletMain->GetKey(pubKey.GetID(), key)) {
                    std::vector<unsigned char> vchSig;
                    if (key.Sign(hash, vchSig)) {
                        vchSig.push_back((unsigned char)SIGHASH_ALL);
                        scriptSig << OP_1 << vchSig << preimageBytes;
                    }
                }
            } else {
                CPubKey pubKey(ParseHex(ifFalsePubkey));
                CKey key;
                if (pwalletMain->GetKey(pubKey.GetID(), key)) {
                    std::vector<unsigned char> vchSig;
                    if (key.Sign(hash, vchSig)) {
                        vchSig.push_back((unsigned char)SIGHASH_ALL);
                        scriptSig << OP_1 << vchSig << OP_0;
                    }
                }
            }
        }
    } else {
        // ── Manual Parameter Fallback ─────────────────────────────────────────
        if (isMinerReg) {
            QString sigStr = ui->lineEditParam1->text().trimmed();
            QString pubkeyStr = ui->lineEditParam2->text().trimmed();
            if (!sigStr.isEmpty() && !pubkeyStr.isEmpty()) {
                if (!IsHex(sigStr.toStdString()) || !IsHex(pubkeyStr.toStdString())) {
                    showThemedNotice(this, tr("Invalid Input"), tr("Signature and Public key must be valid hex."), true);
                    return;
                }
                // Order: <sig> <pubkey>
                scriptSig << ParseHex(sigStr.toStdString()) << ParseHex(pubkeyStr.toStdString());
            }
        } else {
            // Extra sigs (multisig: needs OP_0 first)
            QString extraSigsText = ui->plainTextExtraSigs->toPlainText().trimmed();
            QStringList extraSigs;
            if (!extraSigsText.isEmpty())
                extraSigs = extraSigsText.split('\n', QString::SkipEmptyParts);

            if (!extraSigs.isEmpty()) {
                scriptSig << OP_0;
                for (const QString& sig : extraSigs) {
                    std::string sigHex = sig.trimmed().toStdString();
                    if (!IsHex(sigHex)) {
                        showThemedNotice(this, tr("Invalid Input"),
                            tr("Extra signature is not valid hex: %1").arg(sig), true);
                        return;
                    }
                    scriptSig << ParseHex(sigHex);
                }
            }

            // Param2: pubkey (optional)
            QString param2Str = ui->lineEditParam2->text().trimmed();
            if (!param2Str.isEmpty()) {
                if (!IsHex(param2Str.toStdString())) {
                    showThemedNotice(this, tr("Invalid Input"), tr("Public key is not valid hex."), true);
                    return;
                }
                scriptSig << ParseHex(param2Str.toStdString());
            }

            // Param1: signature or preimage (optional)
            QString param1Str = ui->lineEditParam1->text().trimmed();
            if (!param1Str.isEmpty()) {
                if (!IsHex(param1Str.toStdString())) {
                    showThemedNotice(this, tr("Invalid Input"), tr("Signature / preimage is not valid hex."), true);
                    return;
                }
                scriptSig << ParseHex(param1Str.toStdString());
            }
        }
    }

    rawTx.vin[0].scriptSig = scriptSig;

    // ── Sign via wallet key store ─────────────────────────────────────────────
    // For contracts where the wallet holds the key, use SignSignature
    CTransaction prevTxFull;
    bool foundPrev = false;
    {
        LOCK(cs_main);
        LOCK(pwalletMain->cs_wallet);
        auto it = pwalletMain->mapWallet.find(utxo.txid);
        if (it != pwalletMain->mapWallet.end()) {
            prevTxFull = *static_cast<const CTransaction*>(&it->second);
            foundPrev = true;
        }
    }

    if (!foundPrev) {
        showThemedNotice(this, tr("Error"), tr("Could not find the contract transaction in wallet."), true);
        return;
    }

    {
        const int dispUnit = (walletModel->getOptionsModel()) ? walletModel->getOptionsModel()->getDisplayUnit() : BitcoinUnits::PIV;
        showHideOp(true);
        DefaultDialog* runDlg = new DefaultDialog(window);
        runDlg->setText(
            tr("Confirm Smart Contract Execution"),
            tr("Broadcast transaction spending this smart contract UTXO?\n\n"
               "• Contract Input: %1:%2\n"
               "• Payout Amount: %3\n"
               "• Recipient Address: %4\n"
               "• Estimated Network Fee: %5")
                .arg(QString::fromStdString(utxo.txid.ToString().substr(0, 16) + "..."))
                .arg(utxo.vout)
                .arg(BitcoinUnits::formatWithUnit(dispUnit, sendAmount, false, BitcoinUnits::separatorAlways))
                .arg(recipAddrStr)
                .arg(BitcoinUnits::formatWithUnit(dispUnit, feeEstimate, false, BitcoinUnits::separatorAlways)),
            tr("EXECUTE CONTRACT"),
            tr("CANCEL")
        );
        runDlg->adjustSize();
        const bool confirmedExec = openDialogWithOpaqueBackgroundY(runDlg, window, 3, 5);
        runDlg->deleteLater();
        if (!confirmedExec) {
            return;
        }
    }

    // ── Commit ────────────────────────────────────────────────────────────────
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);

        // Wrap CMutableTransaction → CTransaction → CWalletTx
        CTransaction finalTx(rawTx);
        CWalletTx wtxSpend(pwalletMain, finalTx);
        wtxSpend.fTimeReceivedIsTxTime = 1;

        CReserveKey reservekey(pwalletMain);
        CWallet::CommitResult res = pwalletMain->CommitTransaction(wtxSpend, reservekey, g_connman.get());

        if (res.status != CWallet::CommitStatus::OK) {
            QString errMsg = QString::fromStdString(res.ToString());
            showThemedNotice(this, tr("Commit Failed"),
                tr("Failed to broadcast spending transaction:\n%1\n\nNote: For time-locked contracts, the lock time must have passed.").arg(errMsg), true);
            return;
        }

        walletModel->emitBalanceChanged();

        std::string txid = res.hashTx.GetHex();
        ui->labelRunStatus->setText(tr("Status: Contract executed! Spending TxID: %1")
                                        .arg(QString::fromStdString(txid)));
        inform(tr("Contract executed successfully!\nSpending TxID: %1").arg(QString::fromStdString(txid)));
    }

    // Refresh the UTXO list since the contract is now spent
    onRefreshContractsClicked();
}

// ═══════════════════════════════════════════════════════════════════
//  CUSTOM BUILDER & DEVELOPER MODE
// ═══════════════════════════════════════════════════════════════════

#include "qt/kristatech/visualdesignerdialog.h"
#include <QInputDialog>

void SmartContractWidget::onAddActionClicked()
{
    QStringList items;
    items << tr("Time-Locked (lock-time)")
          << tr("Signature Check (check-signature-verification)")
          << tr("Address / PubKeyHash Check (equalverify-checksig)")
          << tr("Multi-Signature (multi-signature)")
          << tr("Hash-Locked (hash160)")
          << tr("IF-Condition (if-condition)")
          << tr("Miner Registration: Coin-Lock (coin-lock-miner)")
          << tr("Miner Registration: PoW-Lock (pow-miner)")
          << tr("Push Number / True (number)");

    bool ok;
    QString item = getCustomItem(this, tr("Add Step"), tr("Select step type:"), items, 0, false, &ok);
    if (!ok || item.isEmpty()) return;

    QString defaultPubkey = ui->lineEditPubkeyTimeLock->text().trimmed();
    QString defaultPubkeyHash;
    if (!defaultPubkey.isEmpty() && IsHex(defaultPubkey.toStdString())) {
        CPubKey pk(ParseHex(defaultPubkey.toStdString()));
        if (pk.IsValid()) {
            CKeyID id = pk.GetID();
            defaultPubkeyHash = QString::fromStdString(HexStr(id.begin(), id.end()));
        }
    }
    int defaultLock = 150;
    {
        LOCK(cs_main);
        if (chainActive.Height() > 0) defaultLock = chainActive.Height() + 100;
    }

    UniValue node(UniValue::VOBJ);
    UniValue inputs(UniValue::VARR);

    if (item.contains("lock-time")) {
        int64_t lockTime = QInputDialog::getInt(this, tr("Time-Locked"), tr("Lock Until (Block or Timestamp):"), defaultLock, 0, 2000000000, 1, &ok);
        if (!ok) return;

        node.pushKV("role", "lock-time");
        UniValue inp(UniValue::VOBJ);
        inp.pushKV("name", "Lock-Until");
        inp.pushKV("type", "timestamp-or-block-height");
        inp.pushKV("value", lockTime);
        inputs.push_back(inp);
    }
    else if (item.contains("check-signature-verification")) {
        QString pubkey = QInputDialog::getText(this, tr("Signature Check"), tr("Owner Public Key (Hex):"), QLineEdit::Normal, defaultPubkey, &ok);
        if (!ok || pubkey.isEmpty()) return;

        node.pushKV("role", "check-signature-verification");
        UniValue inp(UniValue::VOBJ);
        inp.pushKV("name", "Pubkey");
        inp.pushKV("type", "pubkey");
        inp.pushKV("value", pubkey.trimmed().toStdString());
        inputs.push_back(inp);
    }
    else if (item.contains("equalverify-checksig")) {
        QString pkh = QInputDialog::getText(this, tr("Address / PubKeyHash Check"), tr("PubKeyHash (40-char Hex):"), QLineEdit::Normal, defaultPubkeyHash, &ok);
        if (!ok || pkh.isEmpty()) return;

        node.pushKV("role", "equalverify-checksig");
        UniValue inp(UniValue::VOBJ);
        inp.pushKV("name", "PubKeyHash");
        inp.pushKV("type", "string");
        inp.pushKV("value", pkh.trimmed().toStdString());
        inputs.push_back(inp);
    }
    else if (item.contains("multi-signature")) {
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
    else if (item.contains("hash160")) {
        bool ok1, ok2;
        QString hashHex = QInputDialog::getText(this, tr("Hash-Locked"), tr("Hash160 of Preimage (40-char Hex):"), QLineEdit::Normal, "", &ok1);
        if (!ok1 || hashHex.trimmed().isEmpty()) return;
        QString pubkey = QInputDialog::getText(this, tr("Hash-Locked"), tr("Recipient Pubkey (Hex):"), QLineEdit::Normal, defaultPubkey, &ok2);
        if (!ok2 || pubkey.trimmed().isEmpty()) return;

        node.pushKV("role", "hash160");
        node.pushKV("mode", "hash-equalverify");
        UniValue inpHash(UniValue::VOBJ);
        inpHash.pushKV("name", "hash160");
        inpHash.pushKV("type", "hash160");
        inpHash.pushKV("value", hashHex.trimmed().toStdString());
        inputs.push_back(inpHash);
        node.pushKV("inputs", inputs);
        customActions.push_back(node);

        // Also add signature verification for hash-locked recipient sig
        UniValue sigNode(UniValue::VOBJ);
        sigNode.pushKV("role", "check-signature-verification");
        UniValue inpSig(UniValue::VOBJ);
        inpSig.pushKV("name", "Pubkey");
        inpSig.pushKV("type", "pubkey");
        inpSig.pushKV("value", pubkey.trimmed().toStdString());
        UniValue sigInputs(UniValue::VARR);
        sigInputs.push_back(inpSig);
        sigNode.pushKV("inputs", sigInputs);
        customActions.push_back(sigNode);

        updateCustomTree();
        generateContract();
        return;
    }
    else if (item.contains("if-condition")) {
        // 1. Get Expression
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
            QString hashHex = QInputDialog::getText(this, tr("Expression - Hash-Locked"), tr("Hash160 of Preimage (40-char Hex):"), QLineEdit::Normal, "", &ok1);
            if (!ok1 || hashHex.trimmed().isEmpty()) return;
            exprAct.pushKV("role", "hash160");
            exprAct.pushKV("mode", "htlc-equal");
            UniValue inp(UniValue::VOBJ); inp.pushKV("name", "hash160"); inp.pushKV("type", "hash160"); inp.pushKV("value", hashHex.trimmed().toStdString());
            exprInputs.push_back(inp);
        }
        exprAct.pushKV("inputs", exprInputs);

        // 2. Get True Action
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
                QString hashHex = QInputDialog::getText(this, tr("True Branch - Hash-Locked"), tr("Hash160 of Preimage (40-char Hex):"), QLineEdit::Normal, "", &ok1);
                if (!ok1 || hashHex.trimmed().isEmpty()) return;
                trueAct.pushKV("role", "hash160");
                trueAct.pushKV("mode", "hash-equalverify");
                UniValue inpHash(UniValue::VOBJ); inpHash.pushKV("name", "hash160"); inpHash.pushKV("type", "hash160"); inpHash.pushKV("value", hashHex.trimmed().toStdString()); trueInputs.push_back(inpHash);
            }
            trueAct.pushKV("inputs", trueInputs);
            trueBranch.push_back(trueAct);
        }

        // 3. Get False Action (Optional)
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
                QString hashHex = QInputDialog::getText(this, tr("False Branch - Hash-Locked"), tr("Hash160 of Preimage (40-char Hex):"), QLineEdit::Normal, "", &ok1);
                if (!ok1 || hashHex.trimmed().isEmpty()) return;
                falseAct.pushKV("role", "hash160");
                falseAct.pushKV("mode", "hash-equalverify");
                UniValue inpHash(UniValue::VOBJ); inpHash.pushKV("name", "hash160"); inpHash.pushKV("type", "hash160"); inpHash.pushKV("value", hashHex.trimmed().toStdString()); falseInputs.push_back(inpHash);
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
        condNode.pushKV("true_action", trueBranch);
        if (hasFalse) {
            condNode.pushKV("false_action", falseBranch);
        }

        customActions.push_back(condNode);
        updateCustomTree();
        generateContract();
        return;
    }
    else if (item.contains("coin-lock-miner")) {
        bool ok1, ok2, ok3;
        int minerDefaultLock = 2900;
        {
            LOCK(cs_main);
            minerDefaultLock = std::max(0, chainActive.Height()) + 2900;
        }
        QString ownerPubkey = QInputDialog::getText(this, tr("Coin-Lock Miner"), tr("Miner Public Key (Hex):"), QLineEdit::Normal, defaultPubkey, &ok1);
        if (!ok1 || ownerPubkey.isEmpty()) return;
        int64_t lockHeight = QInputDialog::getInt(this, tr("Coin-Lock Miner"), tr("Lock Until Block Height (+2880..+5760):"), minerDefaultLock, 1, 499999999, 1, &ok2);
        if (!ok2) return;
        QString pubkeyHash = QInputDialog::getText(this, tr("Coin-Lock Miner"), tr("Owner PubKeyHash (40-char Hex):"), QLineEdit::Normal, defaultPubkeyHash, &ok3);
        if (!ok3 || pubkeyHash.isEmpty()) return;

        node.pushKV("role", "coin-lock-miner");
        UniValue inp1(UniValue::VOBJ); inp1.pushKV("name", "Pubkey"); inp1.pushKV("type", "pubkey"); inp1.pushKV("value", ownerPubkey.trimmed().toStdString()); inputs.push_back(inp1);
        UniValue inp2(UniValue::VOBJ); inp2.pushKV("name", "Lock-Until"); inp2.pushKV("type", "timestamp-or-block-height"); inp2.pushKV("value", lockHeight); inputs.push_back(inp2);
        UniValue inp3(UniValue::VOBJ); inp3.pushKV("name", "PubkeyHash"); inp3.pushKV("type", "pubkeyhash"); inp3.pushKV("value", pubkeyHash.trimmed().toStdString()); inputs.push_back(inp3);
    }
    else if (item.contains("pow-miner")) {
        bool ok1, ok2, ok3, ok4, ok5;
        QString defaultChallenge = "0000000000000000000000000000000000000000000000000000000000000000";
        int minerDefaultLock = 2900;
        {
            LOCK(cs_main);
            if (chainActive.Tip()) defaultChallenge = QString::fromStdString(chainActive.Tip()->GetBlockHash().GetHex());
            minerDefaultLock = std::max(0, chainActive.Height()) + 2900;
        }
        QString nonceHex = QInputDialog::getText(this, tr("PoW-Lock Miner"), tr("Solved Nonce (4-byte Hex — or use Dashboard Miner Hub):"), QLineEdit::Normal, "00000000", &ok1);
        if (!ok1 || nonceHex.isEmpty()) return;
        QString challengeHex = QInputDialog::getText(this, tr("PoW-Lock Miner"), tr("Challenge Block Hash (64-char Hex):"), QLineEdit::Normal, defaultChallenge, &ok2);
        if (!ok2 || challengeHex.isEmpty()) return;
        QString ownerPubkey = QInputDialog::getText(this, tr("PoW-Lock Miner"), tr("Miner Public Key (Hex):"), QLineEdit::Normal, defaultPubkey, &ok3);
        if (!ok3 || ownerPubkey.isEmpty()) return;
        int64_t lockHeight = QInputDialog::getInt(this, tr("PoW-Lock Miner"), tr("Lock Until Block Height (+2880..+5760):"), minerDefaultLock, 1, 499999999, 1, &ok4);
        if (!ok4) return;
        QString pubkeyHash = QInputDialog::getText(this, tr("PoW-Lock Miner"), tr("Owner PubKeyHash (40-char Hex):"), QLineEdit::Normal, defaultPubkeyHash, &ok5);
        if (!ok5 || pubkeyHash.isEmpty()) return;

        node.pushKV("role", "pow-miner");
        UniValue inp1(UniValue::VOBJ); inp1.pushKV("name", "Nonce"); inp1.pushKV("type", "nonce"); inp1.pushKV("value", nonceHex.trimmed().toStdString()); inputs.push_back(inp1);
        UniValue inp2(UniValue::VOBJ); inp2.pushKV("name", "Challenge"); inp2.pushKV("type", "challenge"); inp2.pushKV("value", challengeHex.trimmed().toStdString()); inputs.push_back(inp2);
        UniValue inp3(UniValue::VOBJ); inp3.pushKV("name", "Pubkey"); inp3.pushKV("type", "pubkey"); inp3.pushKV("value", ownerPubkey.trimmed().toStdString()); inputs.push_back(inp3);
        UniValue inp4(UniValue::VOBJ); inp4.pushKV("name", "Lock-Until"); inp4.pushKV("type", "timestamp-or-block-height"); inp4.pushKV("value", lockHeight); inputs.push_back(inp4);
        UniValue inp5(UniValue::VOBJ); inp5.pushKV("name", "PubkeyHash"); inp5.pushKV("type", "pubkeyhash"); inp5.pushKV("value", pubkeyHash.trimmed().toStdString()); inputs.push_back(inp5);
    }
    else if (item.contains("number")) {
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
    customActions.push_back(node);

    updateCustomTree();
    generateContract();
}

void SmartContractWidget::onEditAction(QTreeWidgetItem* item, int /*column*/)
{
    if (!item) return;
    int index = ui->treeCustomActions->indexOfTopLevelItem(item);
    if (index < 0 || index >= (int)customActions.size()) return;

    UniValue act = customActions[index];
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
        for (unsigned int i = 0; i < customActions.size(); ++i) {
            newActions.push_back((int)i == index ? updatedAct : customActions[i]);
        }
        customActions = newActions;
        updateCustomTree();
        generateContract();
    } else {
        showThemedNotice(this, tr("Edit Step"), tr("To modify an IF-Condition branch structure, you can edit it directly in Developer Mode (YAML) or replace the IF-Condition step."));
    }
}

void SmartContractWidget::onRemoveActionClicked()
{
    QTreeWidgetItem* current = ui->treeCustomActions->currentItem();
    if (!current) {
        showThemedNotice(this, tr("Selection Required"), tr("Please select a step from the tree to remove."));
        return;
    }
    int index = ui->treeCustomActions->indexOfTopLevelItem(current);
    if (index >= 0 && index < (int)customActions.size()) {
        UniValue newActions(UniValue::VARR);
        for (unsigned int i = 0; i < customActions.size(); ++i) {
            if ((int)i != index) {
                newActions.push_back(customActions[i]);
            }
        }
        customActions = newActions;
        updateCustomTree();
        generateContract();
    }
}

void SmartContractWidget::onMoveActionUp()
{
    QTreeWidgetItem* current = ui->treeCustomActions->currentItem();
    if (!current) return;
    int index = ui->treeCustomActions->indexOfTopLevelItem(current);
    if (index > 0 && index < (int)customActions.size()) {
        UniValue newActions(UniValue::VARR);
        for (int i = 0; i < (int)customActions.size(); ++i) {
            if (i == index - 1) {
                newActions.push_back(customActions[index]);
            } else if (i == index) {
                newActions.push_back(customActions[index - 1]);
            } else {
                newActions.push_back(customActions[i]);
            }
        }
        customActions = newActions;
        updateCustomTree();
        
        QTreeWidgetItem* newCurrent = ui->treeCustomActions->topLevelItem(index - 1);
        ui->treeCustomActions->setCurrentItem(newCurrent);
        
        generateContract();
    }
}

void SmartContractWidget::onMoveActionDown()
{
    QTreeWidgetItem* current = ui->treeCustomActions->currentItem();
    if (!current) return;
    int index = ui->treeCustomActions->indexOfTopLevelItem(current);
    if (index >= 0 && index < (int)customActions.size() - 1) {
        UniValue newActions(UniValue::VARR);
        for (int i = 0; i < (int)customActions.size(); ++i) {
            if (i == index) {
                newActions.push_back(customActions[index + 1]);
            } else if (i == index + 1) {
                newActions.push_back(customActions[index]);
            } else {
                newActions.push_back(customActions[i]);
            }
        }
        customActions = newActions;
        updateCustomTree();
        
        QTreeWidgetItem* newCurrent = ui->treeCustomActions->topLevelItem(index + 1);
        ui->treeCustomActions->setCurrentItem(newCurrent);
        
        generateContract();
    }
}

void SmartContractWidget::onOpenVisualDesignerClicked()
{
    try {
        VisualDesignerDialog dialog(customActions, this);
        if (dialog.exec() == QDialog::Accepted) {
            customActions = dialog.getActions();
            updateCustomTree();
            generateContract();
        }
    } catch (const std::exception& e) {
        showThemedNotice(this, tr("Visual Designer Error"), tr("An error occurred in Visual Designer: %1").arg(e.what()), true);
    } catch (...) {
        showThemedNotice(this, tr("Visual Designer Error"), tr("An unknown error occurred in Visual Designer."), true);
    }
}

void SmartContractWidget::onDeveloperModeToggled(bool checked)
{
    if (checked) {
        ui->textEditYAML->setReadOnly(false);
        ui->textEditYAML->setProperty("scCodeEditable", true);
        updateStyle(ui->textEditYAML);
        if (m_yamlBaseline.isEmpty()) {
            m_yamlBaseline = ui->textEditYAML->toPlainText();
            m_yamlDirty = false;
        }
        updateRightPaneVisibility();
        onYAMLTextChanged();
    } else {
        if (m_yamlDirty) {
            const bool discard = ask(
                tr("Discard Custom YAML Edits?"),
                tr("You have edited the YAML spec in Developer Mode. Switching back to Visual Form mode will discard those edits and regenerate from the form inputs."));
            if (!discard) {
                QSignalBlocker blockCb(ui->cbDeveloperMode);
                ui->cbDeveloperMode->setChecked(true);
                return;
            }
        }
        ui->textEditYAML->setReadOnly(true);
        ui->textEditYAML->setProperty("scCodeEditable", false);
        updateStyle(ui->textEditYAML);
        m_yamlDirty = false;
        m_yamlHasError = false;
        m_yamlErrorText.clear();
        updateRightPaneVisibility();
        generateContract();
    }
}

// Simple YAML Node and Parser implementation
struct YamlNode {
    int lineNum;
    int indent;
    std::string key;
    std::string value;
    bool isListItem;
    bool isQuoted{false};
    std::vector<YamlNode> children;
};

static bool ParseYamlLines(const std::string& yamlStr, std::vector<YamlNode>& rootNodes, std::string& errorStr) {
    std::vector<std::string> lines;
    {
        std::string currentLine;
        for (char c : yamlStr) {
            if (c == '\n' || c == '\r') {
                if (!currentLine.empty() || c == '\n') {
                    lines.push_back(currentLine);
                    currentLine.clear();
                }
            } else {
                currentLine.push_back(c);
            }
        }
        if (!currentLine.empty()) {
            lines.push_back(currentLine);
        }
    }

    struct StackItem {
        int indent;
        YamlNode* node;
    };
    std::vector<StackItem> stack;

    for (size_t lineIdx = 0; lineIdx < lines.size(); ++lineIdx) {
        std::string line = lines[lineIdx];
        
        int indent = 0;
        while (indent < (int)line.size() && line[indent] == ' ') {
            indent++;
        }
        
        std::string content = line.substr(indent);
        size_t hashPos = content.find('#');
        if (hashPos != std::string::npos) {
            content = content.substr(0, hashPos);
        }
        
        while (!content.empty() && (content.back() == ' ' || content.back() == '\t')) {
            content.pop_back();
        }
        
        if (content.empty()) {
            continue;
        }

        YamlNode node;
        node.lineNum = lineIdx + 1;
        node.indent = indent;
        node.isListItem = false;
        node.isQuoted = false;

        if (content[0] == '-') {
            node.isListItem = true;
            content = content.substr(1);
            size_t start = 0;
            while (start < content.size() && (content[start] == ' ' || content[start] == '\t')) {
                start++;
            }
            content = content.substr(start);
        }

        size_t colonPos = content.find(':');
        if (colonPos != std::string::npos) {
            node.key = content.substr(0, colonPos);
            while (!node.key.empty() && (node.key.back() == ' ' || node.key.back() == '\t')) {
                node.key.pop_back();
            }
            std::string valPart = content.substr(colonPos + 1);
            size_t valStart = 0;
            while (valStart < valPart.size() && (valPart[valStart] == ' ' || valPart[valStart] == '\t')) {
                valStart++;
            }
            node.value = valPart.substr(valStart);
        } else {
            node.value = content;
        }

        if (node.value.size() >= 2 &&
            ((node.value.front() == '"' && node.value.back() == '"') ||
             (node.value.front() == '\'' && node.value.back() == '\''))) {
            node.value = node.value.substr(1, node.value.size() - 2);
            node.isQuoted = true;
        }

        // If this is an inline `- key: value` list item, convert it into a list item with a child `key: value`
        if (node.isListItem && !node.key.empty()) {
            YamlNode firstProp;
            firstProp.lineNum = node.lineNum;
            firstProp.indent = indent + 2;
            firstProp.isListItem = false;
            firstProp.key = node.key;
            firstProp.value = node.value;
            firstProp.isQuoted = node.isQuoted;
            node.key.clear();
            node.value.clear();
            node.isQuoted = false;
            node.children.push_back(firstProp);
        }

        while (!stack.empty() && stack.back().indent >= indent) {
            stack.pop_back();
        }

        if (stack.size() >= 32) {
            errorStr = "Maximum YAML nesting depth exceeded (max 32) at line " + std::to_string(node.lineNum);
            return false;
        }

        if (stack.empty()) {
            rootNodes.push_back(node);
            stack.push_back({indent, &rootNodes.back()});
        } else {
            stack.back().node->children.push_back(node);
            stack.push_back({indent, &stack.back().node->children.back()});
        }
    }
    return true;
}

static UniValue ParsePrimitive(const std::string& val, bool isQuoted = false) {
    if (isQuoted) return UniValue(val);
    if (val == "true") return UniValue(true);
    if (val == "false") return UniValue(false);
    
    if (!val.empty()) {
        bool isNum = true;
        bool hasDot = false;
        size_t start = 0;
        if (val[0] == '-') start = 1;
        if (start >= val.size()) isNum = false;
        for (size_t i = start; i < val.size(); ++i) {
            if (val[i] == '.') {
                if (hasDot) { isNum = false; break; }
                hasDot = true;
            } else if (!std::isdigit(static_cast<unsigned char>(val[i]))) {
                isNum = false;
                break;
            }
        }
        if (isNum) {
            try {
                if (hasDot) {
                    return UniValue(std::stod(val));
                } else {
                    return UniValue((int64_t)std::stoll(val));
                }
            } catch (...) {}
        }
    }
    return UniValue(val);
}

static UniValue ConvertNodeChildrenToUniValue(const std::vector<YamlNode>& children, std::string& error, int depth = 0) {
    if (depth > 32) {
        error = "Maximum YAML nesting depth exceeded (max 32)";
        return UniValue("");
    }
    if (children.empty()) {
        return UniValue("");
    }

    bool isArray = false;
    for (const auto& child : children) {
        if (child.isListItem) {
            isArray = true;
            break;
        }
    }

    if (isArray) {
        UniValue arr(UniValue::VARR);
        for (const auto& child : children) {
            if (!child.children.empty()) {
                arr.push_back(ConvertNodeChildrenToUniValue(child.children, error, depth + 1));
            } else {
                arr.push_back(ParsePrimitive(child.value, child.isQuoted));
            }
        }
        return arr;
    } else {
        UniValue obj(UniValue::VOBJ);
        for (const auto& child : children) {
            if (child.key.empty()) {
                if (children.size() == 1) {
                    return ParsePrimitive(child.value, child.isQuoted);
                }
                continue;
            }
            if (!child.children.empty()) {
                obj.pushKV(child.key, ConvertNodeChildrenToUniValue(child.children, error, depth + 1));
            } else {
                obj.pushKV(child.key, ParsePrimitive(child.value, child.isQuoted));
            }
        }
        return obj;
    }
}

void SmartContractWidget::onYAMLTextChanged()
{
    if (!ui->cbDeveloperMode->isChecked()) {
        return;
    }

    m_yamlDirty = (ui->textEditYAML->toPlainText() != m_yamlBaseline);

    std::string yamlStr = ui->textEditYAML->toPlainText().toStdString();
    std::vector<YamlNode> rootNodes;
    std::string error;

    auto setYamlError = [this](const QString& msg) {
        m_yamlHasError = true;
        m_yamlErrorText = msg;
        m_lastCompiledBytes = 0;
        m_lastCompileErr = msg;
        ui->textEditJSON->setPlainText(msg);
        m_buildStatusTone = StatusTone::Error;
        applyStatusLabelTone(ui->labelStatus, m_buildStatusTone, isLightTheme());
        ui->labelStatus->setText(tr("✗ Status: %1").arg(msg));
        if (ui->btnPublish) ui->btnPublish->setEnabled(false);
        updateContractSummary();
    };

    if (!ParseYamlLines(yamlStr, rootNodes, error)) {
        setYamlError(tr("YAML Parse Error: %1").arg(QString::fromStdString(error)));
        return;
    }

    UniValue doc = ConvertNodeChildrenToUniValue(rootNodes, error);
    if (!error.empty()) {
        setYamlError(tr("YAML Conversion Error: %1").arg(QString::fromStdString(error)));
        return;
    }

    m_yamlHasError = false;
    m_yamlErrorText.clear();
    activeDoc = doc;

    if (activeDoc.exists("active_contract") && activeDoc["active_contract"].isStr()) {
        activeName = activeDoc["active_contract"].get_str();
    } else {
        activeName = "CustomContract";
    }

    const QString jsonOut = QString::fromStdString(activeDoc.write(2));
    const QString jsonHtml = highlightJsonHtml(jsonOut, isLightTheme());
    if (!jsonHtml.isEmpty()) ui->textEditJSON->setHtml(jsonHtml);
    else ui->textEditJSON->setPlainText(jsonOut);

    std::string compileErr;
    CScript compiled = CMescal::Compile(activeDoc.write(), compileErr);
    if (!compileErr.empty()) {
        m_lastCompiledBytes = 0;
        m_lastCompileErr = QString::fromStdString(compileErr);
        m_buildStatusTone = StatusTone::Error;
        applyStatusLabelTone(ui->labelStatus, m_buildStatusTone, isLightTheme());
        ui->labelStatus->setText(tr("✗ Status: Compilation Error — %1").arg(QString::fromStdString(compileErr)));
        if (ui->btnPublish) ui->btnPublish->setEnabled(false);
    } else {
        m_lastCompiledBytes = static_cast<int>(compiled.size());
        m_lastCompileErr.clear();
        m_buildStatusTone = StatusTone::Success;
        applyStatusLabelTone(ui->labelStatus, m_buildStatusTone, isLightTheme());
        ui->labelStatus->setText(tr("✓ Status: Valid MESCAL Contract (%1 bytes)").arg(compiled.size()));
        if (ui->btnPublish) ui->btnPublish->setEnabled(true);
    }
    updateContractSummary();
}

void SmartContractWidget::updateCustomTree()
{
    ui->treeCustomActions->clear();
    for (unsigned int i = 0; i < customActions.size(); ++i) {
        const UniValue& act = customActions[i];
        std::string role = act.exists("role") ? act["role"].get_str() : "unknown";
        
        QTreeWidgetItem* item = new QTreeWidgetItem(ui->treeCustomActions);
        
        if (role == "if-condition") {
            item->setText(0, QString("Step %1: IF-Condition").arg(i + 1));
            
            QString details = "IF [";
            UniValue expr = act.exists("expression") ? act["expression"] : (act.exists("expressions") ? act["expressions"] : UniValue());
            if (expr.isArray()) {
                QStringList subDetails;
                for (unsigned int k = 0; k < expr.size(); ++k) {
                    if (expr[k].isObject() && expr[k].exists("role"))
                        subDetails << QString::fromStdString(expr[k]["role"].get_str());
                }
                details += subDetails.join(" AND ") + "] THEN [";
            } else {
                details += QString::fromStdString(expr.exists("role") ? expr["role"].get_str() : "") + "] THEN [";
            }
            
            UniValue trueAct = act.exists("true_action") ? act["true_action"] : (act.exists("true") ? act["true"] : UniValue());
            if (trueAct.isArray()) {
                QStringList subDetails;
                for (unsigned int k = 0; k < trueAct.size(); ++k) {
                    if (trueAct[k].isObject() && trueAct[k].exists("role"))
                        subDetails << QString::fromStdString(trueAct[k]["role"].get_str());
                }
                details += subDetails.join(", ") + "]";
            } else {
                details += QString::fromStdString(trueAct.exists("role") ? trueAct["role"].get_str() : "") + "]";
            }
            
            UniValue falseAct = act.exists("false_action") ? act["false_action"] : (act.exists("false") ? act["false"] : UniValue());
            if (!falseAct.isNull()) {
                if (falseAct.isArray() && !falseAct.empty()) {
                    QStringList subDetails;
                    for (unsigned int k = 0; k < falseAct.size(); ++k) {
                        if (falseAct[k].isObject() && falseAct[k].exists("role"))
                            subDetails << QString::fromStdString(falseAct[k]["role"].get_str());
                    }
                    details += " ELSE [" + subDetails.join(", ") + "]";
                } else if (falseAct.isObject() && falseAct.exists("role")) {
                    details += " ELSE [" + QString::fromStdString(falseAct["role"].get_str()) + "]";
                }
            }
            item->setText(1, details);
        } else {
            item->setText(0, QString("Step %1: %2").arg(i + 1).arg(QString::fromStdString(role)));
            
            QString details = "";
            const UniValue& inputs = act["inputs"];
            if (inputs.isArray()) {
                for (unsigned int j = 0; j < inputs.size(); ++j) {
                    if (!inputs[j].isObject()) continue;
                    QString inpName = inputs[j].exists("name") ? QString::fromStdString(inputs[j]["name"].get_str()) : "";
                    QString inpVal;
                    if (inputs[j].exists("value")) {
                        const UniValue& v = inputs[j]["value"];
                        if (v.isArray()) {
                            inpVal = QString("[%1 keys]").arg(v.size());
                        } else {
                            inpVal = QString::fromStdString(v.getValStr());
                        }
                    }
                    details += QString("%1: %2 | ").arg(inpName).arg(inpVal);
                }
                if (details.endsWith(" | ")) {
                    details.chop(3);
                }
            }
            item->setText(1, details);
        }
    }
}

void SmartContractWidget::buildContractFromCustom()
{
    activeName = "CustomContract";
    UniValue doc(UniValue::VOBJ);
    UniValue basic(UniValue::VOBJ);
    UniValue conditions(UniValue::VOBJ);
    UniValue contract(UniValue::VOBJ);
    UniValue actions(UniValue::VARR);

    int stepIndex = 1;
    int condIndex = 1;

    auto buildBasicSpec = [](const UniValue& srcAct, bool isConditionExpr) -> UniValue {
        UniValue spec(UniValue::VOBJ);
        const std::string role = (srcAct.exists("role") && srcAct["role"].isStr()) ? srcAct["role"].get_str() : "";
        spec.pushKV("role", role);
        if (srcAct.exists("mode") && srcAct["mode"].isStr()) {
            spec.pushKV("mode", srcAct["mode"].get_str());
        } else if (role == "hash160") {
            spec.pushKV("mode", isConditionExpr ? "htlc-equal" : "hash-equalverify");
        }
        if (srcAct.exists("verify") && srcAct["verify"].isBool()) {
            spec.pushKV("verify", srcAct["verify"].get_bool());
        }
        spec.pushKV("inputs", srcAct.exists("inputs") ? srcAct["inputs"] : UniValue(UniValue::VARR));
        return spec;
    };

    auto appendStepRef = [&](const UniValue& srcAct, bool isConditionExpr, UniValue& targetRefs) {
        if (!srcAct.isObject()) return;
        const std::string stepName = "Step-" + std::to_string(stepIndex++);
        basic.pushKV(stepName, buildBasicSpec(srcAct, isConditionExpr));
        UniValue ref(UniValue::VOBJ);
        ref.pushKV("type", "basic");
        ref.pushKV("name", stepName);
        targetRefs.push_back(ref);
    };

    for (unsigned int i = 0; i < customActions.size(); ++i) {
        const UniValue& act = customActions[i];
        std::string role = act["role"].get_str();

        if (role == "if-condition") {
            std::string condName = "Cond-" + std::to_string(condIndex++);
            UniValue condSpec(UniValue::VOBJ);
            condSpec.pushKV("role", "if-condition");

            // 1. Expression Action(s)
            UniValue exprRefs(UniValue::VARR);
            UniValue exprValue = act.exists("expression") ? act["expression"] : (act.exists("expressions") ? act["expressions"] : UniValue());
            if (exprValue.isArray()) {
                for (unsigned int k = 0; k < exprValue.size(); ++k) {
                    appendStepRef(exprValue[k], true, exprRefs);
                }
            } else if (exprValue.isObject()) {
                appendStepRef(exprValue, true, exprRefs);
            }
            condSpec.pushKV("expressions", exprRefs);

            // 2. True Action(s)
            UniValue trueRefs(UniValue::VARR);
            UniValue trueValue = act.exists("true_action") ? act["true_action"] : (act.exists("true") ? act["true"] : UniValue());
            if (trueValue.isArray()) {
                for (unsigned int k = 0; k < trueValue.size(); ++k) {
                    appendStepRef(trueValue[k], false, trueRefs);
                }
            } else if (trueValue.isObject()) {
                appendStepRef(trueValue, false, trueRefs);
            }
            condSpec.pushKV("true", trueRefs);

            // 3. False Action(s) (Optional)
            UniValue falseValue = act.exists("false_action") ? act["false_action"] : (act.exists("false") ? act["false"] : UniValue());
            if (!falseValue.isNull()) {
                UniValue falseRefs(UniValue::VARR);
                if (falseValue.isArray()) {
                    for (unsigned int k = 0; k < falseValue.size(); ++k) {
                        appendStepRef(falseValue[k], false, falseRefs);
                    }
                    if (!falseRefs.empty()) {
                        condSpec.pushKV("false", falseRefs);
                    }
                } else if (falseValue.isObject()) {
                    appendStepRef(falseValue, false, falseRefs);
                    condSpec.pushKV("false", falseRefs);
                }
            }

            conditions.pushKV(condName, condSpec);

            UniValue condRef(UniValue::VOBJ);
            condRef.pushKV("type", "condition");
            condRef.pushKV("name", condName);
            actions.push_back(condRef);
        } else {
            appendStepRef(act, false, actions);
        }
    }

    UniValue cSpec(UniValue::VOBJ);
    cSpec.pushKV("description", "Custom smart contract built using Visual/Custom Builder.");
    cSpec.pushKV("actions", actions);
    contract.pushKV(activeName, cSpec);

    doc.pushKV("basic", basic);
    if (!conditions.empty()) {
        doc.pushKV("condition", conditions);
    }
    doc.pushKV("contract", contract);
    doc.pushKV("active_contract", activeName);

    m_yamlHasError = false;
    m_yamlErrorText.clear();
    activeDoc = doc;
    updatePreviews();
}
