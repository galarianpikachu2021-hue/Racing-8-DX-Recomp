// SPDX-License-Identifier: GPL-3.0-or-later
#include "multiplayer_page.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkInterface>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
constexpr int kDefaultPort = 24872; // the room servers' default

/// This computer's IPv4 addresses on its networks (what people on the same network type in; people elsewhere need the public one).
QStringList LocalAddresses() {
    QStringList out;
    for (const QHostAddress& a : QNetworkInterface::allAddresses()) {
        if (a.protocol() != QAbstractSocket::IPv4Protocol || a.isLoopback() || a.isLinkLocal()) continue;
        out << a.toString();
    }
    return out;
}
} // namespace

MultiplayerPage::MultiplayerPage(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Play <b>Wireless Play</b> (the game's local wireless mode) with people who are not in the same room as you. "
                                "Every player's game joins the same <i>room</i>, a small relay server, and sees the others' sessions as "
                                "if they were nearby consoles. One player hosts the room from this launcher; the others join it with the "
                                "host's address. Rooms run by suyu/yuzu-style room servers work too."));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    // The console profile's name: what the game shows for you (Wireless Play's lobby, so what friends see).
    auto* profile_row = new QWidget;
    auto* profile_layout = new QFormLayout(profile_row);
    profile_layout->setContentsMargins(0, 0, 0, 0);
    profile_name = new QLineEdit;
    profile_name->setMaxLength(10);
    profile_name->setPlaceholderText(tr("leave empty to keep the current name"));
    profile_name->setToolTip(tr("Your console profile's name (up to 10 characters). The game shows it for you, and friends see it in "
                                "Wireless Play. It is saved in the game's profile when the game starts."));
    profile_layout->addRow(tr("Profile name:"), profile_name);
    layout->addWidget(profile_row);

    enabled = new QCheckBox(tr("Enable online play"));
    QFont f = enabled->font();
    f.setBold(true);
    enabled->setFont(f);
    layout->addWidget(enabled);

    settings = new QGroupBox(tr("Room"));
    auto* form = new QFormLayout(settings);
    auto* mode_row = new QWidget;
    auto* mode_layout = new QHBoxLayout(mode_row);
    mode_layout->setContentsMargins(0, 0, 0, 0);
    modes = new QButtonGroup(this);
    auto* host_button = new QRadioButton(tr("Host a room on this computer"));
    auto* join_button = new QRadioButton(tr("Join a room"));
    modes->addButton(host_button, Host);
    modes->addButton(join_button, Join);
    mode_layout->addWidget(host_button);
    mode_layout->addWidget(join_button);
    mode_layout->addStretch();
    form->addRow(tr("I want to:"), mode_row);

    nickname = new QLineEdit;
    nickname->setMaxLength(20);
    nickname->setPlaceholderText(tr("empty: your profile name"));
    nickname->setToolTip(tr("Your name in the room (4 to 20 letters, digits, spaces, . _ -). Empty: the profile name above."));
    form->addRow(tr("Room nickname:"), nickname);

    room_name = new QLineEdit;
    room_name->setMaxLength(40);
    room_name->setPlaceholderText(tr("Racing 8 DX Recomp"));
    form->addRow(tr("Room name:"), room_name);
    members = new QSpinBox;
    members->setRange(2, 16);
    form->addRow(tr("Players in the room:"), members);

    address = new QLineEdit;
    address->setPlaceholderText(tr("the host's address, e.g. 203.0.113.7 or rooms.example.org"));
    form->addRow(tr("Room address:"), address);

    port = new QSpinBox;
    port->setRange(1, 65535);
    form->addRow(tr("Port (UDP):"), port);
    password = new QLineEdit;
    password->setEchoMode(QLineEdit::Password);
    password->setPlaceholderText(tr("optional; everyone in the room uses the same one"));
    form->addRow(tr("Room password:"), password);

    host_addresses = new QLabel;
    host_addresses->setWordWrap(true);
    host_addresses->setTextInteractionFlags(Qt::TextSelectableByMouse);
    copy_details = new QPushButton(tr("Copy join details"));
    copy_details->setToolTip(tr("Copies this computer's address and the port, to send to the people who join."));
    auto* host_row = new QWidget;
    auto* host_layout = new QHBoxLayout(host_row);
    host_layout->setContentsMargins(0, 0, 0, 0);
    host_layout->addWidget(host_addresses, 1);
    host_layout->addWidget(copy_details);
    form->addRow(tr("This computer:"), host_row);
    layout->addWidget(settings);

    hint = new QLabel;
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto* how = new QLabel(tr("<b>How to play:</b> everyone enables online play here and presses Play. In the game, open <i>Wireless "
                              "Play</i>: one player chooses <i>Create Room</i>, the others pick that session from the lobby list. "
                              "Games of different versions (3.0.4 / 4.0.1) cannot play together."));
    how->setWordWrap(true);
    layout->addWidget(how);
    layout->addStretch();

    // Settings (the old Multiplayer tab's "mode" 1 = join, 2 = host, 0 = off carries over).
    QSettings s;
    const int old_mode = s.value(QStringLiteral("multiplayer/mode"), 0).toInt();
    enabled->setChecked(s.value(QStringLiteral("multiplayer/enabled"), old_mode != 0).toBool());
    const int m = s.contains(QStringLiteral("multiplayer/role")) ? s.value(QStringLiteral("multiplayer/role")).toInt() : (old_mode == 1 ? Join : Host);
    modes->button(m == Join ? Join : Host)->setChecked(true);
    nickname->setText(s.value(QStringLiteral("multiplayer/nickname")).toString());
    profile_name->setText(s.value(QStringLiteral("profile/name")).toString());
    room_name->setText(s.value(QStringLiteral("multiplayer/room_name")).toString());
    address->setText(s.value(QStringLiteral("multiplayer/address")).toString());
    port->setValue(s.value(QStringLiteral("multiplayer/port"), kDefaultPort).toInt());
    password->setText(s.value(QStringLiteral("multiplayer/password")).toString());
    members->setValue(s.value(QStringLiteral("multiplayer/members"), 8).toInt());

    const auto changed = [this] {
        save();
        refresh();
    };
    connect(enabled, &QCheckBox::toggled, this, changed);
    connect(modes, &QButtonGroup::idToggled, this, [changed](int, bool on) {
        if (on) changed();
    });
    for (auto* e : {profile_name, nickname, room_name, address, password}) connect(e, &QLineEdit::textChanged, this, changed);
    for (auto* b : {port, members}) connect(b, &QSpinBox::valueChanged, this, changed);
    connect(copy_details, &QPushButton::clicked, this, [this] {
        if (auto* clip = QApplication::clipboard()) clip->setText(joinDetails());
        hint->setText(tr("Copied: %1").arg(joinDetails().toHtmlEscaped()));
    });
    refresh();
}

int MultiplayerPage::mode() const {
    return modes->checkedId() == Join ? Join : Host;
}

void MultiplayerPage::save() const {
    QSettings s;
    s.setValue(QStringLiteral("multiplayer/enabled"), enabled->isChecked());
    s.setValue(QStringLiteral("multiplayer/role"), mode());
    s.setValue(QStringLiteral("multiplayer/mode"), enabled->isChecked() ? (mode() == Join ? 1 : 2) : 0); // older launchers
    s.setValue(QStringLiteral("multiplayer/nickname"), nickname->text());
    s.setValue(QStringLiteral("profile/name"), profile_name->text());
    s.setValue(QStringLiteral("multiplayer/room_name"), room_name->text());
    s.setValue(QStringLiteral("multiplayer/address"), address->text());
    s.setValue(QStringLiteral("multiplayer/port"), port->value());
    s.setValue(QStringLiteral("multiplayer/password"), password->text());
    s.setValue(QStringLiteral("multiplayer/members"), members->value());
}

QString MultiplayerPage::joinDetails() const {
    const QStringList local = LocalAddresses();
    const QString where = local.isEmpty() ? tr("<your address>") : local.first();
    QString text = QStringLiteral("%1:%2").arg(where).arg(port->value());
    if (!password->text().isEmpty()) text += tr(" (password: %1)").arg(password->text());
    return text;
}

void MultiplayerPage::refresh() {
    const bool on = enabled->isChecked();
    const bool host = mode() == Host;
    settings->setEnabled(on);
    room_name->setEnabled(on && host);
    members->setEnabled(on && host);
    address->setEnabled(on && !host);
    copy_details->setEnabled(on && host);
    const QStringList local = LocalAddresses();
    host_addresses->setText(local.isEmpty() ? tr("no network found") : local.join(QStringLiteral(", ")) + tr(" (on your network)"));

    const QString p = problem();
    if (!on) hint->setText(tr("Online play is off: the game plays as before (Wireless Play only finds nothing)."));
    else if (!p.isEmpty()) hint->setText(QStringLiteral("<span style='color:#c33'>%1</span>").arg(p.toHtmlEscaped()));
    else if (host)
        hint->setText(tr("You host the room while the game runs. Players on your network join with the address above; players "
                         "elsewhere need your public address and UDP port %1 forwarded to this computer on your router.")
                          .arg(port->value()));
    else hint->setText(tr("The game joins the room when it starts; the host's session then appears in Wireless Play's lobby."));
}

QString MultiplayerPage::roomNickname() const {
    return nickname->text().trimmed().isEmpty() ? profile_name->text().trimmed() : nickname->text();
}

QString MultiplayerPage::problem() const {
    if (!enabled->isChecked()) return {};
    static const QRegularExpression nick(QStringLiteral("^[a-zA-Z0-9._\\- ]{4,20}$"));
    if (!nick.match(roomNickname()).hasMatch())
        return tr("The room nickname must be 4 to 20 letters, digits, spaces, dots, dashes or underscores (it is the profile name "
                  "when left empty).");
    if (mode() == Join && address->text().trimmed().isEmpty()) return tr("Enter the room's address.");
    if (password->text().contains(QLatin1Char('@')) || password->text().contains(QLatin1Char(':')))
        return tr("The password cannot contain '@' or ':'.");
    return {};
}

void MultiplayerPage::apply(QProcessEnvironment& env) const {
    if (!profile_name->text().trimmed().isEmpty()) env.insert(QStringLiteral("KARTRECOMP_PROFILE_NAME"), profile_name->text().trimmed());
    if (!enabled->isChecked() || !problem().isEmpty()) return;
    const QString pass = password->text();
    if (mode() == Host) {
        env.insert(QStringLiteral("KARTRECOMP_ROOM_HOST"), QString::number(port->value()) + (pass.isEmpty() ? QString() : QLatin1Char(':') + pass));
        env.insert(QStringLiteral("KARTRECOMP_ROOM_NICK"), roomNickname());
        env.insert(QStringLiteral("KARTRECOMP_ROOM_MAX"), QString::number(members->value()));
        if (!room_name->text().trimmed().isEmpty()) env.insert(QStringLiteral("KARTRECOMP_ROOM_NAME"), room_name->text().trimmed());
    } else {
        env.insert(QStringLiteral("KARTRECOMP_ROOM"), roomNickname() + (pass.isEmpty() ? QString() : QLatin1Char(':') + pass) + QLatin1Char('@') +
                                                         address->text().trimmed() + QLatin1Char(':') + QString::number(port->value()));
    }
}
