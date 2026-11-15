// SPDX-License-Identifier: GPL-3.0-or-later
#include "multiplayer_page.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>

namespace {
constexpr int kDefaultPort = 24872; // the room servers' default
enum Mode { Off = 0, Join = 1, Host = 2 };
} // namespace

MultiplayerPage::MultiplayerPage(QWidget* parent) : QWidget(parent) {
    auto* form = new QFormLayout(this);
    auto* intro = new QLabel(tr("Play <b>Wireless Play</b> (the game's local wireless mode) with people who are not in the same room as you: "
                                "every player's game joins the same multiplayer room, a small relay server, and sees the others' "
                                "sessions as if they were nearby consoles. One player can host the room from this launcher; the others "
                                "join it with the host's address. Rooms run by suyu/yuzu-style room servers work too."));
    intro->setWordWrap(true);
    form->addRow(intro);

    mode = new QComboBox;
    mode->addItems({tr("Off"), tr("Join a room"), tr("Host a room on this computer")});
    form->addRow(tr("Multiplayer:"), mode);
    nickname = new QLineEdit;
    nickname->setMaxLength(20);
    nickname->setPlaceholderText(tr("4 to 20 letters, digits, spaces, . _ -"));
    form->addRow(tr("Nickname:"), nickname);
    address = new QLineEdit;
    address->setPlaceholderText(tr("the host's address, e.g. 203.0.113.7 or rooms.example.org"));
    form->addRow(tr("Room address:"), address);
    port = new QSpinBox;
    port->setRange(1, 65535);
    form->addRow(tr("Port:"), port);
    password = new QLineEdit;
    password->setEchoMode(QLineEdit::Password);
    password->setPlaceholderText(tr("optional"));
    form->addRow(tr("Room password:"), password);
    members = new QSpinBox;
    members->setRange(2, 16);
    form->addRow(tr("Players in the room (host):"), members);
    hint = new QLabel;
    hint->setWordWrap(true);
    form->addRow(hint);

    QSettings s;
    mode->setCurrentIndex(qBound(0, s.value(QStringLiteral("multiplayer/mode"), 0).toInt(), 2));
    nickname->setText(s.value(QStringLiteral("multiplayer/nickname")).toString());
    address->setText(s.value(QStringLiteral("multiplayer/address")).toString());
    port->setValue(s.value(QStringLiteral("multiplayer/port"), kDefaultPort).toInt());
    password->setText(s.value(QStringLiteral("multiplayer/password")).toString());
    members->setValue(s.value(QStringLiteral("multiplayer/members"), 8).toInt());

    connect(mode, &QComboBox::currentIndexChanged, this, [this] { save(); refresh(); });
    for (auto* e : {nickname, address, password}) connect(e, &QLineEdit::textChanged, this, [this] { save(); refresh(); });
    for (auto* b : {port, members}) connect(b, &QSpinBox::valueChanged, this, [this] { save(); refresh(); });
    refresh();
}

void MultiplayerPage::save() const {
    QSettings s;
    s.setValue(QStringLiteral("multiplayer/mode"), mode->currentIndex());
    s.setValue(QStringLiteral("multiplayer/nickname"), nickname->text());
    s.setValue(QStringLiteral("multiplayer/address"), address->text());
    s.setValue(QStringLiteral("multiplayer/port"), port->value());
    s.setValue(QStringLiteral("multiplayer/password"), password->text());
    s.setValue(QStringLiteral("multiplayer/members"), members->value());
}

void MultiplayerPage::refresh() {
    const int m = mode->currentIndex();
    for (QWidget* w : {static_cast<QWidget*>(nickname), static_cast<QWidget*>(port), static_cast<QWidget*>(password)}) w->setEnabled(m != Off);
    address->setEnabled(m == Join);
    members->setEnabled(m == Host);
    const QString p = problem();
    if (!p.isEmpty()) hint->setText(QStringLiteral("<span style='color:#c33'>%1</span>").arg(p.toHtmlEscaped()));
    else if (m == Host)
        hint->setText(tr("The others join your public address on port %1 (UDP). If they are not on your network, forward that port on your "
                         "router. Start Wireless Play in the game and choose Create Room.").arg(port->value()));
    else if (m == Join) hint->setText(tr("Start Wireless Play in the game: the host's session appears in the lobby list."));
    else hint->clear();
}

QString MultiplayerPage::problem() const {
    const int m = mode->currentIndex();
    if (m == Off) return {};
    static const QRegularExpression nick(QStringLiteral("^[a-zA-Z0-9._\\- ]{4,20}$"));
    if (!nick.match(nickname->text()).hasMatch()) return tr("The nickname must be 4 to 20 letters, digits, spaces, dots, dashes or underscores.");
    if (m == Join && address->text().trimmed().isEmpty()) return tr("Enter the room's address.");
    if (password->text().contains(QLatin1Char('@'))) return tr("The password cannot contain '@'.");
    return {};
}

void MultiplayerPage::apply(QProcessEnvironment& env) const {
    const int m = mode->currentIndex();
    if (m == Off || !problem().isEmpty()) return;
    const QString pass = password->text();
    if (m == Host) {
        env.insert(QStringLiteral("KARTRECOMP_ROOM_HOST"), QString::number(port->value()) + (pass.isEmpty() ? QString() : QLatin1Char(':') + pass));
        env.insert(QStringLiteral("KARTRECOMP_ROOM_NICK"), nickname->text());
        env.insert(QStringLiteral("KARTRECOMP_ROOM_MAX"), QString::number(members->value()));
    } else {
        env.insert(QStringLiteral("KARTRECOMP_ROOM"), nickname->text() + (pass.isEmpty() ? QString() : QLatin1Char(':') + pass) + QLatin1Char('@') +
                                                         address->text().trimmed() + QLatin1Char(':') + QString::number(port->value()));
    }
}
