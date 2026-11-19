// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/qtutils.h"
#include "qt/kristatech/defaultdialog.h"

#include "guiconstants.h"
#include "qrencode.h"
#include "qt/kristatech/snackbar.h"

#include <QAction>
#include <QFile>
#include <QFontDatabase>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QIconEngine>
#include <QImage>
#include <QLabel>
#include <QListView>
#include <QLocale>
#include <QPainter>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>
#include <QInputDialog>
#include <algorithm>

Qt::Modifier SHORT_KEY
#ifdef Q_OS_MAC
    = Qt::CTRL;
#else
    = Qt::ALT;
#endif

// Open non-modal notification banner at the bottom
bool openDialog(QDialog* widget, QWidget* gui)
{
    if (!widget || !gui) return false;
    if ((widget->windowFlags() & Qt::WindowType_Mask) != Qt::Widget) {
        widget->setWindowFlags(Qt::CustomizeWindowHint);
        widget->setAttribute(Qt::WA_TranslucentBackground, true);
    }
    widget->setModal(false);

    for (QPropertyAnimation* anim : widget->findChildren<QPropertyAnimation*>()) {
        anim->stop();
        anim->deleteLater();
    }

    if (SnackBar* sb = qobject_cast<SnackBar*>(widget)) {
        sb->restartTimers();
    }

    const QPoint targetPos(0, gui->height() - widget->height());
    if (widget->isVisible()) {
        widget->move(targetPos);
        widget->raise();
        return true;
    }

    QPropertyAnimation* animation = new QPropertyAnimation(widget, "pos", widget);
    animation->setDuration(300);
    animation->setStartValue(QPoint(0, gui->height()));
    animation->setEndValue(targetPos);
    animation->setEasingCurve(QEasingCurve::OutQuad);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
    widget->show();
    widget->raise();
    return true;
}

void closeDialog(QDialog* widget, KRISTATECHGUI* gui)
{
    if (!widget || !gui) return;
    for (QPropertyAnimation* anim : widget->findChildren<QPropertyAnimation*>()) {
        anim->stop();
        anim->deleteLater();
    }
    QPropertyAnimation* animation = new QPropertyAnimation(widget, "pos", widget);
    animation->setDuration(300);
    animation->setStartValue(widget->pos());
    animation->setEndValue(QPoint(0, gui->height() + 100));
    animation->setEasingCurve(QEasingCurve::OutQuad);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void openDialogFullScreen(QWidget* parent, QWidget* dialog)
{
    dialog->setWindowFlags(Qt::CustomizeWindowHint);
    dialog->move(0, 0);
    dialog->show();
    dialog->activateWindow();
    dialog->resize(parent->width(), parent->height());
}

bool openDialogWithOpaqueBackgroundY(QDialog* widget, KRISTATECHGUI* gui, double posX, int posY)
{
    Q_UNUSED(posX);
    Q_UNUSED(posY);
    if (gui) {
        gui->showHide(true);
        if (widget->parentWidget() != gui) {
            widget->setParent(gui);
        }
    }
    widget->setWindowFlags(Qt::CustomizeWindowHint);
    widget->setAttribute(Qt::WA_TranslucentBackground, true);

    if (gui) {
        const int maxW = std::max(320, gui->width() - 32);
        const int maxH = std::max(240, gui->height() - 32);
        if (widget->width() > maxW || widget->height() > maxH) {
            widget->resize(std::min(widget->width(), maxW), std::min(widget->height(), maxH));
        }
    }

    const int xPos = gui ? std::max(16, (gui->width() - widget->width()) / 2) : 0;
    const int yPos = gui ? std::max(16, (gui->height() - widget->height()) / 2) : 0;
    QPropertyAnimation* animation = new QPropertyAnimation(widget, "pos");
    animation->setDuration(240);
    animation->setStartValue(QPoint(xPos, gui ? std::min(gui->height(), yPos + 80) : yPos));
    animation->setEndValue(QPoint(xPos, yPos));
    animation->setEasingCurve(QEasingCurve::OutQuad);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
    widget->raise();
    widget->activateWindow();
    bool res = widget->exec();
    if (gui) gui->showHide(false);
    return res;
}

bool openDialogWithOpaqueBackground(QDialog* widget, KRISTATECHGUI* gui, double posX)
{
    return openDialogWithOpaqueBackgroundY(widget, gui, posX, 5);
}

bool showThemedConfirm(QWidget* parent, const QString& title, const QString& message, const QString& okText, const QString& cancelText, bool isDanger)
{
    const QString okLabel = okText.trimmed().isEmpty() ? QObject::tr("OK") : okText;
    const QString cancelLabel = cancelText;
    KRISTATECHGUI* gui = qobject_cast<KRISTATECHGUI*>(parent ? parent->window() : nullptr);
    if (gui && gui->isVisible()) {
        DefaultDialog* dialog = new DefaultDialog(gui);
        dialog->setText(title, message, okLabel, cancelLabel);
        if (isDanger) dialog->setDanger(true);
        bool res = openDialogWithOpaqueBackgroundY(dialog, gui, 3, 5);
        dialog->deleteLater();
        return res;
    }

    DefaultDialog dialog(parent);
    dialog.setText(title, message, okLabel, cancelLabel);
    if (isDanger) dialog.setDanger(true);
    dialog.setWindowTitle(title);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setAttribute(Qt::WA_TranslucentBackground, true);
    dialog.exec();
    return dialog.isOk;
}

void showThemedNotice(QWidget* parent, const QString& title, const QString& message, bool isDanger, const QString& okText)
{
    showThemedConfirm(parent, title, message, okText, QString(), isDanger);
}

bool openDialogWithOpaqueBackgroundFullScreen(QDialog* widget, KRISTATECHGUI* gui)
{
    if (gui) {
        gui->showHide(true);
        if (widget->parentWidget() != gui) {
            widget->setParent(gui);
        }
    }
    widget->setWindowFlags(Qt::CustomizeWindowHint);
    widget->setAttribute(Qt::WA_TranslucentBackground, true);

    if (gui) {
        widget->resize(gui->width(), gui->height());
    }

    QPropertyAnimation* animation = new QPropertyAnimation(widget, "pos");
    animation->setDuration(240);
    int xPos = 0;
    animation->setStartValue(QPoint(xPos, gui ? gui->height() : 0));
    animation->setEndValue(QPoint(xPos, 0));
    animation->setEasingCurve(QEasingCurve::OutQuad);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
    widget->raise();
    widget->activateWindow();
    bool res = widget->exec();
    if (gui) gui->showHide(false);
    return res;
}

QPixmap encodeToQr(QString str, QString& errorStr, QColor qrColor)
{
    if (!str.isEmpty()) {
        // limit URI length
        if (str.length() > MAX_URI_LENGTH) {
            errorStr = "Resulting URI too long, try to reduce the text for label / message.";
            return QPixmap();
        } else {
            QRcode* code = QRcode_encodeString(str.toUtf8().constData(), 0, QR_ECLEVEL_L, QR_MODE_8, 1);
            if (!code) {
                errorStr = "Error encoding URI into QR Code.";
                return QPixmap();
            }
            QImage myImage = QImage(code->width + 8, code->width + 8, QImage::Format_RGB32);
            myImage.fill(0xffffff);
            unsigned char* p = code->data;
            for (int y = 0; y < code->width; y++) {
                for (int x = 0; x < code->width; x++) {
                    myImage.setPixel(x + 4, y + 4, ((*p & 1) ? qrColor.rgb() : 0xffffff));
                    p++;
                }
            }
            QRcode_free(code);
            return QPixmap::fromImage(myImage);
        }
    }
    return QPixmap();
}

void setFilterAddressBook(QComboBox* filter)
{
    initComboBox(filter);
    filter->addItem(QObject::tr("All"), "");
    filter->addItem(QObject::tr("Receiving"), AddressTableModel::Receive);
    filter->addItem(QObject::tr("Contacts"), AddressTableModel::Send);
}

void setSortTx(QComboBox* filter)
{
    // Sort Transactions
    initComboBox(filter);
    filter->addItem(QObject::tr("Date desc"), SortTx::DATE_DESC);
    filter->addItem(QObject::tr("Date asc"), SortTx::DATE_ASC);
    filter->addItem(QObject::tr("Amount desc"), SortTx::AMOUNT_DESC);
    filter->addItem(QObject::tr("Amount asc"), SortTx::AMOUNT_ASC);
}

void setSortTxTypeFilter(QComboBox* filter)
{
    initComboBox(filter);
    filter->addItem(QObject::tr("All"), TransactionFilterProxy::ALL_TYPES);
    filter->addItem(QObject::tr("Received"), TransactionFilterProxy::TYPE(TransactionRecord::RecvWithAddress) | TransactionFilterProxy::TYPE(TransactionRecord::RecvFromOther));
    filter->addItem(QObject::tr("Sent"), TransactionFilterProxy::TYPE(TransactionRecord::SendToAddress) | TransactionFilterProxy::TYPE(TransactionRecord::SendToOther));
    filter->addItem(QObject::tr("Mined"), TransactionFilterProxy::TYPE(TransactionRecord::Generated) | TransactionFilterProxy::TYPE(TransactionRecord::AdamReward));
    filter->addItem(QObject::tr("Minted"), TransactionFilterProxy::TYPE(TransactionRecord::StakeMint));
    filter->addItem(QObject::tr("MN reward"), TransactionFilterProxy::TYPE(TransactionRecord::MNReward) | TransactionFilterProxy::TYPE(TransactionRecord::LlmqReward));
    filter->addItem(QObject::tr("To yourself"), TransactionFilterProxy::TYPE(TransactionRecord::SendToSelf));
    filter->addItem(QObject::tr("Mining Registration"), TransactionFilterProxy::TYPE(TransactionRecord::MinerRegistration));
}

void setupSettings(QSettings* settings)
{
    if (!settings->contains("lightTheme")) {
        setTheme(false);
    }
}

QSettings* settings = nullptr;

QSettings* getSettings()
{
    if (!settings) {
        settings = new QSettings();
        // Setup initial values if them are not there
        setupSettings(settings);
    }

    return settings;
}

bool isLightTheme()
{
    return getSettings()->value("lightTheme", true).toBool();
}

void setTheme(bool isLight)
{
    QSettings* settings = getSettings();
    settings->setValue("theme", isLight ? "default" : "default-dark");
    settings->setValue("lightTheme", isLight);
}


// Style

void updateStyle(QWidget* widget)
{
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}


QColor getRowColor(bool isLightTheme, bool isHovered, bool isSelected)
{
    if (isLightTheme) {
        if (isSelected) {
            return QColor("#f9f9f9");
        } else if (isHovered) {
            return QColor("#f9f9f9");
        } else {
            return QColor("#ffffff");
        }
    } else {
        if (isSelected) {
            return QColor(118, 149, 205, 80); // Logo Slate-Blue (#7695CD) selected highlight
        } else if (isHovered) {
            return QColor(118, 149, 205, 40); // Logo Slate-Blue (#7695CD) hover highlight
        } else {
            return QColor("#121b2d"); // Dark night-blue row background
        }
    }
}

void initComboBox(QComboBox* combo, QString cssClass)
{
    setCssProperty(combo, cssClass);
    combo->setView(new QListView());
    combo->setMaxVisibleItems(10);
    combo->setCursor(Qt::PointingHandCursor);
}

QString getCustomItem(QWidget* parent, const QString& title, const QString& label, const QStringList& items, int current, bool editable, bool* ok)
{
    QInputDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setLabelText(label);
    dialog.setComboBoxItems(items);
    dialog.setComboBoxEditable(editable);
    dialog.setProperty("cssClass", "container-dialog");

    QComboBox* combo = dialog.findChild<QComboBox*>();
    if (combo) {
        initComboBox(combo, "btn-combo-dialog");
        combo->setCurrentIndex(current);
    }

    if (parent) {
        dialog.setStyleSheet(parent->styleSheet());
    }

    bool res = dialog.exec();
    if (ok) {
        *ok = res;
    }
    return res ? dialog.textValue() : QString();
}

void fillAddressSortControls(QComboBox* boxType, QComboBox* boxOrder)
{
    // Sort Type
    initComboBox(boxType, "btn-combo-small");
    boxType->addItem(QObject::tr("by Label"), AddressTableModel::Label);
    boxType->addItem(QObject::tr("by Address"), AddressTableModel::Address);
    boxType->addItem(QObject::tr("by Date"), AddressTableModel::Date);
    boxType->setCurrentIndex(0);
    // Sort Order
    initComboBox(boxOrder, "btn-combo-small");
    boxOrder->addItem("asc", Qt::AscendingOrder);
    boxOrder->addItem("desc", Qt::DescendingOrder);
    boxOrder->setCurrentIndex(0);
}

void initCssEditLine(QLineEdit* edit, bool isDialog)
{
    if (isDialog)
        setCssEditLineDialog(edit, true, false);
    else
        setCssEditLine(edit, true, false);
    setShadow(edit);
    edit->setAttribute(Qt::WA_MacShowFocusRect, 0);
}

void setCssEditLine(QLineEdit* edit, bool isValid, bool forceUpdate)
{
    setCssProperty(edit, isValid ? "edit-primary" : "edit-primary-error", forceUpdate);
}

void setCssEditLineDialog(QLineEdit* edit, bool isValid, bool forceUpdate)
{
    setCssProperty(edit, isValid ? "edit-primary-dialog" : "edit-primary-dialog-error", forceUpdate);
}

void setShadow(QWidget* edit)
{
    QGraphicsDropShadowEffect* shadowEffect = new QGraphicsDropShadowEffect();
    shadowEffect->setColor(QColor(0, 0, 0, 22));
    shadowEffect->setXOffset(0);
    shadowEffect->setYOffset(3);
    shadowEffect->setBlurRadius(6);
    edit->setGraphicsEffect(shadowEffect);
}

void setCssBtnPrimary(QPushButton* btn, bool forceUpdate)
{
    setCssProperty(btn, "btn-primary", forceUpdate);
}

void setCssBtnSecondary(QPushButton* btn, bool forceUpdate)
{
    setCssProperty(btn, "btn-secundary", forceUpdate);
}

void setCssTextBodyDialog(std::initializer_list<QWidget*> args)
{
    for (QWidget* w : args) {
        setCssTextBodyDialog(w);
    }
}

void setCssTextBodyDialog(QWidget* widget)
{
    setCssProperty(widget, "text-body1-dialog", false);
}

void setCssTitleScreen(QLabel* label)
{
    setCssProperty(label, "text-title-screen", false);
}

void setCssSubtitleScreen(QWidget* wid)
{
    setCssProperty(wid, "text-subtitle", false);
}

void setCssProperty(std::initializer_list<QWidget*> args, QString value)
{
    for (QWidget* w : args) {
        setCssProperty(w, value);
    }
}

void setCssProperty(QWidget* wid, QString value, bool forceUpdate)
{
    wid->setProperty("cssClass", value);
    forceUpdateStyle(wid, forceUpdate);
}

void forceUpdateStyle(QWidget* widget, bool forceUpdate)
{
    if (forceUpdate)
        updateStyle(widget);
}

void forceUpdateStyle(std::initializer_list<QWidget*> args)
{
    for (QWidget* w : args) {
        forceUpdateStyle(w, true);
    }
}

namespace {
class ClearButtonIconEngine : public QIconEngine
{
public:
    void paint(QPainter* p, const QRect& rect, QIcon::Mode mode, QIcon::State) override
    {
        if (!p || rect.isEmpty()) return;
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);

        const QRectF r(rect);
        const qreal side = std::min(r.width(), r.height());
        const QPointF center = r.center();
        const qreal radius = side * 0.41;

        const bool light = isLightTheme();
        QColor circleFill;
        if (mode == QIcon::Active || mode == QIcon::Selected) {
            circleFill = QColor(light ? "#5576B2" : "#6C8EC9");
        } else if (mode == QIcon::Disabled) {
            circleFill = QColor(light ? "#CBD5E1" : "#334155");
        } else {
            circleFill = QColor(light ? "#94A3B8" : "#475569");
        }

        p->setPen(Qt::NoPen);
        p->setBrush(circleFill);
        p->drawEllipse(center, radius, radius);

        const qreal arm = radius * 0.42;
        QPen crossPen(QColor(QStringLiteral("#FFFFFF")),
                      std::max(1.5, side * 0.105),
                      Qt::SolidLine,
                      Qt::RoundCap,
                      Qt::RoundJoin);
        p->setPen(crossPen);
        p->drawLine(QPointF(center.x() - arm, center.y() - arm), QPointF(center.x() + arm, center.y() + arm));
        p->drawLine(QPointF(center.x() + arm, center.y() - arm), QPointF(center.x() - arm, center.y() + arm));
        p->restore();
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        return renderScaled(size, mode, state, 1.0);
    }

    void virtual_hook(int id, void* data) override
    {
        if (id == QIconEngine::ScaledPixmapHook && data) {
            auto* arg = static_cast<ScaledPixmapArgument*>(data);
            const qreal dpr = (arg->scale > 0.0) ? arg->scale : 1.0;
            arg->pixmap = renderScaled(arg->size, arg->mode, arg->state, dpr);
            return;
        }
        QIconEngine::virtual_hook(id, data);
    }

    QIconEngine* clone() const override
    {
        return new ClearButtonIconEngine();
    }

private:
    QPixmap renderScaled(const QSize& logicalSize, QIcon::Mode mode, QIcon::State state, qreal dpr)
    {
        const QSize sz = logicalSize.isValid() && !logicalSize.isEmpty() ? logicalSize : QSize(16, 16);
        QImage img(std::max(1, qRound(sz.width() * dpr)),
                   std::max(1, qRound(sz.height() * dpr)),
                   QImage::Format_ARGB32_Premultiplied);
        img.setDevicePixelRatio(dpr);
        img.fill(Qt::transparent);
        QPainter painter(&img);
        paint(&painter, QRect(QPoint(0, 0), sz), mode, state);
        painter.end();
        return QPixmap::fromImage(img);
    }
};
} // namespace

QIcon makeVectorClearIcon()
{
    return QIcon(new ClearButtonIconEngine());
}

#include <QJsonDocument>
#include <QJsonParseError>

QString highlightJsonHtml(const QString& text, bool isLight)
{
    const QString trimmed = text.trimmed();
    if (!trimmed.startsWith('{') && !trimmed.startsWith('[')) {
        return QString();
    }
    QJsonParseError err;
    QJsonDocument::fromJson(trimmed.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) {
        return QString();
    }

    const QString cKey = isLight ? QStringLiteral("#2563EB") : QStringLiteral("#7DD3FC");
    const QString cStr = isLight ? QStringLiteral("#059669") : QStringLiteral("#6EE7B7");
    const QString cNum = isLight ? QStringLiteral("#D97706") : QStringLiteral("#FCD34D");
    const QString cBool = isLight ? QStringLiteral("#7C3AED") : QStringLiteral("#DDD6FE");
    const QString cPunct = isLight ? QStringLiteral("#475569") : QStringLiteral("#CBD5E1");

    QString out = QStringLiteral("<pre style=\"margin:2px 0px; font-family:monospace; white-space:pre-wrap;\">");
    const int n = text.size();
    int i = 0;
    while (i < n) {
        const QChar ch = text.at(i);
        if (ch == '"') {
            int j = i + 1;
            while (j < n) {
                if (text.at(j) == '\\') {
                    j += 2;
                } else if (text.at(j) == '"') {
                    ++j;
                    break;
                } else {
                    ++j;
                }
            }
            int k = j;
            while (k < n && text.at(k).isSpace()) ++k;
            const bool isKey = (k < n && text.at(k) == ':');
            const QString tok = text.mid(i, j - i).toHtmlEscaped();
            if (isKey) {
                out += QString("<span style=\"color:%1; font-weight:bold;\">%2</span>").arg(cKey, tok);
            } else {
                out += QString("<span style=\"color:%1;\">%2</span>").arg(cStr, tok);
            }
            i = j;
        } else if (ch == '{' || ch == '}' || ch == '[' || ch == ']' || ch == ':' || ch == ',') {
            out += QString("<span style=\"color:%1;\">%2</span>").arg(cPunct, QString(ch).toHtmlEscaped());
            ++i;
        } else if (ch == '-' || ch.isDigit()) {
            int j = i + 1;
            while (j < n && (text.at(j).isDigit() || text.at(j) == '.' || text.at(j) == 'e' || text.at(j) == 'E' || text.at(j) == '+' || text.at(j) == '-')) {
                ++j;
            }
            out += QString("<span style=\"color:%1;\">%2</span>").arg(cNum, text.mid(i, j - i).toHtmlEscaped());
            i = j;
        } else if (ch.isLetter()) {
            int j = i + 1;
            while (j < n && text.at(j).isLetter()) ++j;
            const QString word = text.mid(i, j - i);
            if (word == QLatin1String("true") || word == QLatin1String("false") || word == QLatin1String("null")) {
                out += QString("<span style=\"color:%1; font-weight:bold;\">%2</span>").arg(cBool, word);
            } else {
                out += word.toHtmlEscaped();
            }
            i = j;
        } else {
            out += QString(ch).toHtmlEscaped();
            ++i;
        }
    }
    out += QStringLiteral("</pre>");
    return out;
}

QString highlightRpcHelpHtml(const QString& text, bool isLight)
{
    const QString trimmedAll = text.trimmed();
    if (trimmedAll.isEmpty()) return QString();

    const bool isHelpList = trimmedAll.startsWith(QLatin1String("== ")) ||
                            trimmedAll.contains(QLatin1String("\n== "));
    const bool isHelpCmd = trimmedAll.contains(QLatin1String("\nArguments:\n")) ||
                           trimmedAll.contains(QLatin1String("\nResult")) ||
                           trimmedAll.contains(QLatin1String("\nExamples:\n"));
    if (!isHelpList && !isHelpCmd) return QString();

    const QString cHeader = isLight ? QStringLiteral("#C2410C") : QStringLiteral("#FCA5A5");
    const QString cCmd    = isLight ? QStringLiteral("#3B5998") : QStringLiteral("#93C5FD");
    const QString cStr    = isLight ? QStringLiteral("#15803D") : QStringLiteral("#6EE7B7");
    const QString cOpt    = isLight ? QStringLiteral("#7C3AED") : QStringLiteral("#DDD6FE");
    const QString cMuted  = isLight ? QStringLiteral("#475569") : QStringLiteral("#CBD5E1");

    auto colorArgs = [&](const QString& args) -> QString {
        QString res;
        int i = 0;
        const int n = args.size();
        int parenDepth = 0;
        while (i < n) {
            const QChar ch = args.at(i);
            if (ch == QLatin1Char('"')) {
                int j = i + 1;
                while (j < n && args.at(j) != QLatin1Char('"')) {
                    if (args.at(j) == QLatin1Char('\\') && j + 1 < n) ++j;
                    ++j;
                }
                if (j < n) ++j;
                res += QString("<span style=\"color:%1;\">%2</span>")
                           .arg(cStr, args.mid(i, j - i).toHtmlEscaped());
                i = j;
            } else if (ch == QLatin1Char('(') || ch == QLatin1Char('[') || ch == QLatin1Char('|')) {
                if (ch == QLatin1Char('(') || ch == QLatin1Char('[')) ++parenDepth;
                res += QString("<span style=\"color:%1;\">%2</span>").arg(cMuted, QString(ch).toHtmlEscaped());
                ++i;
            } else if (ch == QLatin1Char(')') || ch == QLatin1Char(']')) {
                if (parenDepth > 0) --parenDepth;
                res += QString("<span style=\"color:%1;\">%2</span>").arg(cMuted, QString(ch).toHtmlEscaped());
                ++i;
            } else if (ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('-')) {
                int j = i + 1;
                while (j < n && (args.at(j).isLetterOrNumber() || args.at(j) == QLatin1Char('_') || args.at(j) == QLatin1Char('-'))) {
                    ++j;
                }
                const QString tok = args.mid(i, j - i);
                const QString& col = (parenDepth > 0) ? cOpt : cHeader;
                res += QString("<span style=\"color:%1;\">%2</span>").arg(col, tok.toHtmlEscaped());
                i = j;
            } else {
                res += QString(ch).toHtmlEscaped();
                ++i;
            }
        }
        return res;
    };

    QString out;
    out.reserve(text.size() * 3 + 128);
    out += QStringLiteral("<pre style=\"margin:2px 0px; font-family:monospace; white-space:pre-wrap;\">");

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int li = 0; li < lines.size(); ++li) {
        if (li > 0) out += QLatin1Char('\n');
        const QString& line = lines.at(li);
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;

        if (trimmed.startsWith(QLatin1String("==")) && trimmed.endsWith(QLatin1String("=="))) {
            out += QString("<span style=\"color:%1; font-weight:bold;\">%2</span>")
                       .arg(cHeader, line.toHtmlEscaped());
            continue;
        }

        if (trimmed == QLatin1String("Arguments:") ||
            trimmed == QLatin1String("Examples:") ||
            (trimmed.startsWith(QLatin1String("Result")) && trimmed.endsWith(QLatin1Char(':')))) {
            out += QString("<span style=\"color:%1; font-weight:bold;\">%2</span>")
                       .arg(cHeader, line.toHtmlEscaped());
            continue;
        }

        if (trimmed.startsWith(QLatin1String("> "))) {
            const int gtIdx = line.indexOf(QLatin1Char('>'));
            out += line.left(gtIdx).toHtmlEscaped();
            out += QString("<span style=\"color:%1; font-weight:bold;\">&gt;</span>").arg(cMuted);
            out += QString("<span style=\"color:%1;\">%2</span>")
                       .arg(cCmd, line.mid(gtIdx + 1).toHtmlEscaped());
            continue;
        }

        // Numbered argument line in single-command help: "1. \"hash\"   (string, required) ..."
        if (isHelpCmd && trimmed.at(0).isDigit()) {
            const int dotIdx = line.indexOf(QLatin1String(". "));
            if (dotIdx > 0 && dotIdx <= 4) {
                bool okNum = false;
                line.left(dotIdx).trimmed().toInt(&okNum);
                if (okNum) {
                    const int pStart = dotIdx + 2;
                    int pEnd = pStart;
                    while (pEnd < line.size() && !line.at(pEnd).isSpace()) ++pEnd;
                    const QString paramTok = line.mid(pStart, pEnd - pStart);
                    const QString restLine = line.mid(pEnd);
                    out += QString("<span style=\"color:%1; font-weight:bold;\">%2</span>")
                               .arg(cHeader, line.left(pStart).toHtmlEscaped());
                    const QString& pCol = paramTok.startsWith(QLatin1Char('"')) ? cStr : cCmd;
                    out += QString("<span style=\"color:%1; font-weight:bold;\">%2</span>")
                               .arg(pCol, paramTok.toHtmlEscaped());
                    const int openP = restLine.indexOf(QLatin1Char('('));
                    const int closeP = (openP >= 0) ? restLine.indexOf(QLatin1Char(')'), openP) : -1;
                    if (openP >= 0 && closeP > openP) {
                        out += restLine.left(openP).toHtmlEscaped();
                        out += QString("<span style=\"color:%1;\">%2</span>")
                                   .arg(cOpt, restLine.mid(openP, closeP - openP + 1).toHtmlEscaped());
                        out += restLine.mid(closeP + 1).toHtmlEscaped();
                    } else {
                        out += restLine.toHtmlEscaped();
                    }
                    continue;
                }
            }
        }

        // Command signature line: starts at column 0 with lowercase RPC command name
        if (!line.at(0).isSpace() && line.at(0).isLower()) {
            int spIdx = 0;
            while (spIdx < line.size() && (line.at(spIdx).isLower() || line.at(spIdx).isDigit() || line.at(spIdx) == QLatin1Char('_'))) {
                ++spIdx;
            }
            if (spIdx > 0 && (spIdx == line.size() || line.at(spIdx) == QLatin1Char(' '))) {
                const QString cmdName = line.left(spIdx);
                const QString argsPart = line.mid(spIdx);
                out += QString("<span style=\"color:%1; font-weight:bold;\">%2</span>")
                           .arg(cCmd, cmdName.toHtmlEscaped());
                if (!argsPart.isEmpty()) {
                    out += colorArgs(argsPart);
                }
                continue;
            }
        }

        out += line.toHtmlEscaped();
    }

    out += QStringLiteral("</pre>");
    return out;
}

QString highlightYamlHtml(const QString& text, bool isLight)
{
    if (text.isEmpty()) return QString();

    const QString cTopKey  = isLight ? QStringLiteral("#C2410C") : QStringLiteral("#FCA5A5");
    const QString cKey     = isLight ? QStringLiteral("#7C3AED") : QStringLiteral("#D8B4FE");
    const QString cStr     = isLight ? QStringLiteral("#0F766E") : QStringLiteral("#6EE7B7");
    const QString cNum     = isLight ? QStringLiteral("#BE185D") : QStringLiteral("#FCD34D");
    const QString cBool    = isLight ? QStringLiteral("#2563EB") : QStringLiteral("#7DD3FC");
    const QString cDash    = isLight ? QStringLiteral("#D97706") : QStringLiteral("#FDE047");
    const QString cPunct   = isLight ? QStringLiteral("#475569") : QStringLiteral("#CBD5E1");
    const QString cComment = isLight ? QStringLiteral("#64748B") : QStringLiteral("#94A3B8");

    auto colorScalar = [&](const QString& rawVal) -> QString {
        const QString val = rawVal.trimmed();
        if (val.isEmpty()) return QString();
        if (val == QLatin1String("true") || val == QLatin1String("false") ||
            val == QLatin1String("null") || val == QLatin1String("~")) {
            return QString("<span style=\"color:%1; font-weight:bold;\">%2</span>").arg(cBool, val.toHtmlEscaped());
        }
        if (val == QLatin1String("{}") || val == QLatin1String("[]")) {
            return QString("<span style=\"color:%1;\">%2</span>").arg(cPunct, val.toHtmlEscaped());
        }
        if ((val.startsWith(QLatin1Char('"')) && val.endsWith(QLatin1Char('"')) && val.size() >= 2) ||
            (val.startsWith(QLatin1Char('\'')) && val.endsWith(QLatin1Char('\'')) && val.size() >= 2)) {
            return QString("<span style=\"color:%1;\">%2</span>").arg(cStr, val.toHtmlEscaped());
        }
        bool isNum = false;
        val.toDouble(&isNum);
        if (isNum) {
            return QString("<span style=\"color:%1;\">%2</span>").arg(cNum, val.toHtmlEscaped());
        }
        return QString("<span style=\"color:%1;\">%2</span>").arg(cStr, val.toHtmlEscaped());
    };

    QString out;
    out.reserve(text.size() * 3 + 128);
    out += QStringLiteral("<pre style=\"margin:2px 0px; font-family:monospace; white-space:pre-wrap;\">");

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int li = 0; li < lines.size(); ++li) {
        if (li > 0) out += QLatin1Char('\n');
        const QString& line = lines.at(li);
        int pos = 0;
        const int len = line.size();
        while (pos < len && (line.at(pos) == QLatin1Char(' ') || line.at(pos) == QLatin1Char('\t'))) {
            out += line.at(pos);
            ++pos;
        }
        if (pos >= len) continue;

        if (line.at(pos) == QLatin1Char('#')) {
            out += QString("<span style=\"color:%1;\">%2</span>").arg(cComment, line.mid(pos).toHtmlEscaped());
            continue;
        }

        const int indent = pos;
        if (line.at(pos) == QLatin1Char('-') && (pos + 1 == len || line.at(pos + 1) == QLatin1Char(' '))) {
            out += QString("<span style=\"color:%1; font-weight:bold;\">-</span>").arg(cDash);
            ++pos;
            while (pos < len && line.at(pos) == QLatin1Char(' ')) {
                out += QLatin1Char(' ');
                ++pos;
            }
            if (pos >= len) continue;
        }

        const QString rest = line.mid(pos);
        // Find key: separator (colon followed by space or end-of-line, outside quotes)
        int colonIdx = -1;
        bool inSQuote = false;
        bool inDQuote = false;
        for (int k = 0; k < rest.size(); ++k) {
            const QChar c = rest.at(k);
            if (c == QLatin1Char('\'') && !inDQuote) inSQuote = !inSQuote;
            else if (c == QLatin1Char('"') && !inSQuote) inDQuote = !inDQuote;
            else if (c == QLatin1Char(':') && !inSQuote && !inDQuote) {
                if (k + 1 == rest.size() || rest.at(k + 1) == QLatin1Char(' ')) {
                    colonIdx = k;
                    break;
                }
            }
        }

        if (colonIdx > 0) {
            const QString key = rest.left(colonIdx);
            const QString after = rest.mid(colonIdx + 1);
            const QString& keyColor = (indent == 0) ? cTopKey : cKey;
            out += QString("<span style=\"color:%1; font-weight:bold;\">%2</span><span style=\"color:%3;\">:</span>")
                       .arg(keyColor, key.toHtmlEscaped(), cPunct);
            if (!after.trimmed().isEmpty()) {
                int sp = 0;
                while (sp < after.size() && after.at(sp) == QLatin1Char(' ')) {
                    out += QLatin1Char(' ');
                    ++sp;
                }
                out += colorScalar(after.mid(sp));
            }
        } else {
            out += colorScalar(rest);
        }
    }

    out += QStringLiteral("</pre>");
    return out;
}

void styleLineEditClearButton(QLineEdit* edit)
{
    if (!edit) return;
    const QIcon clearIcon = makeVectorClearIcon();
    for (QAction* act : edit->findChildren<QAction*>()) {
        if (act && act->objectName().contains(QStringLiteral("clear"), Qt::CaseInsensitive)) {
            act->setIcon(clearIcon);
        }
    }
    for (QToolButton* btn : edit->findChildren<QToolButton*>()) {
        if (btn) {
            btn->setIcon(clearIcon);
            btn->setCursor(Qt::PointingHandCursor);
            if (btn->toolTip().isEmpty()) {
                btn->setToolTip(QObject::tr("Clear input"));
            }
            btn->update();
        }
    }
}

QString buildSettingsPageStyleSheet(bool isLight)
{
    const QString cardBg      = isLight ? QStringLiteral("#FFFFFF") : QStringLiteral("#131B2E");
    const QString cardBrd     = isLight ? QStringLiteral("#CBD5E1") : QStringLiteral("#253554");
    const QString divider     = isLight ? QStringLiteral("#E2E8F0") : QStringLiteral("#1E2C46");
    const QString titleCol    = isLight ? QStringLiteral("#1E293B") : QStringLiteral("#F1F5F9");
    const QString subCol      = isLight ? QStringLiteral("#475569") : QStringLiteral("#CBD5E1");
    const QString descCol     = isLight ? QStringLiteral("#64748B") : QStringLiteral("#94A3B8");
    const QString inputBg     = isLight ? QStringLiteral("#F8FAFC") : QStringLiteral("#0D1424");
    const QString inputBrd    = isLight ? QStringLiteral("#B6C5DC") : QStringLiteral("#33486E");
    const QString inputFocus  = isLight ? QStringLiteral("#FFFFFF") : QStringLiteral("#0A101E");
    const QString inputDisBg  = isLight ? QStringLiteral("#F1F5F9") : QStringLiteral("#111A2E");
    const QString inputDisBrd = isLight ? QStringLiteral("#CBD5E1") : QStringLiteral("#253554");
    const QString inputDisFg  = isLight ? QStringLiteral("#94A3B8") : QStringLiteral("#64748B");
    const QString primBg      = isLight ? QStringLiteral("#EEF3FA") : QStringLiteral("#1B2B4A");
    const QString primFg      = isLight ? QStringLiteral("#3E5B94") : QStringLiteral("#A8C1EE");
    const QString primBrd     = QStringLiteral("#7695CD");
    const QString primHov     = isLight ? QStringLiteral("#DCE7F7") : QStringLiteral("#243860");
    const QString primHovFg   = isLight ? QStringLiteral("#2F497A") : QStringLiteral("#D5E3FA");
    const QString primHovBrd  = isLight ? QStringLiteral("#5576B2") : QStringLiteral("#93B4F0");
    const QString primPress   = isLight ? QStringLiteral("#CEDEF4") : QStringLiteral("#16233D");
    const QString secBg       = isLight ? QStringLiteral("#F1F5F9") : QStringLiteral("#1C2942");
    const QString secFg       = isLight ? QStringLiteral("#334155") : QStringLiteral("#CBD5E1");
    const QString secBrd      = isLight ? QStringLiteral("#CBD5E1") : QStringLiteral("#33486E");
    const QString secHov      = isLight ? QStringLiteral("#E2E8F0") : QStringLiteral("#26385A");
    const QString dropArrow   = isLight ? QStringLiteral("://ic-arrow-drop-down") : QStringLiteral("://ic-arrow-drop-down-white");
    const QString warnBdgBg   = isLight ? QStringLiteral("#FEF2F2") : QStringLiteral("#2E1619");
    const QString warnBdgFg   = isLight ? QStringLiteral("#C94A3E") : QStringLiteral("#FCA5A5");
    const QString warnBdgBrd  = isLight ? QStringLiteral("#F5B5AE") : QStringLiteral("#7F2D28");
    const QString warnBoxBg   = isLight ? QStringLiteral("#FEF2F2") : QStringLiteral("#2A1519");
    const QString warnBoxBrd  = isLight ? QStringLiteral("#F5B5AE") : QStringLiteral("#6E2B2A");
    const QString warnBoxL    = isLight ? QStringLiteral("#C94A3E") : QStringLiteral("#F5897C");
    const QString warnBoxFg   = isLight ? QStringLiteral("#991B1B") : QStringLiteral("#FECACA");
    const QString infoBdgBrd  = isLight ? QStringLiteral("#B8CBE8") : QStringLiteral("#355184");
    const QString infoBoxBg   = isLight ? QStringLiteral("#EEF3FA") : QStringLiteral("#182642");
    const QString infoBoxBrd  = isLight ? QStringLiteral("#B8CBE8") : QStringLiteral("#2E4775");
    const QString infoBoxL    = isLight ? QStringLiteral("#5576B2") : QStringLiteral("#7695CD");
    const QString infoBoxFg   = isLight ? QStringLiteral("#3E5B94") : QStringLiteral("#CBD5E1");

    const QString containerCss = QString(
        "QWidget#settingsPageRoot, QScrollArea#settingsScroll, QWidget#settingsScrollViewport, QWidget#settingsScrollContent {"
        "  background-color: transparent; border: none;"
        "}"
        "QFrame[settingsCard=\"true\"] {"
        "  background-color: %1; border: 1px solid %2; border-radius: 10px;"
        "}"
        "QFrame[settingsDivider=\"true\"] {"
        "  background-color: %3; border: none;"
        "}"
        "QFrame[settingsAlertBox=\"warn\"] {"
        "  background-color: %4; border: 1px solid %5; border-left: 3px solid %6; border-radius: 8px;"
        "}"
        "QFrame[settingsAlertBox=\"info\"] {"
        "  background-color: %7; border: 1px solid %8; border-left: 3px solid %9; border-radius: 8px;"
        "}"
    ).arg(cardBg, cardBrd, divider, warnBoxBg, warnBoxBrd, warnBoxL, infoBoxBg, infoBoxBrd, infoBoxL);

    const QString textCss = QString(
        "QLabel[settingsPageTitle=\"true\"] { color: %1; font-size: 22px; font-weight: 500; background: transparent; border: none; }"
        "QLabel[settingsPageSubtitle=\"true\"] { color: %2; font-size: 13px; font-weight: 400; background: transparent; border: none; }"
        "QLabel[settingsCardTitle=\"true\"] { color: %1; font-size: 14px; font-weight: 600; background: transparent; border: none; }"
        "QLabel[settingsRowTitle=\"true\"] { color: %1; font-size: 13px; font-weight: 600; background: transparent; border: none; }"
        "QLabel[settingsRowDesc=\"true\"] { color: %3; font-size: 12px; font-weight: 400; background: transparent; border: none; }"
        "QLabel[settingsInfoKey=\"true\"] { color: %3; font-size: 13px; font-weight: 500; background: transparent; border: none; }"
        "QLabel[settingsInfoVal=\"true\"] { color: %1; font-size: 13px; font-weight: 600; background: transparent; border: none; }"
        "QLabel[settingsInfoMono=\"true\"] { color: %2; font-size: 12px; font-family: monospace; font-weight: 500; background: transparent; border: none; }"
    ).arg(titleCol, subCol, descCol)
    + QString(
        "QLabel[settingsBadge=\"warn\"] { background-color: %1; color: %2; border: 1px solid %3; border-radius: 11px; padding: 3px 10px; font-size: 11px; font-weight: 700; }"
        "QLabel[settingsBadge=\"active\"] { background-color: %4; color: %5; border: 1px solid %6; border-radius: 11px; padding: 3px 10px; font-size: 11px; font-weight: 700; }"
        "QLabel[settingsAlertText=\"warn\"] { color: %7; font-size: 12px; font-weight: 500; background: transparent; border: none; }"
        "QLabel[settingsAlertText=\"info\"] { color: %8; font-size: 12px; font-weight: 500; background: transparent; border: none; }"
    ).arg(warnBdgBg, warnBdgFg, warnBdgBrd, primBg, primFg, infoBdgBrd, warnBoxFg, infoBoxFg);

    const QString checkCss = QString(
        "QCheckBox[settingsCheck=\"true\"] {"
        "  color: %1; font-size: 13px; font-weight: 600; spacing: 10px; background: transparent;"
        "}"
        "QCheckBox[settingsCheck=\"true\"]:checked { color: %1; font-size: 13px; font-weight: 600; }"
        "QCheckBox[settingsCheck=\"true\"]::indicator { width: 18px; height: 18px; }"
        "QCheckBox[settingsCheck=\"true\"]::indicator:unchecked { image: url(\"://ic-check-box\"); }"
        "QCheckBox[settingsCheck=\"true\"]::indicator:checked { image: url(\"://ic-check-active\"); }"
        "QPushButton[settingsToggle=\"true\"] {"
        "  background-image: url(\"://ic-switch-off\"); background-position: center; background-repeat: no-repeat;"
        "  background-color: transparent; border: none; padding: 0px;"
        "}"
        "QPushButton[settingsToggle=\"true\"]:checked {"
        "  background-image: url(\"://ic-switch-on\"); background-position: center; background-repeat: no-repeat;"
        "  background-color: transparent; border: none; padding: 0px;"
        "}"
    ).arg(titleCol);

    const QString inputCss = QString(
        "QSpinBox[settingsInput=\"true\"], QDoubleSpinBox[settingsInput=\"true\"], QLineEdit[settingsInput=\"true\"] {"
        "  background-color: %1; color: %2; border: 1.5px solid %3; border-radius: 7px;"
        "  padding: 0px 10px; font-size: 13px; font-weight: 500;"
        "  selection-background-color: #5576B2; selection-color: #FFFFFF;"
        "}"
        "QPlainTextEdit[settingsInput=\"true\"] {"
        "  background-color: %1; color: %2; border: 1.5px solid %3; border-radius: 7px;"
        "  padding: 8px 10px; font-size: 13px; font-weight: 500;"
        "  selection-background-color: #5576B2; selection-color: #FFFFFF;"
        "}"
        "QSpinBox[settingsInput=\"true\"]:hover, QDoubleSpinBox[settingsInput=\"true\"]:hover, QLineEdit[settingsInput=\"true\"]:hover, QPlainTextEdit[settingsInput=\"true\"]:hover {"
        "  border-color: #7695CD;"
        "}"
        "QSpinBox[settingsInput=\"true\"]:focus, QDoubleSpinBox[settingsInput=\"true\"]:focus, QLineEdit[settingsInput=\"true\"]:focus, QPlainTextEdit[settingsInput=\"true\"]:focus {"
        "  border-color: #5576B2; background-color: %7;"
        "}"
        "QSpinBox[settingsInput=\"true\"]:disabled, QDoubleSpinBox[settingsInput=\"true\"]:disabled, QLineEdit[settingsInput=\"true\"]:disabled, QPlainTextEdit[settingsInput=\"true\"]:disabled {"
        "  background-color: %4; color: %5; border: 1.5px solid %6;"
        "}"
        "QLineEdit[settingsInputReadOnly=\"true\"] {"
        "  background-color: %4; color: %8; border: 1.5px solid %9; border-radius: 7px;"
        "  padding: 0px 10px; font-size: 12px; font-family: monospace; font-weight: 500;"
        "  selection-background-color: #5576B2; selection-color: #FFFFFF;"
        "}"
        "QSpinBox[settingsInput=\"true\"]::up-button, QDoubleSpinBox[settingsInput=\"true\"]::up-button {"
        "  subcontrol-origin: border; subcontrol-position: top right; width: 22px; height: 16px;"
        "  border: none; background: transparent; image: url(\"://ic-arrow-drop-up-purple\"); margin: 2px 4px 0px 0px;"
        "}"
        "QSpinBox[settingsInput=\"true\"]::down-button, QDoubleSpinBox[settingsInput=\"true\"]::down-button {"
        "  subcontrol-origin: border; subcontrol-position: bottom right; width: 22px; height: 16px;"
        "  border: none; background: transparent; image: url(\"://ic-arrow-drop-down-purple\"); margin: 0px 4px 2px 0px;"
        "}"
    ).arg(inputBg, titleCol, inputBrd, inputDisBg, inputDisFg, inputDisBrd, inputFocus, subCol, divider)
    + QString(
        "QComboBox[settingsInput=\"true\"] {"
        "  combobox-popup: 0; background-color: %1; color: %2; border: 1.5px solid %3; border-radius: 7px;"
        "  padding: 0px 34px 0px 12px; font-size: 13px; font-weight: 500; text-align: left;"
        "}"
        "QComboBox[settingsInput=\"true\"]:hover { border-color: #7695CD; }"
        "QComboBox[settingsInput=\"true\"]:focus, QComboBox[settingsInput=\"true\"]:on {"
        "  border-color: #5576B2; background-color: %4;"
        "}"
        "QComboBox[settingsInput=\"true\"]:disabled {"
        "  background-color: %5; color: %6; border: 1.5px solid %7;"
        "}"
        "QComboBox[settingsInput=\"true\"]::drop-down {"
        "  subcontrol-origin: padding; subcontrol-position: top right; width: 30px;"
        "  border: none; border-left: 1px solid %8; border-top-right-radius: 6px; border-bottom-right-radius: 6px; background: transparent;"
        "}"
        "QComboBox[settingsInput=\"true\"]::down-arrow { image: url(\"%9\"); width: 16px; height: 16px; }"
    ).arg(inputBg, titleCol, inputBrd, inputFocus, inputDisBg, inputDisFg, inputDisBrd, divider, dropArrow)
    + QString(
        "QComboBox[settingsInput=\"true\"] QAbstractItemView {"
        "  background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 4px;"
        "  selection-background-color: #5576B2; selection-color: #FFFFFF; outline: none;"
        "}"
    ).arg(cardBg, titleCol, cardBrd);

    const QString btnCss = QString(
        "QPushButton[settingsPrimary=\"true\"] {"
        "  background-color: %1; color: %2; border: 1px solid %3; border-radius: 7px;"
        "  padding: 0px 18px; font-size: 13px; font-weight: 600;"
        "}"
        "QPushButton[settingsPrimary=\"true\"]:hover { background-color: %4; color: %5; border-color: %6; }"
        "QPushButton[settingsPrimary=\"true\"]:pressed { background-color: %7; }"
        "QPushButton[settingsPrimary=\"true\"]:disabled { background-color: %8; color: %9; border-color: %8; }"
    ).arg(primBg, primFg, primBrd, primHov, primHovFg, primHovBrd, primPress, inputDisBg, inputDisFg)
    + QString(
        "QPushButton[settingsSecondary=\"true\"] {"
        "  background-color: %1; color: %2; border: 1px solid %3; border-radius: 7px;"
        "  padding: 0px 14px; font-size: 13px; font-weight: 600;"
        "}"
        "QPushButton[settingsSecondary=\"true\"]:hover { background-color: %4; border-color: #7695CD; color: %5; }"
        "QPushButton[settingsSecondary=\"true\"]:pressed { background-color: %6; }"
        "QPushButton[settingsSecondary=\"true\"]:disabled { background-color: %8; color: %9; border-color: %6; }"
        "QPushButton[settingsMiniCopy=\"true\"] {"
        "  background-color: %1; color: %7; border: 1px solid %3; border-radius: 6px;"
        "  padding: 0px 10px; font-size: 12px; font-weight: 600;"
        "}"
        "QPushButton[settingsMiniCopy=\"true\"]:hover { background-color: %4; border-color: #7695CD; color: %5; }"
    ).arg(secBg, secFg, secBrd, secHov, titleCol, divider, subCol, inputDisBg, inputDisFg)
    + QString(
        "QPushButton[settingsSegBtn=\"true\"] {"
        "  background-color: %1; color: %2; border: 1px solid %3; border-radius: 7px;"
        "  padding: 0px 16px; font-size: 13px; font-weight: 600;"
        "}"
        "QPushButton[settingsSegBtn=\"true\"]:hover { border-color: #7695CD; color: %4; }"
        "QPushButton[settingsSegBtn=\"true\"]:checked { background-color: %5; color: %6; border: 1.5px solid #7695CD; }"
    ).arg(secBg, subCol, secBrd, titleCol, primBg, primFg);

    return containerCss + textCss + checkCss + inputCss + btnCss;
}

QFrame* createSettingsCard(QWidget* parent, const QString& title, QVBoxLayout*& bodyLayoutOut)
{
    QFrame* card = new QFrame(parent);
    card->setProperty("settingsCard", true);
    card->setAttribute(Qt::WA_StyledBackground, true);

    bodyLayoutOut = new QVBoxLayout(card);
    bodyLayoutOut->setContentsMargins(18, 14, 18, 6);
    bodyLayoutOut->setSpacing(0);

    if (!title.isEmpty()) {
        QLabel* lblTitle = new QLabel(title, card);
        lblTitle->setProperty("settingsCardTitle", true);
        bodyLayoutOut->addWidget(lblTitle);
        bodyLayoutOut->addSpacing(8);
    }

    return card;
}

QFrame* createSettingsDivider(QWidget* parent)
{
    QFrame* div = new QFrame(parent);
    div->setProperty("settingsDivider", true);
    div->setAttribute(Qt::WA_StyledBackground, true);
    div->setFixedHeight(1);
    return div;
}

void clearLegacySettingsConstraints(QWidget* w)
{
    if (!w) return;
    w->setMinimumSize(0, 0);
    w->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    w->setProperty("cssClass", QVariant());
    w->setGraphicsEffect(nullptr);
}

QWidget* createSettingsPageShell(QWidget* owner,
                                 QVBoxLayout* hostLayout,
                                 const QString& title,
                                 const QString& subtitle,
                                 QVBoxLayout*& scrollLayoutOut,
                                 QHBoxLayout*& headerRightLayoutOut,
                                 QVBoxLayout*& rootLayoutOut)
{
    QWidget* modernRoot = new QWidget(owner);
    modernRoot->setObjectName(QStringLiteral("settingsPageRoot"));
    modernRoot->setAttribute(Qt::WA_StyledBackground, true);
    if (hostLayout) {
        for (int i = hostLayout->count() - 1; i >= 0; --i) {
            QLayoutItem* item = hostLayout->itemAt(i);
            if (item && item->spacerItem()) {
                delete hostLayout->takeAt(i);
            }
        }
        hostLayout->addWidget(modernRoot, 1);
    }

    rootLayoutOut = new QVBoxLayout(modernRoot);
    rootLayoutOut->setContentsMargins(0, 0, 0, 0);
    rootLayoutOut->setSpacing(12);

    QHBoxLayout* headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(2, 0, 2, 0);
    headerRow->setSpacing(12);

    QVBoxLayout* titleCol = new QVBoxLayout();
    titleCol->setContentsMargins(0, 0, 0, 0);
    titleCol->setSpacing(2);

    QLabel* lblTitle = new QLabel(title, modernRoot);
    lblTitle->setProperty("settingsPageTitle", true);
    QLabel* lblSub = new QLabel(subtitle, modernRoot);
    lblSub->setProperty("settingsPageSubtitle", true);
    lblSub->setWordWrap(true);
    titleCol->addWidget(lblTitle);
    titleCol->addWidget(lblSub);

    headerRightLayoutOut = new QHBoxLayout();
    headerRightLayoutOut->setContentsMargins(0, 0, 0, 0);
    headerRightLayoutOut->setSpacing(8);

    headerRow->addLayout(titleCol, 1);
    headerRow->addLayout(headerRightLayoutOut);
    rootLayoutOut->addLayout(headerRow);

    QScrollArea* scrollArea = new QScrollArea(modernRoot);
    scrollArea->setObjectName(QStringLiteral("settingsScroll"));
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setAutoFillBackground(false);
    if (scrollArea->viewport()) {
        scrollArea->viewport()->setObjectName(QStringLiteral("settingsScrollViewport"));
        scrollArea->viewport()->setAutoFillBackground(false);
        scrollArea->viewport()->setAttribute(Qt::WA_StyledBackground, true);
    }

    QWidget* scrollContent = new QWidget(scrollArea);
    scrollContent->setObjectName(QStringLiteral("settingsScrollContent"));
    scrollContent->setAutoFillBackground(false);
    scrollContent->setAttribute(Qt::WA_StyledBackground, true);

    scrollLayoutOut = new QVBoxLayout(scrollContent);
    scrollLayoutOut->setContentsMargins(0, 2, 4, 8);
    scrollLayoutOut->setSpacing(14);

    scrollArea->setWidget(scrollContent);
    rootLayoutOut->addWidget(scrollArea, 1);

    return modernRoot;
}

void buildSettingsOptionsFooter(QWidget* root,
                                QVBoxLayout* rootLayout,
                                QPushButton* btnReset,
                                QPushButton* btnDiscard,
                                QPushButton* btnSave)
{
    if (!root || !rootLayout) return;

    QHBoxLayout* footerRow = new QHBoxLayout();
    footerRow->setContentsMargins(0, 4, 0, 0);
    footerRow->setSpacing(10);

    if (btnReset) {
        clearLegacySettingsConstraints(btnReset);
        btnReset->setParent(root);
        btnReset->setText(QObject::tr("Reset to Default"));
        btnReset->setProperty("settingsSecondary", true);
        btnReset->setFixedHeight(36);
        btnReset->setCursor(Qt::PointingHandCursor);
        btnReset->setFocusPolicy(Qt::NoFocus);
        footerRow->addWidget(btnReset);
    }

    if (btnDiscard) {
        clearLegacySettingsConstraints(btnDiscard);
        btnDiscard->setParent(root);
        btnDiscard->setText(QObject::tr("Discard Changes"));
        btnDiscard->setProperty("settingsSecondary", true);
        btnDiscard->setFixedHeight(36);
        btnDiscard->setCursor(Qt::PointingHandCursor);
        btnDiscard->setFocusPolicy(Qt::NoFocus);
        footerRow->addWidget(btnDiscard);
    }

    footerRow->addStretch(1);

    if (btnSave) {
        clearLegacySettingsConstraints(btnSave);
        btnSave->setParent(root);
        btnSave->setText(QObject::tr("Save Changes"));
        btnSave->setProperty("settingsPrimary", true);
        btnSave->setMinimumSize(150, 36);
        btnSave->setMaximumHeight(36);
        btnSave->setCursor(Qt::PointingHandCursor);
        btnSave->setFocusPolicy(Qt::NoFocus);
        footerRow->addWidget(btnSave);
    }

    rootLayout->addLayout(footerRow);
}

void addSettingsControlRow(QWidget* card,
                           QVBoxLayout* cardBody,
                           const QString& title,
                           const QString& desc,
                           QWidget* control,
                           bool addDividerBefore)
{
    if (!card || !cardBody) return;
    if (addDividerBefore) {
        cardBody->addWidget(createSettingsDivider(card));
    }

    QWidget* row = new QWidget(card);
    QHBoxLayout* h = new QHBoxLayout(row);
    h->setContentsMargins(0, addDividerBefore ? 12 : 2, 0, 12);
    h->setSpacing(16);

    QVBoxLayout* textCol = new QVBoxLayout();
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(2);
    QLabel* lblTitle = new QLabel(title, row);
    lblTitle->setProperty("settingsRowTitle", true);
    QLabel* lblDesc = new QLabel(desc, row);
    lblDesc->setProperty("settingsRowDesc", true);
    lblDesc->setWordWrap(true);
    textCol->addWidget(lblTitle);
    textCol->addWidget(lblDesc);

    h->addLayout(textCol, 1);
    if (control) {
        control->setParent(row);
        h->addWidget(control, 0, Qt::AlignVCenter);
    }
    cardBody->addWidget(row);
}

void addSettingsCheckRow(QWidget* card,
                         QVBoxLayout* cardBody,
                         QCheckBox* checkBox,
                         const QString& text,
                         const QString& desc,
                         bool addDividerBefore)
{
    if (!card || !cardBody || !checkBox) return;
    if (addDividerBefore) {
        cardBody->addWidget(createSettingsDivider(card));
    }

    QWidget* row = new QWidget(card);
    QVBoxLayout* v = new QVBoxLayout(row);
    v->setContentsMargins(0, addDividerBefore ? 12 : 2, 0, 12);
    v->setSpacing(3);

    clearLegacySettingsConstraints(checkBox);
    checkBox->setParent(row);
    checkBox->setText(text);
    checkBox->setProperty("settingsCheck", true);
    checkBox->setCursor(Qt::PointingHandCursor);
    checkBox->setFocusPolicy(Qt::NoFocus);
    v->addWidget(checkBox);

    if (!desc.isEmpty()) {
        QLabel* lblDesc = new QLabel(desc, row);
        lblDesc->setProperty("settingsRowDesc", true);
        lblDesc->setWordWrap(true);
        lblDesc->setContentsMargins(28, 0, 0, 0);
        v->addWidget(lblDesc);
    }

    cardBody->addWidget(row);
}

void addSettingsInfoRow(QWidget* card,
                        QVBoxLayout* cardBody,
                        const QString& keyText,
                        QLabel* valueLabel,
                        bool monoStyle,
                        bool addDividerBefore,
                        QWidget* trailingWidget)
{
    if (!card || !cardBody || !valueLabel) return;
    if (addDividerBefore) {
        cardBody->addWidget(createSettingsDivider(card));
    }

    QWidget* row = new QWidget(card);
    QHBoxLayout* h = new QHBoxLayout(row);
    h->setContentsMargins(0, addDividerBefore ? 10 : 2, 0, 10);
    h->setSpacing(14);

    QLabel* lblKey = new QLabel(keyText, row);
    lblKey->setProperty("settingsInfoKey", true);
    lblKey->setFixedWidth(210);
    h->addWidget(lblKey, 0, Qt::AlignTop);

    clearLegacySettingsConstraints(valueLabel);
    valueLabel->setParent(row);
    valueLabel->setProperty(monoStyle ? "settingsInfoMono" : "settingsInfoVal", true);
    valueLabel->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
    valueLabel->setCursor(Qt::IBeamCursor);
    h->addWidget(valueLabel, 1, Qt::AlignVCenter);

    if (trailingWidget) {
        trailingWidget->setParent(row);
        h->addWidget(trailingWidget, 0, Qt::AlignVCenter);
    }

    cardBody->addWidget(row);
}

QString formatLocaleDisplayName(const QString& langStr)
{
    const QLocale locale(langStr);
    const bool hasRegion = langStr.contains(QLatin1Char('_'));

    QString nativeLabel = locale.nativeLanguageName();
    if (hasRegion) {
        const QString nativeCountry = locale.nativeCountryName();
        if (!nativeCountry.isEmpty()) {
            nativeLabel += QStringLiteral(" - ") + nativeCountry;
        }
    }

    QFontDatabase::WritingSystem ws = QFontDatabase::Any;
    switch (locale.script()) {
        case QLocale::KoreanScript:
        case QLocale::HangulScript:
            ws = QFontDatabase::Korean;
            break;
        case QLocale::SimplifiedHanScript:
            ws = QFontDatabase::SimplifiedChinese;
            break;
        case QLocale::TraditionalHanScript:
            ws = QFontDatabase::TraditionalChinese;
            break;
        case QLocale::JapaneseScript:
            ws = QFontDatabase::Japanese;
            break;
        case QLocale::ArabicScript:
            ws = QFontDatabase::Arabic;
            break;
        case QLocale::HebrewScript:
            ws = QFontDatabase::Hebrew;
            break;
        case QLocale::CyrillicScript:
            ws = QFontDatabase::Cyrillic;
            break;
        case QLocale::GreekScript:
            ws = QFontDatabase::Greek;
            break;
        default:
            if (locale.language() == QLocale::Korean) {
                ws = QFontDatabase::Korean;
            } else if (locale.language() == QLocale::Chinese) {
                ws = QFontDatabase::SimplifiedChinese;
            } else if (locale.language() == QLocale::Japanese) {
                ws = QFontDatabase::Japanese;
            }
            break;
    }

    bool canRenderNative = !nativeLabel.trimmed().isEmpty();
    if (canRenderNative && ws != QFontDatabase::Any && ws != QFontDatabase::Latin && ws != QFontDatabase::Cyrillic) {
        QFontDatabase db;
        canRenderNative = !db.families(ws).isEmpty();
    }

    QString displayLabel = nativeLabel;
    if (!canRenderNative) {
        const QString langEn = QLocale::languageToString(locale.language());
        if (hasRegion && locale.country() != QLocale::AnyCountry) {
            const QString countryEn = QLocale::countryToString(locale.country());
            displayLabel = countryEn.isEmpty() ? langEn : (langEn + QStringLiteral(" - ") + countryEn);
        } else {
            displayLabel = langEn;
        }
    }

    return displayLabel + QStringLiteral(" (") + langStr + QStringLiteral(")");
}
