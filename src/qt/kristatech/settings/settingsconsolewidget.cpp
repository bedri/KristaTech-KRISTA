// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/settings/settingsconsolewidget.h"
#include "qt/kristatech/settings/forms/ui_settingsconsolewidget.h"
#include "QGraphicsDropShadowEffect"
#include "qt/kristatech/qtutils.h"

#include "clientmodel.h"
#include "guiutil.h"

#include "chainparams.h"
#include "evo/deterministicmns.h"
#include "main.h"
#include "masternodeman.h"
#include "rpc/client.h"
#include "rpc/server.h"
#include "util.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif // ENABLE_WALLET

#include <openssl/crypto.h>

#include <univalue.h>

#ifdef ENABLE_WALLET
#include <db_cxx.h>
#endif

#include <QAbstractItemView>
#include <QDir>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalMapper>
#include <QStringList>
#include <QTextCursor>
#include <QThread>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>
#include "utilitydialog.h"

const int CONSOLE_HISTORY = 50;

/* Object for executing console RPC commands in a separate thread.
*/
class RPCExecutor : public QObject
{
    Q_OBJECT

public Q_SLOTS:
     void requestCommand(const QString& command);

Q_SIGNALS:
     void reply(int category, const QString& command);
};

/** Class for handling RPC timers
 * (used for e.g. re-locking the wallet after a timeout)
 */
class QtRPCTimerBase: public QObject, public RPCTimerBase
{
    Q_OBJECT
public:
    QtRPCTimerBase(std::function<void(void)>& _func, int64_t millis):
            func(_func)
    {
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, [this]{ func(); });
        timer.start(millis);
    }
    ~QtRPCTimerBase() {}

private:
    QTimer timer;
    std::function<void(void)> func;
};

class QtRPCTimerInterface: public RPCTimerInterface
{
public:
    ~QtRPCTimerInterface() {}
    const char *Name() { return "Qt"; }
    RPCTimerBase* NewTimer(std::function<void(void)>& func, int64_t millis)
    {
        return new QtRPCTimerBase(func, millis);
    }
};

#include "qt/kristatech/settings/moc_settingsconsolewidget.cpp"

/**
 * Split shell command line into a list of arguments. Aims to emulate \c bash and friends.
 *
 * - Arguments are delimited with whitespace
 * - Extra whitespace at the beginning and end and between arguments will be ignored
 * - Text can be "double" or 'single' quoted
 * - The backslash \c \ is used as escape character
 *   - Outside quotes, any character can be escaped
 *   - Within double quotes, only escape \c " and backslashes before a \c " or another backslash
 *   - Within single quotes, no escaping is possible and no special interpretation takes place
 *
 * @param[out]   args        Parsed arguments will be appended to this list
 * @param[in]    strCommand  Command line to split
 */
bool parseCommandLineSettings(std::vector<std::string>& args, const std::string& strCommand)
{
    enum CmdParseState {
        STATE_EATING_SPACES,
        STATE_ARGUMENT,
        STATE_SINGLEQUOTED,
        STATE_DOUBLEQUOTED,
        STATE_ESCAPE_OUTER,
        STATE_ESCAPE_DOUBLEQUOTED
    } state = STATE_EATING_SPACES;
    std::string curarg;
    Q_FOREACH (char ch, strCommand) {
        switch (state) {
            case STATE_ARGUMENT:      // In or after argument
            case STATE_EATING_SPACES: // Handle runs of whitespace
                switch (ch) {
                    case '"':
                        state = STATE_DOUBLEQUOTED;
                        break;
                    case '\'':
                        state = STATE_SINGLEQUOTED;
                        break;
                    case '\\':
                        state = STATE_ESCAPE_OUTER;
                        break;
                    case ' ':
                    case '\n':
                    case '\t':
                        if (state == STATE_ARGUMENT) // Space ends argument
                        {
                            args.push_back(curarg);
                            curarg.clear();
                        }
                        state = STATE_EATING_SPACES;
                        break;
                    default:
                        curarg += ch;
                        state = STATE_ARGUMENT;
                }
                break;
            case STATE_SINGLEQUOTED: // Single-quoted string
                switch (ch) {
                    case '\'':
                        state = STATE_ARGUMENT;
                        break;
                    default:
                        curarg += ch;
                }
                break;
            case STATE_DOUBLEQUOTED: // Double-quoted string
                switch (ch) {
                    case '"':
                        state = STATE_ARGUMENT;
                        break;
                    case '\\':
                        state = STATE_ESCAPE_DOUBLEQUOTED;
                        break;
                    default:
                        curarg += ch;
                }
                break;
            case STATE_ESCAPE_OUTER: // '\' outside quotes
                curarg += ch;
                state = STATE_ARGUMENT;
                break;
            case STATE_ESCAPE_DOUBLEQUOTED:                  // '\' in double-quoted text
                if (ch != '"' && ch != '\\') curarg += '\\'; // keep '\' for everything but the quote and '\' itself
                curarg += ch;
                state = STATE_DOUBLEQUOTED;
                break;
        }
    }
    switch (state) // final state
    {
        case STATE_EATING_SPACES:
            return true;
        case STATE_ARGUMENT:
            args.push_back(curarg);
            return true;
        default: // ERROR to end in one of the other states
            return false;
    }
}

void RPCExecutor::requestCommand(const QString& command)
{
    std::vector<std::string> args;
    if (!parseCommandLineSettings(args, command.toStdString())) {
        Q_EMIT reply(SettingsConsoleWidget::CMD_ERROR, QString("Parse error: unbalanced ' or \""));
        return;
    }
    if (args.empty()) {
        Q_EMIT reply(SettingsConsoleWidget::CMD_REPLY, QString());
        return;
    }
    try {
        std::string strPrint;
        JSONRPCRequest req;
        req.params = RPCConvertValues(args[0], std::vector<std::string>(args.begin() + 1, args.end()));
        req.strMethod = args[0];
        UniValue result = tableRPC.execute(req);

        if (result.isNull())
            strPrint = "";
        else if (result.isStr())
            strPrint = result.get_str();
        else
            strPrint = result.write(2);

        Q_EMIT reply(SettingsConsoleWidget::CMD_REPLY, QString::fromStdString(strPrint));
    } catch (UniValue& objError) {
        try {
            int code = find_value(objError, "code").get_int();
            std::string message = find_value(objError, "message").get_str();
            Q_EMIT reply(SettingsConsoleWidget::CMD_ERROR, QString::fromStdString(message) + " (code " + QString::number(code) + ")");
        } catch (std::runtime_error&) {
            Q_EMIT reply(SettingsConsoleWidget::CMD_ERROR, QString::fromStdString(objError.write()));
        }
    } catch (std::exception& e) {
        Q_EMIT reply(SettingsConsoleWidget::CMD_ERROR, QString("Error: ") + QString::fromStdString(e.what()));
    }
}

SettingsConsoleWidget::SettingsConsoleWidget(KRISTATECHGUI* _window, QWidget *parent) :
    PWidget(_window, parent),
    ui(new Ui::SettingsConsoleWidget)
{
    ui->setupUi(this);
    this->setStyleSheet(parent->styleSheet());

    // Containers
    setCssProperty({ui->left, ui->messagesWidget}, "container");
    ui->left->setContentsMargins(12, 10, 12, 10);
    ui->messagesWidget->setReadOnly(true);
    ui->messagesWidget->setTextInteractionFlags(Qt::TextInteractionFlag::TextSelectableByMouse);
    if (ui->verticalLayout_4) {
        ui->verticalLayout_4->setContentsMargins(8, 6, 8, 6);
    }

    // Tighten fixed vertical spacers so more vertical space belongs to the terminal output
    if (ui->verticalSpacer_2) {
        ui->verticalSpacer_2->changeSize(20, 10, QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    if (ui->verticalSpacer) {
        ui->verticalSpacer->changeSize(20, 10, QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    // Header Row
    setCssTitleScreen(ui->labelTitle);
    ui->labelTitle->setText(tr("RPC Debug Console"));
    ui->pushButtonOpenDebug->setText(tr("Open debug.log"));
    ui->pushButtonOpenDebug->setToolTip(tr("Open ~/.kristatech/debug.log in your default text editor"));
    ui->pushButtonOpenDebug->setCursor(Qt::PointingHandCursor);
    ui->pushButtonCommandOptions->setText(tr("CLI Options"));
    ui->pushButtonCommandOptions->setToolTip(tr("View all kristatechd / kristatech-qt startup command-line flags"));
    ui->pushButtonCommandOptions->setCursor(Qt::PointingHandCursor);
    ui->horizontalLayout_3->setSpacing(10);

    // Console container
    setCssProperty(ui->consoleWidget, "container-square");

    // 1) Status & Find Bar + 2) Quick RPC Chips Bar
    {
        QWidget* topTools = new QWidget(ui->left);
        topTools->setObjectName("consoleTopTools");
        QVBoxLayout* toolsRoot = new QVBoxLayout(topTools);
        toolsRoot->setContentsMargins(0, 0, 0, 8);
        toolsRoot->setSpacing(8);

        // Row A: Live Node Telemetry Pill + In-Output Search
        QHBoxLayout* statusFindRow = new QHBoxLayout();
        statusFindRow->setContentsMargins(0, 0, 0, 0);
        statusFindRow->setSpacing(8);

        labelLiveStatus = new QLabel(topTools);
        labelLiveStatus->setObjectName("consoleLiveStatus");
        labelLiveStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
        statusFindRow->addWidget(labelLiveStatus);
        statusFindRow->addStretch(1);

        lineEditFind = new QLineEdit(topTools);
        lineEditFind->setObjectName("consoleFindInput");
        lineEditFind->setMinimumSize(220, 32);
        lineEditFind->setMaximumSize(260, 32);
        lineEditFind->setClearButtonEnabled(true);
        styleLineEditClearButton(lineEditFind);
        lineEditFind->setPlaceholderText(tr("Find in output (Ctrl+F)..."));
        lineEditFind->installEventFilter(this);
        connect(lineEditFind, &QLineEdit::returnPressed, this, &SettingsConsoleWidget::findInOutput);
        connect(lineEditFind, &QLineEdit::textChanged, this, [this](const QString&) {
            if (findMissHighlight) {
                findMissHighlight = false;
                applyConsoleTheme(isLightTheme());
            }
        });

        btnFindNext = new QPushButton(tr("Find"), topTools);
        btnFindNext->setObjectName("btnConsoleFind");
        btnFindNext->setMinimumSize(60, 32);
        btnFindNext->setMaximumSize(72, 32);
        btnFindNext->setCursor(Qt::PointingHandCursor);
        btnFindNext->setFocusPolicy(Qt::NoFocus);
        btnFindNext->setToolTip(tr("Jump to next match in console output (Enter)"));
        connect(btnFindNext, &QPushButton::clicked, this, &SettingsConsoleWidget::findInOutput);

        statusFindRow->addWidget(lineEditFind);
        statusFindRow->addWidget(btnFindNext);
        toolsRoot->addLayout(statusFindRow);

        // Row B: 1-Click KristaTech Diagnostic Command Chips
        QHBoxLayout* chipsLayout = new QHBoxLayout();
        chipsLayout->setContentsMargins(0, 0, 0, 0);
        chipsLayout->setSpacing(7);

        QLabel* lblQuick = new QLabel(tr("Quick RPC:"), topTools);
        setCssProperty(lblQuick, "text-subtitle");
        chipsLayout->addWidget(lblQuick);

        const QList<QPair<QString, QString>> quickCmds = {
            {QStringLiteral("getblockchaininfo"), tr("Chain height, difficulty & upgrade status")},
            {QStringLiteral("getmininginfo"), tr("Mining status, network hashrate, difficulty & thread count")},
            {QStringLiteral("getadamminers"), tr("Registered Masternode, Coin-Lock, PoW-Lock & Zero-Coin validators")},
            {QStringLiteral("listmasternodes"), tr("Active Masternodes, collateral addresses, status & lastseen health")},
            {QStringLiteral("getactivequorum"), tr("Active 5-member BLS LLMQ quorum")},
            {QStringLiteral("getstakingstatus"), tr("Wallet PoS staking eligibility breakdown")},
            {QStringLiteral("help"), tr("List all available KristaTech RPC commands")}
        };

        for (const auto& pair : quickCmds) {
            QPushButton* chip = new QPushButton(pair.first, topTools);
            chip->setMinimumHeight(29);
            chip->setMaximumHeight(29);
            chip->setCursor(Qt::PointingHandCursor);
            chip->setFocusPolicy(Qt::NoFocus);
            chip->setToolTip(pair.second);
            setCssProperty(chip, "btn-chip");
            const QString cmdText = pair.first;
            connect(chip, &QPushButton::clicked, this, [this, cmdText]() {
                runQuickCommand(cmdText);
            });
            chipsLayout->addWidget(chip);
        }
        chipsLayout->addStretch(1);
        toolsRoot->addLayout(chipsLayout);

        ui->verticalLayout_2->insertWidget(2, topTools);
    }

    // Bottom Command Bar Redesign:
    // [ >_ Commands ▾ ]  [ RPC Command Input ... ]  [ Run ↵ ]  [ Copy Last ]  [ Clear ]
    ui->horizontalLayout_7->setSpacing(8);
    if (ui->horizontalSpacer_5) {
        ui->horizontalSpacer_5->changeSize(0, 20, QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    // 1. Left Command Catalog & Recent History Menu Button (replaces invisible ghost arrow button)
    ui->pushButton->setObjectName("btnConsoleCatalog");
    ui->pushButton->setMinimumSize(134, 42);
    ui->pushButton->setMaximumSize(146, 42);
    ui->pushButton->setText(tr(">_ Commands ▾"));
    ui->pushButton->setToolTip(tr("Browse categorized RPC commands, parameterized templates & recent history"));
    ui->pushButton->setCursor(Qt::PointingHandCursor);
    ui->pushButton->setFocusPolicy(Qt::NoFocus);
    connect(ui->pushButton, &QPushButton::clicked, this, &SettingsConsoleWidget::onBtnCommandsMenuClicked);

    // 2. Command Input LineEdit
    ui->lineEdit->setObjectName("consoleCommandInput");
    ui->lineEdit->setMinimumHeight(42);
    ui->lineEdit->setMaximumHeight(42);
    ui->lineEdit->setClearButtonEnabled(true);
    styleLineEditClearButton(ui->lineEdit);
    ui->lineEdit->setPlaceholderText(tr("Enter RPC command (e.g. getblockchaininfo, help, or type 'adam' / 'quorum' to search)..."));
    initCssEditLine(ui->lineEdit);

    // 3. Primary Run Button + Copy Last Reply Button + Clear Button
    btnRunCmd = new QPushButton(tr("Run ↵"), ui->left);
    btnRunCmd->setObjectName("btnConsoleRun");
    btnRunCmd->setMinimumSize(88, 42);
    btnRunCmd->setMaximumSize(104, 42);
    btnRunCmd->setCursor(Qt::PointingHandCursor);
    btnRunCmd->setFocusPolicy(Qt::NoFocus);
    btnRunCmd->setToolTip(tr("Execute the RPC command in the input box (Enter)"));
    connect(btnRunCmd, &QPushButton::clicked, this, &SettingsConsoleWidget::on_lineEdit_returnPressed);

    btnCopyLast = new QPushButton(tr("Copy Last"), ui->left);
    btnCopyLast->setObjectName("btnConsoleCopyLast");
    btnCopyLast->setMinimumSize(94, 42);
    btnCopyLast->setMaximumSize(110, 42);
    btnCopyLast->setCursor(Qt::PointingHandCursor);
    btnCopyLast->setFocusPolicy(Qt::NoFocus);
    btnCopyLast->setEnabled(false);
    btnCopyLast->setToolTip(tr("Copy the latest RPC JSON/text response cleanly to clipboard"));
    connect(btnCopyLast, &QPushButton::clicked, this, &SettingsConsoleWidget::copyLastOutput);

    ui->pushButtonClear->setObjectName("btnConsoleClear");
    ui->pushButtonClear->setMinimumSize(72, 42);
    ui->pushButtonClear->setMaximumSize(84, 42);
    ui->pushButtonClear->setText(tr("Clear"));
    ui->pushButtonClear->setToolTip(tr("Clear console output screen (Ctrl+L) — command history is preserved"));
    ui->pushButtonClear->setCursor(Qt::PointingHandCursor);
    ui->pushButtonClear->setFocusPolicy(Qt::NoFocus);

    int clearIdx = ui->horizontalLayout_7->indexOf(ui->pushButtonClear);
    if (clearIdx >= 0) {
        ui->horizontalLayout_7->insertWidget(clearIdx, btnRunCmd);
        ui->horizontalLayout_7->insertWidget(clearIdx + 1, btnCopyLast);
    } else {
        ui->horizontalLayout_7->addWidget(btnRunCmd);
        ui->horizontalLayout_7->addWidget(btnCopyLast);
    }

    setCssBtnSecondary(ui->pushButtonOpenDebug);
    setCssBtnSecondary(ui->pushButtonCommandOptions);

    connect(ui->pushButtonClear, &QPushButton::clicked, [this]{ clear(false); });
    connect(ui->pushButtonOpenDebug, &QPushButton::clicked, [this](){
        if (!GUIUtil::openDebugLogfile()) {
            inform(tr("Cannot open debug.log with default text editor."));
        }
    });
    connect(ui->pushButtonCommandOptions, &QPushButton::clicked, this, &SettingsConsoleWidget::onCommandsClicked);

    // Install event filter for keyboard navigation & shortcuts
    ui->lineEdit->installEventFilter(this);
    ui->messagesWidget->installEventFilter(this);

    // Register RPC timer interface
    rpcTimerInterface = new QtRPCTimerInterface();
    RPCSetTimerInterfaceIfUnset(rpcTimerInterface);

    updateLiveStatusBadge();
    startExecutor();
    clear();
}

SettingsConsoleWidget::~SettingsConsoleWidget()
{
    GUIUtil::saveWindowGeometry("nRPCConsoleWindow", this);
    Q_EMIT stopExecutor();
    RPCUnsetTimerInterface(rpcTimerInterface);
    delete rpcTimerInterface;
    delete ui;
}

bool SettingsConsoleWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyevt = static_cast<QKeyEvent*>(event);
        int key = keyevt->key();
        Qt::KeyboardModifiers mod = keyevt->modifiers();

        // Global Ctrl+F inside console focuses the Find bar
        if ((mod & Qt::ControlModifier) && key == Qt::Key_F) {
            if (lineEditFind) {
                lineEditFind->setFocus();
                lineEditFind->selectAll();
                return true;
            }
        }

        // Pressing Esc inside Find bar returns focus to command input
        if (obj == lineEditFind && key == Qt::Key_Escape) {
            if (ui->lineEdit) {
                ui->lineEdit->setFocus();
                return true;
            }
        }

        switch (key) {
            case Qt::Key_Up:
                if (obj == ui->lineEdit) {
                    browseHistory(-1);
                    return true;
                }
                break;
            case Qt::Key_Down:
                if (obj == ui->lineEdit) {
                    browseHistory(1);
                    return true;
                }
                break;
            case Qt::Key_PageUp:
            case Qt::Key_PageDown:
                if (obj == ui->lineEdit) {
                    QApplication::postEvent(ui->messagesWidget, new QKeyEvent(*keyevt));
                    return true;
                }
                break;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                if (autoCompleter && obj == autoCompleter->popup()) {
                    QApplication::postEvent(ui->lineEdit, new QKeyEvent(*keyevt));
                    return true;
                }
                break;
            default:
                if (obj == ui->messagesWidget && ((!mod && !keyevt->text().isEmpty() && key != Qt::Key_Tab) ||
                                                  ((mod & Qt::ControlModifier) && key == Qt::Key_V) ||
                                                  ((mod & Qt::ShiftModifier) && key == Qt::Key_Insert))) {
                    ui->lineEdit->setFocus();
                    QApplication::postEvent(ui->lineEdit, new QKeyEvent(*keyevt));
                    return true;
                }
                if ((mod & Qt::ControlModifier) && key == Qt::Key_L) {
                    clear(false);
                    return true;
                }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void SettingsConsoleWidget::loadClientModel()
{
    if (clientModel) {
        QStringList wordList;
        std::vector<std::string> commandList = tableRPC.listCommands();
        for (size_t i = 0; i < commandList.size(); ++i) {
            wordList << commandList[i].c_str();
            wordList << ("help " + commandList[i]).c_str();
        }

        wordList.sort(Qt::CaseInsensitive);
        autoCompleter = new QCompleter(wordList, this);
        autoCompleter->setCaseSensitivity(Qt::CaseInsensitive);
        autoCompleter->setFilterMode(Qt::MatchContains);
        autoCompleter->setMaxVisibleItems(12);
        ui->lineEdit->setCompleter(autoCompleter);

        if (autoCompleter->popup()) {
            autoCompleter->popup()->installEventFilter(this);
        }

        connect(clientModel, &ClientModel::numBlocksChanged, this, [this](int) { updateLiveStatusBadge(); });
        connect(clientModel, &ClientModel::numConnectionsChanged, this, [this](int) { updateLiveStatusBadge(); });
        connect(clientModel, &ClientModel::strMasternodesChanged, this, [this](const QString&) { updateLiveStatusBadge(); });

        updateLiveStatusBadge();
        applyConsoleTheme(isLightTheme());
    }
}

void SettingsConsoleWidget::showEvent(QShowEvent *event)
{
    PWidget::showEvent(event);
    if (clientModel) {
        clientModel->startMasternodesTimer();
    }
    updateLiveStatusBadge();
    if (ui->lineEdit) ui->lineEdit->setFocus();
}

void SettingsConsoleWidget::hideEvent(QHideEvent *event)
{
    PWidget::hideEvent(event);
    if (clientModel) {
        clientModel->stopMasternodesTimer();
    }
}

void SettingsConsoleWidget::updateLiveStatusBadge()
{
    if (!labelLiveStatus) return;

    int height = clientModel ? clientModel->getNumBlocks() : chainActive.Height();
    int peers = clientModel ? clientModel->getNumConnections() : 0;
    int mnEnabled = mnodeman.CountEnabled();
    int mnTotal = mnodeman.size();
    if (deterministicMNManager) {
        const dip3::CDeterministicMNList dip3List = deterministicMNManager->GetListAtChainTip();
        if (!dip3List.mapMNs.empty()) {
            mnTotal = static_cast<int>(dip3List.mapMNs.size());
            mnEnabled = 0;
            for (const auto& kv : dip3List.mapMNs) {
                if (kv.second.state.IsValid()) ++mnEnabled;
            }
        }
    }

    QString netName = QString::fromStdString(Params().NetworkIDString());
    labelLiveStatus->setText(
        tr("Network: %1   •   Block #%2   •   Peers: %3   •   Masternodes: %4/%5 Enabled")
            .arg(netName)
            .arg(height)
            .arg(peers)
            .arg(mnEnabled)
            .arg(mnTotal)
    );
}

static QString categoryClass(int category)
{
    switch (category) {
        case SettingsConsoleWidget::CMD_REQUEST:
            return "cmd-request";
        case SettingsConsoleWidget::CMD_REPLY:
            return "cmd-reply";
        case SettingsConsoleWidget::CMD_ERROR:
        case SettingsConsoleWidget::MC_ERROR:
            return "cmd-error";
        default:
            return "misc";
    }
}

static QString categoryBadgeHtml(int category)
{
    switch (category) {
        case SettingsConsoleWidget::CMD_REQUEST:
            return QStringLiteral("<span class=\"badge-req\">&gt;</span>");
        case SettingsConsoleWidget::CMD_REPLY:
            return QStringLiteral("<span class=\"badge-ok\">&#10003;</span>");
        case SettingsConsoleWidget::CMD_ERROR:
        case SettingsConsoleWidget::MC_ERROR:
            return QStringLiteral("<span class=\"badge-err\">&#10005;</span>");
        default:
            return QStringLiteral("<span class=\"badge-misc\">&#8226;</span>");
    }
}

void SettingsConsoleWidget::clear(bool clearHistory)
{
    consoleEntries.clear();
    ui->messagesWidget->clear();
    if (clearHistory) {
        history.clear();
        historyPtr = 0;
    }
    lastReplyText.clear();
    if (btnCopyLast) {
        btnCopyLast->setEnabled(false);
    }
    ui->lineEdit->clear();
    ui->lineEdit->setFocus();

    QString theme;
    changeTheme(isLightTheme(), theme);

#ifdef Q_OS_MAC
    QString clsKey = "(⌘)-L";
    QString findKey = "(⌘)-F";
#else
    QString clsKey = "Ctrl+L";
    QString findKey = "Ctrl+F";
#endif

    messageInternal(
        MC_DEBUG,
        tr("<b>KristaTech Core RPC Console</b><br>"
           "• Click <b>&gt;_ Commands ▾</b> (bottom-left) for categorized RPCs, templates &amp; recent history.<br>"
           "• Start typing any keyword (e.g. <b>adam</b>, <b>quorum</b>, <b>masternode</b>, <b>mescal</b>) for instant suggestions.<br>"
           "• Shortcuts: <b>↑ / ↓</b> command history  •  <b>%1</b> find in output  •  <b>%2</b> clear screen."
           "<br><br><span class=\"secwarning\">"
           "SECURITY NOTICE: Never run commands sent by strangers (especially dumpprivkey / dumpwallet) — anyone with your private keys can steal your funds."
           "</span>").arg(findKey, clsKey),
        true
    );
}

void SettingsConsoleWidget::response(int category, const QString &message)
{
    setCommandRunningState(false);
    if (category == CMD_REPLY && message.isEmpty()) {
        return;
    }
    if (category == CMD_REPLY || category == CMD_ERROR) {
        lastReplyText = message;
        if (btnCopyLast) {
            btnCopyLast->setEnabled(!lastReplyText.trimmed().isEmpty());
        }
    }
    messageInternal(category, message);
}

void SettingsConsoleWidget::appendConsoleEntry(const ConsoleLogEntry& entry, bool isLight)
{
    QString out;
    out += "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"2\"><tr>";
    out += "<td class=\"time\" width=\"68\" valign=\"top\">" + entry.timeString + "</td>";
    out += "<td class=\"icon\" width=\"22\" align=\"center\" valign=\"top\">" + categoryBadgeHtml(entry.category) + "</td>";
    out += "<td class=\"message " + categoryClass(entry.category) + "\" valign=\"top\">";
    if (entry.html) {
        out += entry.message;
    } else {
        QString coloredHtml;
        if (entry.category == CMD_REPLY) {
            coloredHtml = highlightJsonHtml(entry.message, isLight);
            if (coloredHtml.isEmpty()) {
                coloredHtml = highlightRpcHelpHtml(entry.message, isLight);
            }
        }
        out += coloredHtml.isEmpty() ? GUIUtil::HtmlEscape(entry.message, true) : coloredHtml;
    }
    out += "</td></tr></table>";
    ui->messagesWidget->append(out);
}

void SettingsConsoleWidget::rebuildConsoleForTheme(bool isLight)
{
    if (!ui || !ui->messagesWidget) return;
    ui->messagesWidget->clear();
    for (const ConsoleLogEntry& entry : consoleEntries) {
        appendConsoleEntry(entry, isLight);
    }
    scrollToEnd();
}

void SettingsConsoleWidget::messageInternal(int category, const QString& message, bool html)
{
    ConsoleLogEntry entry;
    entry.timeString = QTime::currentTime().toString("HH:mm:ss");
    entry.category = category;
    entry.message = message;
    entry.html = html;

    consoleEntries.append(entry);
    if (consoleEntries.size() > 500) {
        consoleEntries.removeFirst();
    }

    appendConsoleEntry(entry, isLightTheme());
    scrollToEnd();
}

static bool PotentiallyDangerousCommand(const QString& cmd)
{
    const QString trimmed = cmd.trimmed();
    if (trimmed.startsWith("dumpwallet", Qt::CaseInsensitive) && trimmed.size() > 10) {
        return true;
    }
    if (trimmed.startsWith("dumpprivkey", Qt::CaseInsensitive) && trimmed.size() > 11) {
        return true;
    }
    return false;
}

void SettingsConsoleWidget::setCommandRunningState(bool running)
{
    isCommandRunning = running;
    if (btnRunCmd) {
        btnRunCmd->setEnabled(!running);
        btnRunCmd->setText(running ? tr("Running...") : tr("Run ↵"));
    }
}

void SettingsConsoleWidget::on_lineEdit_returnPressed()
{
    QString cmd = ui->lineEdit->text().trimmed();
    if (cmd.isEmpty() || isCommandRunning) {
        return;
    }

    // If user left an unedited <placeholder> from a template, highlight it instead of firing a syntax error
    int phStart = cmd.indexOf('<');
    int phEnd = (phStart >= 0) ? cmd.indexOf('>', phStart + 1) : -1;
    if (phStart >= 0 && phEnd > phStart) {
        ui->lineEdit->setFocus();
        ui->lineEdit->setSelection(phStart, phEnd - phStart + 1);
        inform(tr("Please replace %1 with an actual value first.").arg(cmd.mid(phStart, phEnd - phStart + 1)));
        return;
    }

    ui->lineEdit->clear();

    if (PotentiallyDangerousCommand(cmd) &&
        !ask(tr("Security Warning"), tr("Private keys exported by this command give full control over your funds.\n\nNever share this output with anyone. Continue?"))) {
        return;
    }

    setCommandRunningState(true);
    messageInternal(CMD_REQUEST, cmd);
    Q_EMIT cmdCommandRequest(cmd);

    history.removeOne(cmd);
    history.append(cmd);
    while (history.size() > CONSOLE_HISTORY)
        history.removeFirst();
    historyPtr = history.size();
    scrollToEnd();
}

void SettingsConsoleWidget::browseHistory(int offset)
{
    historyPtr += offset;
    if (historyPtr < 0)
        historyPtr = 0;
    if (historyPtr > history.size())
        historyPtr = history.size();
    QString cmd;
    if (historyPtr < history.size())
        cmd = history.at(historyPtr);
    ui->lineEdit->setText(cmd);
}

void SettingsConsoleWidget::startExecutor()
{
    QThread* thread = new QThread;
    RPCExecutor* executor = new RPCExecutor();
    executor->moveToThread(thread);

    connect(executor, &RPCExecutor::reply, this, &SettingsConsoleWidget::response);
    connect(this, &SettingsConsoleWidget::cmdCommandRequest, executor, &RPCExecutor::requestCommand);

    connect(this, &SettingsConsoleWidget::stopExecutor, executor, &RPCExecutor::deleteLater);
    connect(this, &SettingsConsoleWidget::stopExecutor, thread, &QThread::quit);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);

    thread->start();
}

void SettingsConsoleWidget::scrollToEnd()
{
    QScrollBar* scrollbar = ui->messagesWidget->verticalScrollBar();
    scrollbar->setValue(scrollbar->maximum());
}

void SettingsConsoleWidget::runQuickCommand(const QString& cmd)
{
    if (cmd.isEmpty() || isCommandRunning) return;
    ui->lineEdit->setText(cmd);
    on_lineEdit_returnPressed();
}

void SettingsConsoleWidget::applyCommandTemplate(const QString& tmpl)
{
    if (tmpl.isEmpty()) return;
    int phStart = tmpl.indexOf('<');
    int phEnd = (phStart >= 0) ? tmpl.indexOf('>', phStart + 1) : -1;
    if (phStart >= 0 && phEnd > phStart) {
        ui->lineEdit->setText(tmpl);
        ui->lineEdit->setFocus();
        ui->lineEdit->setSelection(phStart, phEnd - phStart + 1);
    } else {
        runQuickCommand(tmpl);
    }
}

void SettingsConsoleWidget::copyLastOutput()
{
    if (!lastReplyText.trimmed().isEmpty()) {
        GUIUtil::setClipboard(lastReplyText);
        inform(tr("Latest RPC response copied to clipboard"));
    } else {
        const QString allText = ui->messagesWidget->toPlainText().trimmed();
        if (!allText.isEmpty()) {
            GUIUtil::setClipboard(allText);
            inform(tr("Console text copied to clipboard"));
        }
    }
}

void SettingsConsoleWidget::findInOutput()
{
    if (!lineEditFind || !ui->messagesWidget) return;
    const QString needle = lineEditFind->text().trimmed();
    if (needle.isEmpty()) {
        ui->lineEdit->setFocus();
        return;
    }

    bool found = ui->messagesWidget->find(needle);
    if (!found) {
        // Wrap around to top of console output and retry once
        QTextCursor savedCursor = ui->messagesWidget->textCursor();
        ui->messagesWidget->moveCursor(QTextCursor::Start);
        found = ui->messagesWidget->find(needle);
        if (!found) {
            ui->messagesWidget->setTextCursor(savedCursor);
        }
    }

    const bool prevMiss = findMissHighlight;
    findMissHighlight = !found;
    if (prevMiss != findMissHighlight) {
        applyConsoleTheme(isLightTheme());
    }
    if (!found) {
        inform(tr("No matches found for \"%1\"").arg(needle));
    }
}

void SettingsConsoleWidget::onBtnCommandsMenuClicked()
{
    const bool light = isLightTheme();
    const QString menuStyle = QString(
        "QMenu { background-color: %1; color: %2; border: 1px solid #7695CD; border-radius: 6px; padding: 4px; }"
        "QMenu::item { padding: 7px 22px 7px 14px; border-radius: 4px; font-size: 13px; }"
        "QMenu::item:selected { background-color: %3; color: %4; font-weight: bold; }"
        "QMenu::item:disabled { color: %5; }"
        "QMenu::separator { height: 1px; background-color: %6; margin: 4px 8px; }"
    ).arg(light ? "#FFFFFF" : "#131B2E",
          light ? "#1C2B45" : "#E2E8F0",
          light ? "rgba(85, 118, 178, 0.14)" : "rgba(118, 149, 205, 0.24)",
          light ? "#1C2B45" : "#FFFFFF",
          light ? "#94A3B8" : "#64748B",
          light ? "#E2E8F0" : "#25395E");

    QMenu rootMenu(this);
    rootMenu.setStyleSheet(menuStyle);

    struct CmdItem {
        QString label;
        QString command;
    };

    auto addCategoryMenu = [&](const QString& title, const QList<CmdItem>& items) {
        QMenu* sub = rootMenu.addMenu(title);
        sub->setStyleSheet(menuStyle);
        QFont boldFont = sub->font();
        boldFont.setPixelSize(13);
        boldFont.setBold(true);
        QFontMetrics fm(boldFont);
        int maxW = 0;
        for (const auto& item : items) {
            QAction* a = sub->addAction(item.label);
            a->setData(item.command);
            maxW = std::max(maxW, fm.boundingRect(item.label).width());
        }
        sub->setMinimumWidth(maxW + 52);
    };

    addCategoryMenu(tr("Chain && Network"), {
        {tr("getblockchaininfo  —  Chain height, difficulty & upgrades"), QStringLiteral("getblockchaininfo")},
        {tr("getmininginfo  —  Hashrate, difficulty & mining state"), QStringLiteral("getmininginfo")},
        {tr("getnetworkinfo  —  Version, protocol (70983) & connections"), QStringLiteral("getnetworkinfo")},
        {tr("getpeerinfo  —  Connected peers, latency & heights"), QStringLiteral("getpeerinfo")},
        {tr("getmempoolinfo  —  Pending transaction count & memory"), QStringLiteral("getmempoolinfo")},
        {tr("getblockhash <height>  —  Look up block hash by height"), QStringLiteral("getblockhash <height>")},
        {tr("getblock <blockhash>  —  Inspect full block & ADAM/PoBLS fields"), QStringLiteral("getblock <blockhash>")}
    });

    addCategoryMenu(tr("ADAM && PoBLS Consensus"), {
        {tr("getadamminers  —  All registered MN / Coin-Lock / PoW-Lock miners"), QStringLiteral("getadamminers")},
        {tr("getactivequorum  —  Current 5-member BLS LLMQ signers"), QStringLiteral("getactivequorum")},
        {tr("registerminer pow  —  Register 10-KRISTA PoW-Lock / 0-Coin PoW"), QStringLiteral("registerminer pow")},
        {tr("registerminer lock <unlock_height>  —  Register 250-KRISTA Coin-Lock"), QStringLiteral("registerminer lock <unlock_height>")}
    });

    addCategoryMenu(tr("Masternodes"), {
        {tr("protx list detailed  —  List KTDIP3 deterministic Masternodes"), QStringLiteral("protx list detailed")},
        {tr("protx bls_generate  —  Generate a fresh BLS12-381 operator keypair"), QStringLiteral("protx bls_generate")},
        {tr("protx info <proTxHash>  —  Inspect a registered KTDIP3 Masternode"), QStringLiteral("protx info <proTxHash>")},
        {tr("getmasternodecount  —  Total, stable & enabled Masternodes"), QStringLiteral("getmasternodecount")},
        {tr("listmasternodes  —  Full network Masternode table"), QStringLiteral("listmasternodes")},
        {tr("listmasternodeconf  —  Local Cold Controller masternode.conf status"), QStringLiteral("listmasternodeconf")},
        {tr("getmasternodestatus  —  Local Hot Multi-MN operator status"), QStringLiteral("getmasternodestatus")},
        {tr("mnsync status  —  Masternode synchronization state"), QStringLiteral("mnsync status")},
        {tr("startmasternode all 0  —  Start all Cold Controller entries"), QStringLiteral("startmasternode all 0")}
    });

    addCategoryMenu(tr("Wallet && Staking"), {
        {tr("getwalletinfo  —  Available, immature & locked balances"), QStringLiteral("getwalletinfo")},
        {tr("getstakingstatus  —  PoS staking readiness checklist"), QStringLiteral("getstakingstatus")},
        {tr("listlockunspent  —  Locked 4,200-KRISTA collateral UTXOs"), QStringLiteral("listlockunspent")},
        {tr("listunspent  —  Spendable wallet UTXOs"), QStringLiteral("listunspent")},
        {tr("validateaddress <address>  —  Inspect address & compressed pubkey"), QStringLiteral("validateaddress <address>")}
    });

    addCategoryMenu(tr("MESCAL && Raw Transactions"), {
        {tr("getrawtransaction <txid> 1  —  Decode transaction JSON"), QStringLiteral("getrawtransaction <txid> 1")},
        {tr("decompilemescal <script_hex>  —  Decompile MESCAL CScript to JSON"), QStringLiteral("decompilemescal <script_hex>")},
        {tr("help <command>  —  Show detailed manual for a specific RPC"), QStringLiteral("help <command>")}
    });

    if (!history.isEmpty()) {
        rootMenu.addSeparator();
        QMenu* histMenu = rootMenu.addMenu(tr("Recent Command History"));
        histMenu->setStyleSheet(menuStyle);
        int shown = 0;
        for (int i = history.size() - 1; i >= 0 && shown < 10; --i, ++shown) {
            QAction* ha = histMenu->addAction(history.at(i));
            ha->setData(history.at(i));
        }
    }

    rootMenu.addSeparator();
    QAction* actHelpAll = rootMenu.addAction(tr("Run 'help' (List All RPC Commands)"));
    actHelpAll->setData(QStringLiteral("help"));

    QFont rootBold = rootMenu.font();
    rootBold.setPixelSize(13);
    rootBold.setBold(true);
    QFontMetrics rfm(rootBold);
    int maxRootW = 0;
    for (QAction* act : rootMenu.actions()) {
        if (act && !act->isSeparator()) {
            maxRootW = std::max(maxRootW, rfm.boundingRect(act->text()).width());
        }
    }
    rootMenu.setMinimumWidth(maxRootW + 58);

    QPoint popupPos = ui->pushButton->mapToGlobal(QPoint(0, -rootMenu.sizeHint().height() - 4));
    if (popupPos.y() < 40) {
        popupPos = ui->pushButton->mapToGlobal(QPoint(0, ui->pushButton->height() + 4));
    }
    QAction* chosen = rootMenu.exec(popupPos);
    if (chosen && chosen->data().isValid()) {
        applyCommandTemplate(chosen->data().toString());
    }
}

void SettingsConsoleWidget::applyConsoleTheme(bool isLight)
{
    const QString statusBg = isLight ? "#EEF3FA" : "#15223B";
    const QString statusBorder = isLight ? "#C5D5EE" : "#283E66";
    const QString statusFg = isLight ? "#2D4673" : "#CBD5E1";

    const QString termBg = isLight ? "#FFFFFF" : "#0D1526";
    const QString termBorder = isLight ? "#CBD5E1" : "#25395E";

    const QString inputBg = isLight ? "#FFFFFF" : "#111C33";
    const QString inputFg = isLight ? "#1E293B" : "#F1F5F9";
    const QString inputBorder = isLight ? "#94A3B8" : "#3B527E";
    const QString findBorder = findMissHighlight ? QStringLiteral("#E05A4E") : (isLight ? QStringLiteral("#CBD5E1") : QStringLiteral("#2D426B"));

    const QString catBg = isLight ? "#EEF3FA" : "#162440";
    const QString catHover = isLight ? "#DCE7F7" : "#1F3258";
    const QString catBorder = isLight ? "#8DA8D8" : "#4B699E";
    const QString catFg = isLight ? "#2D4673" : "#E2E8F0";

    const QString secBg = isLight ? "#F8FAFC" : "#142038";
    const QString secHover = isLight ? "#E8EFFA" : "#1D2E50";
    const QString secBorder = isLight ? "#B8CBE8" : "#324B77";
    const QString secFg = isLight ? "#334155" : "#E2E8F0";
    const QString secDisabledFg = isLight ? "#64748B" : "#8FA4C2";

    const QString localSheet = QString(
        "QWidget#consoleTopTools { background-color: transparent; border: none; }"
        "QLabel#consoleLiveStatus {"
        "  background-color: %1; border: 1px solid %2; border-radius: 6px;"
        "  color: %3; font-size: 12px; font-weight: 600; padding: 5px 12px;"
        "}"
        "QLineEdit#consoleFindInput {"
        "  background-color: %6; border: 1px solid %9; border-radius: 6px;"
        "  color: %7; font-size: 12px; padding: 4px 10px;"
        "}"
        "QLineEdit#consoleFindInput:focus { border: 1px solid #5576B2; }"
        "QPushButton#btnConsoleFind {"
        "  background-color: %14; border: 1px solid %16; border-radius: 6px;"
        "  color: %17; font-size: 12px; font-weight: 600; padding: 4px 10px;"
        "}"
        "QPushButton#btnConsoleFind:hover { background-color: %15; border-color: #5576B2; }"
        "QWidget#consoleWidget {"
        "  background-color: %4; border: 1px solid %5; border-radius: 8px;"
        "}"
        "QTextEdit#messagesWidget {"
        "  background-color: transparent; border: none;"
        "}"
        "QLineEdit#consoleCommandInput {"
        "  background-color: %6; border: 1px solid %8; border-radius: 6px;"
        "  color: %7; font-family: 'DejaVu Sans Mono', 'Liberation Mono', monospace;"
        "  font-size: 13px; padding: 6px 12px;"
        "}"
        "QLineEdit#consoleCommandInput:focus { border: 2px solid #5576B2; padding: 5px 11px; }"
        "QPushButton#btnConsoleCatalog {"
        "  background-color: %10; border: 1px solid %12; border-radius: 6px;"
        "  color: %13; font-size: 13px; font-weight: 600; padding: 6px 10px;"
        "}"
        "QPushButton#btnConsoleCatalog:hover { background-color: %11; border-color: #5576B2; }"
        "QPushButton#btnConsoleRun {"
        "  background-color: %10; border: 1px solid #7695CD; border-radius: 6px;"
        "  color: %13; font-size: 13px; font-weight: 600; padding: 6px 14px;"
        "}"
        "QPushButton#btnConsoleRun:hover { background-color: %11; border-color: #5576B2; }"
        "QPushButton#btnConsoleRun:disabled { background-color: %14; border-color: %16; color: %18; }"
        "QPushButton#btnConsoleCopyLast, QPushButton#btnConsoleClear {"
        "  background-color: %14; border: 1px solid %16; border-radius: 6px;"
        "  color: %17; font-size: 13px; font-weight: 600; padding: 6px 10px;"
        "}"
        "QPushButton#btnConsoleCopyLast:hover, QPushButton#btnConsoleClear:hover {"
        "  background-color: %15; border-color: #5576B2;"
        "}"
        "QPushButton#btnConsoleCopyLast:disabled {"
        "  color: %18; border-color: %5;"
        "}"
    ).arg(statusBg, statusBorder, statusFg,
          termBg, termBorder,
          inputBg, inputFg, inputBorder, findBorder)
     .arg(catBg, catHover, catBorder, catFg,
          secBg, secHover, secBorder, secFg, secDisabledFg);

    ui->left->setStyleSheet(localSheet);
    styleLineEditClearButton(ui->lineEdit);
    styleLineEditClearButton(lineEditFind);

    const QColor phColor = isLight ? QColor("#64748B") : QColor("#94A3B8");
    for (QLineEdit* edit : {ui->lineEdit, lineEditFind}) {
        if (edit) {
            QPalette pal = edit->palette();
            pal.setColor(QPalette::PlaceholderText, phColor);
            edit->setPalette(pal);
        }
    }

    if (autoCompleter && autoCompleter->popup()) {
        autoCompleter->popup()->setStyleSheet(QString(
            "QAbstractItemView {"
            "  background-color: %1; color: %2; border: 1px solid #7695CD;"
            "  selection-background-color: %3; selection-color: %4;"
            "  font-family: 'DejaVu Sans Mono', 'Liberation Mono', monospace; font-size: 13px; padding: 4px;"
            "}"
        ).arg(isLight ? "#FFFFFF" : "#131B2E",
              isLight ? "#1E293B" : "#E2E8F0",
              isLight ? "#DCE7F7" : "#243B67",
              isLight ? "#1C2B45" : "#FFFFFF"));
    }
}

void SettingsConsoleWidget::changeTheme(bool isLightTheme, QString &theme)
{
    if (isLightTheme) {
        ui->messagesWidget->document()->setDefaultStyleSheet(
            "table { color: #1E293B; margin-bottom: 2px; }"
            "td.time { color: #475569; padding-top: 2px; font-family: 'DejaVu Sans Mono', 'Liberation Mono', monospace; font-size: 12px; }"
            "td.message { color: #1E293B; font-family: 'DejaVu Sans Mono', 'Liberation Mono', monospace; font-size: 13px; line-height: 135%; }"
            "td.cmd-request { color: #3B5990; font-weight: bold; }"
            "td.cmd-error { color: #C94A3E; font-weight: bold; }"
            ".badge-req { color: #3B5990; font-family: monospace; font-weight: bold; font-size: 14px; }"
            ".badge-ok  { color: #15803D; font-family: monospace; font-weight: bold; font-size: 13px; }"
            ".badge-err { color: #C94A3E; font-family: monospace; font-weight: bold; font-size: 13px; }"
            ".badge-misc { color: #475569; font-family: monospace; font-weight: bold; font-size: 13px; }"
            ".secwarning { color: #B83B2E; font-weight: bold; }"
            "b { color: #1E293B; }"
        );
    } else {
        ui->messagesWidget->document()->setDefaultStyleSheet(
            "table { color: #F1F5F9; margin-bottom: 2px; }"
            "td.time { color: #CBD5E1; padding-top: 2px; font-family: 'DejaVu Sans Mono', 'Liberation Mono', monospace; font-size: 12px; }"
            "td.message { color: #F1F5F9; font-family: 'DejaVu Sans Mono', 'Liberation Mono', monospace; font-size: 13px; line-height: 135%; }"
            "td.cmd-request { color: #93C5FD; font-weight: bold; }"
            "td.cmd-error { color: #FCA5A5; font-weight: bold; }"
            ".badge-req { color: #93C5FD; font-family: monospace; font-weight: bold; font-size: 14px; }"
            ".badge-ok  { color: #4ADE80; font-family: monospace; font-weight: bold; font-size: 13px; }"
            ".badge-err { color: #FCA5A5; font-family: monospace; font-weight: bold; font-size: 13px; }"
            ".badge-misc { color: #CBD5E1; font-family: monospace; font-weight: bold; font-size: 13px; }"
            ".secwarning { color: #FCA5A5; font-weight: bold; }"
            "b { color: #FFFFFF; }"
        );
    }
    applyConsoleTheme(isLightTheme);
    rebuildConsoleForTheme(isLightTheme);
    updateStyle(ui->messagesWidget);
}

void SettingsConsoleWidget::onCommandsClicked()
{
    if (!clientModel)
        return;

    HelpMessageDialog dlg(this, false);
    dlg.exec();
}
