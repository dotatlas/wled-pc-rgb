// MainWindow — wled-pc-rgb: mirror a WLED instance onto this PC's RGB, live.
// Owns the setup-readiness strip, the device list (per-device mirror toggle),
// the WLED-host field, the primary Mirror button, the live colour swatches, the
// tray icon, and all persisted settings.
#include "main_window.h"
#include "orgb_client.h"
#include "sysinfo.h"
#include "ipc_client.h"
#include "colour_ops.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QSlider>
#include <QSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QWidget>
#include <QPixmap>
#include <QIcon>
#include <QVariant>
#include <QColor>
#include <QProcess>
#include <QCoreApplication>
#include <QApplication>
#include <QFile>
#include <QTimer>
#include <QElapsedTimer>
#include <QTcpSocket>
#include <QDir>
#include <QTextStream>
#include <QSettings>
#include <QCloseEvent>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QStyle>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QStringList>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QRadioButton>
#include <QButtonGroup>
#include <QFormLayout>
#include <QDesktopServices>
#include <QShortcut>
#include <QUrl>
#include <QFontMetrics>
#include <QHeaderView>
#include <QPalette>
#include <memory>

namespace {
constexpr int kDeviceIndexRole = Qt::UserRole + 1;
constexpr int kModeIndexRole   = Qt::UserRole + 2;
constexpr int kDeviceTypeRole  = Qt::UserRole + 3;   // OpenRGB device type (2 = GPU, 4 = cooler)
constexpr auto kHost = "127.0.0.1";
constexpr quint16 kPort = 6742;
constexpr quint16 kIpcPort = 47900;
constexpr int kDefaultZoneLeds = 8;   // MSI JARGB headers are zones of 8 (user-adjustable)

// The mirror's colour transforms live in colour_ops.h, shared with the headless CLI.
using colour::scale;
using colour::stripBg;
using colour::stripBgAll;
using colour::gammaOut;

QString findJava() {
    const QString jh = qEnvironmentVariable("JAVA_HOME");
    if (!jh.isEmpty() && QFile::exists(jh + "/bin/java.exe")) return jh + "/bin/java.exe";
    const QString scoop = "C:/.software/scoop/apps/temurin21-jdk/current/bin/java.exe";
    if (QFile::exists(scoop)) return scoop;
    return "java";
}

void paintSwatch(QLabel* l, const QColor& c) {
    l->setStyleSheet(QString("background:%1; border:1px solid #555; border-radius:3px;").arg(c.name()));
}

// A secondary-text colour taken from the palette, so grey captions stay legible on a dark theme.
QString hintColour(const QWidget* w) {
    return w->palette().color(QPalette::Disabled, QPalette::WindowText).name();
}
}

// The app icon: a ring of RGB dots on a rounded dark tile, painted in code at several sizes so it
// stays crisp in the taskbar and tray. Evokes an addressable LED ring — what the app mirrors.
QIcon MainWindow::appIcon() {
    QIcon icon;
    for (int sz : { 16, 24, 32, 48, 64, 128 }) {
        QPixmap pm(sz, sz);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal r = sz * 0.14;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x1c, 0x1f, 0x27));                     // dark rounded tile
        p.drawRoundedRect(QRectF(sz * 0.06, sz * 0.06, sz * 0.88, sz * 0.88), r, r);
        const QPointF c(sz / 2.0, sz / 2.0);
        const qreal ring = sz * 0.30, dot = qMax<qreal>(1.0, sz * 0.11);
        const int n = 8;
        for (int i = 0; i < n; ++i) {
            const qreal a = (2.0 * M_PI * i) / n - M_PI / 2.0;
            const QColor col = QColor::fromHsvF(qreal(i) / n, 0.85, 1.0);
            p.setBrush(col);
            p.drawEllipse(QPointF(c.x() + ring * std::cos(a), c.y() + ring * std::sin(a)), dot, dot);
        }
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

void MainWindow::openAbout() {
    const QString ver = QCoreApplication::applicationVersion();
    const QString repo = "https://github.com/dotatlas/wled-pc-rgb";
    QMessageBox box(this);
    box.setWindowTitle("About WLED PC RGB");
    box.setIconPixmap(appIcon().pixmap(64, 64));
    box.setTextFormat(Qt::RichText);
    box.setText(
        "<h3 style='margin-bottom:2px'>WLED PC RGB</h3>"
        "<p style='margin-top:0;color:gray'>Version " + ver + " &nbsp;·&nbsp; MIT licence</p>"
        "<p>Mirror a WLED strip's live colours onto your PC's RGB — fans, GPU, the NZXT Kraken "
        "ring, mouse and motherboard.</p>"
        "<p><a href='" + repo + "'>Project&nbsp;page</a> &nbsp;·&nbsp; "
        "<a href='" + repo + "/blob/main/docs/USAGE.md'>User&nbsp;guide</a> &nbsp;·&nbsp; "
        "<a href='" + repo + "/issues'>Report&nbsp;a&nbsp;bug</a></p>"
        "<p style='color:gray;font-size:11px'>Built with Qt. Talks to OpenRGB, WLED and LedFx; "
        "drives the NZXT Kraken over hidapi and the GPU over NVAPI. Thanks to those projects.</p>");
    box.setStandardButtons(QMessageBox::Ok);
    box.exec();
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    QSettings s;
    wledHost_ = s.value("wled/host", "wled.local").toString();
    // The user-facing blacklist (Advanced) starts empty — the Kraken is NOT hidden any more; it
    // shows in the device list like everything else. It is still driven by its OWN HID pipeline
    // (opcode 0x26, 512-byte reports — SignalRGB's protocol), and OpenRGB always skips it
    // internally (see setMirroring) so the two never fight. Ticking/unticking the Kraken row
    // turns its HID ring on/off; unticking hands the ring back to NZXT CAM.
    blacklist_ = {};

    setWindowTitle(baseTitle_);
    resize(660, 640);
    if (s.contains("win/geometry")) restoreGeometry(s.value("win/geometry").toByteArray());

    auto* central = new QWidget(this);
    auto* layout  = new QVBoxLayout(central);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    // --- title row: motherboard readout (demoted) + an always-visible About button ------------
    mobo_ = new QLabel("Motherboard: " + (sysinfo::motherboard().isEmpty() ? QString("(unknown)")
                                                                           : sysinfo::motherboard()), central);
    mobo_->setStyleSheet("color:" + hintColour(central) + ";");   // a quiet caption, not a heading
    auto* aboutBtn = new QToolButton(central);
    aboutBtn->setText("About");
    aboutBtn->setToolTip("Version, licence and links (F1)");
    aboutBtn->setAutoRaise(true);
    connect(aboutBtn, &QToolButton::clicked, this, &MainWindow::openAbout);
    { auto* tr = new QHBoxLayout; tr->addWidget(mobo_); tr->addStretch(1); tr->addWidget(aboutBtn);
      layout->addLayout(tr); }
    new QShortcut(QKeySequence(Qt::Key_F1), this, [this]{ openAbout(); });

    // --- setup readiness group ------------------------------------------------
    auto* setup = new QGroupBox("Setup", central);
    auto* sg = new QGridLayout(setup);

    // Status "traffic lights": colour AND a distinct glyph per state (see setDot), so they read
    // without relying on red/green — plus a leading caption so the row explains itself.
    sg->addWidget(new QLabel("Status:", setup), 0, 0);
    auto* dotsRow = new QHBoxLayout; dotsRow->setSpacing(14);
    auto makeDot = [&](QLabel*& dot, QLabel*& txt, const QString& label) {
        dot = new QLabel("○", setup); dot->setStyleSheet("color:#888; font-size:15px;");
        txt = new QLabel(label, setup);
        auto* cell = new QHBoxLayout; cell->setSpacing(4);
        cell->addWidget(dot); cell->addWidget(txt);
        dotsRow->addLayout(cell);
    };
    makeDot(dotO_, dotOtxt_, "OpenRGB");
    makeDot(dotB_, dotBtxt_, "Backend");
    makeDot(dotW_, dotWtxt_, "WLED");
    dotsRow->addStretch(1);
    sg->addLayout(dotsRow, 0, 1, 1, 2);

    sg->addWidget(new QLabel("WLED host:", setup), 1, 0);
    hostEdit_ = new QLineEdit(wledHost_, setup);
    hostEdit_->setPlaceholderText("wled.local or 192.168.x.x");
    auto* applyHost = new QPushButton("&Apply", setup);
    { auto* hr = new QHBoxLayout; hr->addWidget(hostEdit_, 1); hr->addWidget(applyHost);
      sg->addLayout(hr, 1, 1, 1, 2); }

    sg->addWidget(new QLabel("Live:", setup), 2, 0);
    swatchW_ = new QLabel(setup); swatchW_->setFixedSize(26, 16); paintSwatch(swatchW_, Qt::black);
    swatchP_ = new QLabel(setup); swatchP_->setFixedSize(26, 16); paintSwatch(swatchP_, Qt::black);
    swatchW_->setAccessibleName("WLED strip colour"); swatchW_->setToolTip("The colour WLED is showing");
    swatchP_->setAccessibleName("Colour sent to the PC"); swatchP_->setToolTip("The colour the PC mirrors");
    { auto* sr = new QHBoxLayout; sr->setSpacing(6);
      sr->addWidget(new QLabel("WLED", setup)); sr->addWidget(swatchW_);
      sr->addWidget(new QLabel("→  PC", setup)); sr->addWidget(swatchP_);
      sr->addStretch(1);
      sg->addLayout(sr, 2, 1, 1, 2); }

    // --- device list ----------------------------------------------------------
    status_ = new QLabel("Tick the devices to mirror, then press Mirror WLED.", central);
    auto* tip = new QLabel("The app puts each device in Direct mode for you. Close other RGB apps "
                           "(NZXT CAM, SignalRGB, MSI Center) first — they fight for the same devices.",
                           central);
    tip->setWordWrap(true);
    tip->setStyleSheet("color:" + hintColour(central) + ";");

    auto* devBox = new QGroupBox("Devices to mirror", central);
    auto* devLay = new QVBoxLayout(devBox);
    tree_ = new QTreeWidget(devBox);
    tree_->setHeaderLabels({"Device", "Detail"});
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    connect(tree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it, int col) {
        if (building_ || col != 0 || !it->data(0, kDeviceIndexRole).isValid()) return;
        if (!(it->flags() & Qt::ItemIsUserCheckable)) return;
        // Reconcile the bespoke pipelines FIRST, then push — syncGpuDriving() re-pushes on a change,
        // and this order matches setMirroring() so the GPU is never briefly included while we own it.
        if (mirroring_) { syncKrakenDriving(); syncGpuDriving(); pushIncluded(); }   // in-session opt-out
        refreshMirrorGate();
    });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int) {
        if (it && it->data(0, kModeIndexRole).isValid()) activateMode(it);   // double-click a mode row to select it
    });
    devLay->addWidget(tree_, 1);
    // Mode-activation lives WITH the tree it acts on (it used to be an orphan button in Advanced).
    auto* modeHint = new QLabel("Double-click a mode row to activate it.", devBox);
    modeHint->setStyleSheet("color:" + hintColour(central) + ";");
    auto* setMod = new QPushButton("Set as active mode", devBox);
    setMod->setToolTip("Activate the selected mode on its device (stop mirroring first).");
    { auto* mr = new QHBoxLayout; mr->addWidget(modeHint); mr->addStretch(1); mr->addWidget(setMod);
      devLay->addLayout(mr); }

    // --- brightness (the single control for the whole mirror) -----------------
    auto* bRow = new QHBoxLayout;
    bright_ = new QSlider(Qt::Horizontal, central);
    bright_->setRange(0, 100);
    bright_->setValue(s.value("mirror/brightness", 100).toInt());
    auto* bVal = new QLabel(QString::number(bright_->value()) + "%", central);
    connect(bright_, &QSlider::valueChanged, this, [this, bVal](int v){
        bVal->setText(QString::number(v) + "%");
        QSettings().setValue("mirror/brightness", v);
        // Preview from the MIRRORED colour, not the raw one — otherwise dragging the slider makes a
        // removed background flash back into the PC swatch. Same strip -> scale -> gamma order.
        const QColor prev = scale(mirrorColour_, v);
        paintSwatch(swatchP_, (gammaChk_ && gammaChk_->isChecked()) ? gammaOut(prev) : prev);
    });
    bRow->addWidget(new QLabel("Brightness:", central));
    bRow->addWidget(bright_, 1);
    bRow->addWidget(bVal);

    // Paint the PC preview once now (slider built setValue-before-connect).
    paintSwatch(swatchP_, scale(mirrorColour_, bright_->value()));

    // --- primary mirror button (the one prominent action) --------------------
    mirBtn_ = new QPushButton("▶  &Mirror WLED", central);
    mirBtn_->setCheckable(true);
    mirBtn_->setMinimumHeight(qMax(38, mirBtn_->fontMetrics().height() * 2));
    mirBtn_->setStyleSheet("QPushButton{font-weight:700;font-size:14px;}"
                           "QPushButton:checked{background:#2a8f5a;color:white;}");
    mirBtn_->setDefault(true);
    connect(mirBtn_, &QPushButton::toggled, this, &MainWindow::setMirroring);

    // --- advanced group (collapsed by default) --------------------------------
    // Everything lives in ONE container widget (advBody) so the whole section shows/hides in a
    // single line — the old design kept a parallel setVisible() list that every new control had to
    // be added to, and a control forgotten there would leak into the collapsed state.
    auto* adv = new QGroupBox("Advanced", central);
    adv->setCheckable(true);
    adv->setChecked(false);
    auto* advOuter = new QVBoxLayout(adv);
    auto* advBody  = new QWidget(adv);
    auto* av = new QVBoxLayout(advBody);
    av->setContentsMargins(0, 0, 0, 0);
    advOuter->addWidget(advBody);
    connect(adv, &QGroupBox::toggled, advBody, &QWidget::setVisible);
    advBody->setVisible(false);

    // Row: Zones + Kraken ring, side by side (both compact).
    {
        auto* row = new QHBoxLayout;
        auto* zonesBox = new QGroupBox("Zones", advBody);
        auto* zf = new QFormLayout(zonesBox);
        zoneSpin_ = new QSpinBox(zonesBox);
        zoneSpin_->setRange(1, 512);
        zoneSpin_->setValue(s.value("mirror/zoneLeds", kDefaultZoneLeds).toInt());
        zoneSpin_->setToolTip("LEDs per motherboard ARGB header (e.g. 8 for an MSI JARGB fan).\n"
                              "Set the number, then press Size zones.");
        connect(zoneSpin_, &QSpinBox::valueChanged, this, [](int v){ QSettings().setValue("mirror/zoneLeds", v); });
        auto* maxZ   = new QPushButton("Size zones", zonesBox);
        auto* rescan = new QPushButton("&Rescan devices", zonesBox);
        connect(maxZ,   &QPushButton::clicked, this, &MainWindow::maxZones);
        connect(rescan, &QPushButton::clicked, this, &MainWindow::refresh);
        zf->addRow("Zone size (LEDs):", zoneSpin_);
        { auto* zb = new QHBoxLayout; zb->addWidget(maxZ); zb->addWidget(rescan); zb->addStretch(1);
          zf->addRow(QString(), zb); }

        auto* ringBox = new QGroupBox("Kraken ring", advBody);
        auto* rf = new QFormLayout(ringBox);
        originSpin_ = new QSpinBox(ringBox);
        originSpin_->setRange(1, KrakenDriver::maxOrigins());
        originSpin_->setValue(s.value("mirror/ringOrigins", 2).toInt());
        originSpin_->setToolTip("How many points the ring grows the pattern out from, spaced evenly around\n"
                                "the circle. 1 = one bloom across 12 LEDs; 2 (default) = two opposite blooms\n"
                                "of 6 LEDs that meet halfway. More = smaller, faster blooms (fewer shades).");
        kraken_.setOrigins(originSpin_->value());
        connect(originSpin_, &QSpinBox::valueChanged, this, [this](int v){
            QSettings().setValue("mirror/ringOrigins", v); kraken_.setOrigins(v);
        });
        rf->addRow("Ring origins:", originSpin_);

        row->addWidget(zonesBox); row->addWidget(ringBox); row->addStretch(0);
        av->addLayout(row);
    }

    // Colour mapping — a 3-way radio, so the exclusivity is enforced by the widget and the default
    // "same colour everywhere" state is visible, instead of two checkboxes that manually un-tick
    // each other with the default hidden behind "neither ticked".
    {
        spread_ = s.value("mirror/spread", false).toBool();
        wrap_   = s.value("mirror/wrap", false).toBool();
        auto* mapBox = new QGroupBox("Colour mapping", advBody);
        auto* mv = new QVBoxLayout(mapBox);
        mapSame_   = new QRadioButton("Same colour on every device", mapBox);
        mapSpread_ = new QRadioButton("Spread — the whole strip on each device", mapBox);
        mapWrap_   = new QRadioButton("Wrap — the strip across all devices in turn", mapBox);
        mapSpread_->setToolTip("Each device stretches the ENTIRE WLED strip across its own LEDs.");
        mapWrap_->setToolTip("Lay the WLED strip once across all ticked devices in sequence, so the\n"
                             "colour flows from one device to the next.");
        auto* grp = new QButtonGroup(this);
        grp->addButton(mapSame_); grp->addButton(mapSpread_); grp->addButton(mapWrap_);
        (wrap_ ? mapWrap_ : spread_ ? mapSpread_ : mapSame_)->setChecked(true);
        auto applyMap = [this]{
            spread_ = mapSpread_->isChecked(); wrap_ = mapWrap_->isChecked();
            QSettings().setValue("mirror/spread", spread_);
            QSettings().setValue("mirror/wrap", wrap_);
        };
        connect(mapSame_,   &QRadioButton::toggled, this, [applyMap]{ applyMap(); });
        connect(mapSpread_, &QRadioButton::toggled, this, [applyMap]{ applyMap(); });
        connect(mapWrap_,   &QRadioButton::toggled, this, [applyMap]{ applyMap(); });
        mv->addWidget(mapSame_); mv->addWidget(mapSpread_); mv->addWidget(mapWrap_);

        gammaChk_ = new QCheckBox("Match strip gamma (recommended)", mapBox);
        gammaChk_->setChecked(s.value("mirror/gamma", true).toBool());
        gammaChk_->setToolTip("Send the colours through the same gamma curve WLED uses for the strip.\n"
                              "Without it the PC is brighter than the strip at low levels, and very dark\n"
                              "colours the strip shows as black can still glow on the PC. This darkens the\n"
                              "whole mirror — raise Brightness to suit.");
        connect(gammaChk_, &QCheckBox::toggled, this, [](bool on){ QSettings().setValue("mirror/gamma", on); });
        mv->addWidget(gammaChk_);
        av->addWidget(mapBox);
    }

    // Music-reactive only (background calibration).
    {
        bgCal_ = QColor(s.value("mirror/bgCal").toString());     // invalid == never calibrated
        auto* reBox = new QGroupBox("Music-reactive only", advBody);
        auto* rv = new QVBoxLayout(reBox);
        stripBgChk_ = new QCheckBox("Remove the background colour", reBox);
        stripBgChk_->setChecked(s.value("mirror/stripBg", false).toBool());
        stripBgChk_->setToolTip("Subtract the always-on background colour, so only the music-reactive part\n"
                                "lights the PC. Stop the music first, then tick this box.");
        calBtn_ = new QPushButton("Set from now", reBox);
        calBtn_->setToolTip("Store the colour WLED shows right now as the background.\n"
                            "Do this with the music stopped.");
        bgSwatch_ = new QLabel(reBox); bgSwatch_->setFixedSize(26, 16);
        paintSwatch(bgSwatch_, bgCal_.isValid() ? bgCal_ : QColor(Qt::black));
        bgLabel_  = new QLabel(reBox);
        connect(calBtn_, &QPushButton::clicked, this, &MainWindow::calibrateBg);
        connect(stripBgChk_, &QCheckBox::toggled, this, [this](bool on){
            QSettings().setValue("mirror/stripBg", on);
            // Ticking the box IS the first calibration; after that the stored value is reused.
            if (on && !bgCal_.isValid()) calibrateBg(); else refreshBgUi();
        });
        { auto* hb = new QHBoxLayout; hb->addWidget(stripBgChk_);
          hb->addSpacing(10); hb->addWidget(new QLabel("Background:", reBox));
          hb->addWidget(bgSwatch_); hb->addWidget(bgLabel_); hb->addWidget(calBtn_); hb->addStretch(1);
          rv->addLayout(hb); }
        auto* calHint = new QLabel("Stop the music so the strip shows just the background, then tick the box "
                                   "(or press “Set from now”). The app measures for about a second and subtracts "
                                   "that colour, so only the reactive part reaches the PC.", reBox);
        calHint->setWordWrap(true);
        calHint->setStyleSheet("color:" + hintColour(central) + ";");
        rv->addWidget(calHint);
        av->addWidget(reBox);
    }

    // Hidden devices — a bespoke driver owns them, so OpenRGB doesn't; re-add hands one back to
    // OpenRGB. Session only (resets on restart).
    {
        auto* hidBox = new QGroupBox("Hidden devices", advBody);
        auto* hf = new QHBoxLayout(hidBox);
        blacklistCombo_ = new QComboBox(hidBox);
        blacklistCombo_->setMinimumWidth(200);
        auto* readd = new QPushButton("Re-add to scan", hidBox);
        readd->setToolTip("Show this device in the list again and let OpenRGB drive it (on the next Mirror start).");
        repopulateBlacklist();
        connect(readd, &QPushButton::clicked, this, [this]{
            if (!blacklistCombo_ || blacklistCombo_->count() == 0) return;
            const QString sel = blacklistCombo_->currentText();
            blacklist_.removeAll(sel);
            repopulateBlacklist();
            refresh();
            status_->setText("Re-added \"" + sel + "\" to the scan — start Mirror again for OpenRGB to drive it.");
        });
        hf->addWidget(new QLabel("Driven directly (not OpenRGB):", hidBox));
        hf->addWidget(blacklistCombo_, 1); hf->addWidget(readd);
        av->addWidget(hidBox);
    }

    // --- options group --------------------------------------------------------
    auto* opts = new QGroupBox("Options", central);
    auto* og = new QVBoxLayout(opts);
    autoMirrorChk_ = new QCheckBox("Auto-mirror on launch", opts);
    autostartChk_  = new QCheckBox("Launch at login", opts);
    startMinChk_   = new QCheckBox("Start minimised to tray", opts);
    autoMirrorChk_->setChecked(s.value("opts/autoMirror", false).toBool());
    autostartChk_->setChecked(s.value("opts/autostart", false).toBool());
    startMinChk_->setChecked(s.value("opts/startMin", false).toBool());
    og->addWidget(autoMirrorChk_); og->addWidget(autostartChk_); og->addWidget(startMinChk_);

    // --- assemble -------------------------------------------------------------
    layout->addWidget(setup);
    layout->addWidget(status_);
    layout->addWidget(tip);
    layout->addWidget(devBox, 1);
    layout->addLayout(bRow);
    layout->addSpacing(6);
    layout->addWidget(mirBtn_);
    layout->addWidget(adv);
    layout->addWidget(opts);
    setCentralWidget(central);

    // --- signals --------------------------------------------------------------
    // (Rescan / Size zones / colour-mapping radios / gamma are wired at creation, in Advanced.)
    connect(setMod, &QPushButton::clicked, this, &MainWindow::setSelectedMode);
    connect(applyHost, &QPushButton::clicked, this, &MainWindow::connectHostFromField);
    connect(hostEdit_, &QLineEdit::returnPressed, this, &MainWindow::connectHostFromField);
    connect(autoMirrorChk_, &QCheckBox::toggled, this, [](bool on){ QSettings().setValue("opts/autoMirror", on); });
    connect(startMinChk_,   &QCheckBox::toggled, this, [](bool on){ QSettings().setValue("opts/startMin", on); });
    connect(autostartChk_,  &QCheckBox::toggled, this, [this](bool on){ setAutostart(on); QSettings().setValue("opts/autostart", on); });

    // --- tray -----------------------------------------------------------------
    tray_ = new QSystemTrayIcon(this);
    tray_->setIcon(appIcon());
    tray_->setToolTip(baseTitle_);
    auto* trayMenu = new QMenu(this);
    connect(trayMenu->addAction("Show WLED PC RGB"), &QAction::triggered, this, &MainWindow::showAndRaise);
    trayMirror_ = trayMenu->addAction("Mirror WLED");
    trayMirror_->setCheckable(true);
    connect(trayMirror_, &QAction::toggled, this, &MainWindow::setMirroring);
    connect(trayMenu->addAction("Rescan devices"), &QAction::triggered, this, &MainWindow::refresh);
    trayMenu->addSeparator();
    const QString repo = "https://github.com/dotatlas/wled-pc-rgb";
    connect(trayMenu->addAction("Documentation…"), &QAction::triggered, this,
            [repo]{ QDesktopServices::openUrl(QUrl(repo + "/blob/main/docs/USAGE.md")); });
    connect(trayMenu->addAction("Report a bug…"), &QAction::triggered, this,
            [repo]{ QDesktopServices::openUrl(QUrl(repo + "/issues")); });
    connect(trayMenu->addAction("About WLED PC RGB…"), &QAction::triggered, this, &MainWindow::openAbout);
    trayMenu->addSeparator();
    connect(trayMenu->addAction("Quit"), &QAction::triggered, qApp, &QApplication::quit);
    tray_->setContextMenu(trayMenu);
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r){
        if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) showAndRaise();
    });
    tray_->show();

    // --- bring everything up --------------------------------------------------
    startOpenRGB();
    refresh();
    startBackend();

    ipc_ = new IpcClient(this);
    connect(ipc_, &IpcClient::connectionChanged, this, [this](bool c) {
        backendUp_ = c;
        setDot(dotB_, c ? 3 : 1, c ? "Backend connected" : "Backend starting / reconnecting…");
        if (!c) setDot(dotW_, 0, "Waiting for backend…");
        else maybeAutoMirror();
    });
    connect(ipc_, &IpcClient::hello, this, [this](const QString& n, int leds, bool ok) {
        dotWtxt_->setText(n.isEmpty() ? "WLED" : "WLED · " + n);
        setDot(dotW_, ok ? 3 : 1, ok ? QString("%1 · %2 LEDs").arg(n).arg(leds)
                                     : "WLED unreachable at " + wledHost_);
    });
    connect(ipc_, &IpcClient::wledState, this, [this](bool reachable, bool on, int bri, const QString& src) {
        wledReachable_ = reachable; wledOn_ = on;
        const int lvl = reachable ? (on ? 3 : 2) : 1;
        setDot(dotW_, lvl, reachable ? QString("%1 · bri %2 · %3")
                                           .arg(on ? "on" : "off").arg(bri).arg(src)
                                     : "WLED unreachable at " + wledHost_);
    });
    connect(ipc_, &IpcClient::frame, this, [this](const QColor& avg, const QList<QColor>& cols) {
        wledColour_ = avg;                                 // always the RAW strip colour

        // A calibration in progress accumulates the per-channel maximum over the window.
        if (calibrating_) {
            calMax_ = QColor(qMax(calMax_.red(),   avg.red()),
                             qMax(calMax_.green(), avg.green()),
                             qMax(calMax_.blue(),  avg.blue()));
            ++calFrames_;
            if (calTimer_.elapsed() >= 1000 && calFrames_ >= 3) finishCalibration();
        }

        const int b = bright_->value();
        const bool off = !wledOn_;                         // WLED powered off → PC off
        const bool strip = stripBgChk_ && stripBgChk_->isChecked() && bgCal_.isValid();
        const bool gam = gammaChk_ && gammaChk_->isChecked();

        // "Reactive only": subtract the calibrated background from every bucket. Done here, once, so
        // all four pipelines below (Kraken, GPU, OpenRGB positional, OpenRGB solid) inherit it.
        QList<QColor> use = cols;
        QColor useAvg = avg;
        if (strip && !cols.isEmpty()) useAvg = stripBgAll(cols, bgCal_, &use);
        mirrorColour_ = useAvg;                            // what the PC mirrors (pre-brightness)
        // Output order is strip -> scale -> gamma, exactly WLED's own order. Gamma must be LAST: it is
        // non-linear, so applying it before the subtraction would break the additive arithmetic.
        const QColor pc = off ? QColor(0, 0, 0)
                              : (gam ? gammaOut(scale(useAvg, b)) : scale(useAvg, b));

        // Cosmetics (swatches + title) don't need the full frame rate — throttle to ~10 Hz
        // so the GUI doesn't churn while devices update every frame.
        static QElapsedTimer cos;
        if (!cos.isValid() || cos.elapsed() > 100) {
            cos.restart();
            paintSwatch(swatchW_, off ? QColor(0, 0, 0) : avg);   // WLED as-is …
            paintSwatch(swatchP_, pc);                            // … next to what the PC shows
            setWindowTitle(baseTitle_ + " · WLED " + avg.name()
                           + (strip ? "  −  bg " + bgCal_.name() : QString()));
        }
        if (!mirroring_) return;

        // Scaled per-LED strip, built once per frame and shared by every pipeline below.
        QList<QColor> sc;
        if (!off) {
            sc.reserve(use.size());
            for (const QColor& c : use) sc.push_back(gam ? gammaOut(scale(c, b)) : scale(c, b));
        }

        // --- Kraken pipeline (direct HID) -------------------------------------
        // Its own device-specific path, independent of the OpenRGB socket: always per-LED so
        // the ring shows a moving gradient (the driver resamples the strip to its 24 LEDs).
        if (krakenDriving_) {
            static QElapsedTimer kReopen;                  // self-heal: a dropped write closes the handle
            if (!kraken_.isOpen() && (!kReopen.isValid() || kReopen.elapsed() > 2000)) {
                kReopen.restart(); kraken_.open();
            }
            if (kraken_.isOpen()) {
                if (off || sc.isEmpty()) kraken_.setRingColor(QColor(0, 0, 0));   // stay in step with the black OpenRGB devices
                else                     kraken_.setRing(sc);
            }
        }

        // --- GPU pipeline (direct NVAPI I2C) ----------------------------------
        // Also its own path: per-LED across the logo + fan strips, self-healing (a reopen
        // re-enumerates the GPU handle, which is the fix for NVIDIA's stale-handle bug).
        if (gpuDriving_) {
            static QElapsedTimer gReopen;
            if (!gpu_.isOpen() && (!gReopen.isValid() || gReopen.elapsed() > 2000)) {
                gReopen.restart();
                if (!gpu_.open() && gpu_.isLatched()) {
                    // The driver has given up for this session. Release ownership so OpenRGB gets
                    // the card back — otherwise it would be driven by nothing (the v1.3 failure).
                    gpuDriving_ = false;
                    pushIncluded();
                    status_->setText("GPU direct control stopped after repeated errors — OpenRGB has it again.");
                }
            }
            if (gpuDriving_ && gpu_.isOpen()) {
                if (off || sc.isEmpty()) gpu_.setColor(QColor(0, 0, 0));
                else                     gpu_.setLeds(sc);
            }
        }

        // --- OpenRGB pipeline (fans, GPU, mouse, motherboard) -----------------
        static QElapsedTimer reopen;                       // self-heal the mirror socket
        if (!mirror_.alive() && (!reopen.isValid() || reopen.elapsed() > 2000)) {
            reopen.restart(); QString e;
            // Restate ownership first: close() cleared it, and open() consults it to decide which
            // devices to leave alone. Without this the reopen would mode-switch a card we own.
            mirror_.setOwnedExternally(gpuDriving_ ? allGpuRowIndices() : QList<int>{});
            if (mirror_.open(kHost, kPort, &e)) pushIncluded();
        }
        if (!mirror_.alive()) return;

        if (off) { mirror_.apply(QColor(0, 0, 0)); return; }   // WLED off → PC off

        if (wrap_ || spread_) {                            // positional modes need the per-LED strip
            if (wrap_) mirror_.applyWrapped(sc); else mirror_.applyBuckets(sc);
        } else {
            mirror_.apply(pc);
        }
    });
    ipc_->start(kIpcPort);

    refreshBgUi();
    refreshMirrorGate();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    QSettings().setValue("win/geometry", saveGeometry());
    if (tray_ && tray_->isVisible()) {
        static bool told = false;
        hide();
        if (!told) { tray_->showMessage("WLED PC RGB", "Still running in the tray — right-click to quit.",
                                        QSystemTrayIcon::Information, 3000); told = true; }
        e->ignore();
    } else {
        e->accept();
    }
}

void MainWindow::showAndRaise() { showNormal(); raise(); activateWindow(); }

void MainWindow::maybeAutoMirror() {
    if (autoMirrorChk_ && autoMirrorChk_->isChecked() && openrgbReady_ && backendUp_
        && !mirroring_ && !gatherChecked().isEmpty())
        setMirroring(true);
}

void MainWindow::setMirroring(bool on) {
    if (on && !mirroring_) {
        // OpenRGB must ALWAYS skip the Kraken — its HID pipeline owns the ring (OpenRGB's HUE2
        // path is wrong for this model and would fight our writes) — plus any user blacklist.
        QStringList skip = blacklist_;
        if (!skip.contains("kraken", Qt::CaseInsensitive)) skip << "kraken";
        mirror_.setSkip(skip);
        // State ownership explicitly BEFORE open() reads it. gpu_ is not open yet at this point, so
        // nothing is owned — never inherit a previous session's value, or open() would skip the
        // mode-switch for whatever device now holds that index (indices shift between scans).
        mirror_.setOwnedExternally(gpuDriving_ ? allGpuRowIndices() : QList<int>{});
        QString e;
        if (!mirror_.open(kHost, kPort, &e)) { status_->setText("⚠  " + e); on = false; }
        else {
            mirroring_ = true;
            syncKrakenDriving();            // start the ring pipeline if the Kraken row is ticked
            // The GPU pipeline opens AFTER OpenRGB, so OpenRGB's one-time Direct-mode switch cannot
            // land on top of our per-LED enable. syncGpuDriving() re-pushes the included set itself,
            // which is what actually stops OpenRGB driving the card.
            syncGpuDriving();
            pushIncluded();                 // (also covers the case where the GPU did not open)
        }
    } else if (!on && mirroring_) {
        // Release the bespoke pipelines while mirroring_ is still true, so syncGpuDriving()'s
        // re-push actually runs (it is gated on mirroring_) and ownership is cleared in order —
        // handing each device back and re-asserting its mode BEFORE the socket goes away.
        const bool wasKraken = krakenDriving_, wasGpu = gpuDriving_;
        krakenDriving_ = false; gpuDriving_ = false;
        if (wasKraken) { kraken_.setRingColor(QColor(0, 0, 0)); kraken_.close(); }
        if (wasGpu)    { gpu_.blackout(); gpu_.close(); }
        pushIncluded();                    // clears ownership + re-asserts the released modes
        mirroring_ = false;
        mirror_.close(); status_->setText("Mirror off.");
    }
    QSignalBlocker b1(mirBtn_), b2(trayMirror_);
    mirBtn_->setChecked(mirroring_);
    mirBtn_->setText(mirroring_ ? "■  Stop mirroring" : "▶  Mirror WLED");
    if (trayMirror_) trayMirror_->setChecked(mirroring_);
}

void MainWindow::connectHostFromField() {
    const QString h = hostEdit_->text().trimmed();
    if (h.isEmpty() || h == wledHost_) return;
    wledHost_ = h;
    QSettings().setValue("wled/host", h);
    setDot(dotW_, 2, "Connecting to " + h + "…");
    dotWtxt_->setText("WLED");
    if (ipc_) ipc_->sendHost(h);          // live-retarget the running backend
    status_->setText("WLED host → " + h);
}

void MainWindow::startBackend() {
    { QTcpSocket probe; probe.connectToHost(kHost, kIpcPort);
      if (probe.waitForConnected(300)) { probe.abort(); return; } }
    const QString java   = findJava();
    const QString script = QCoreApplication::applicationDirPath() + "/WledBackend.java";
    if (!QFile::exists(script)) { setDot(dotB_, 1, "WledBackend.java not found next to the app."); return; }
    backend_ = new QProcess(this);
    backend_->setProgram(java);
    backend_->setArguments({script, wledHost_, QString::number(kIpcPort)});
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]{ stopping_ = true; if (backend_) backend_->kill(); });
    connect(backend_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError){
        setDot(dotB_, 1, "Backend failed to start — is Java installed?");
    });
    connect(backend_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) {
                if (stopping_) return;
                backendDelayMs_ = qMin(backendDelayMs_ + 1500, 15000);   // back off on repeated crashes
                setDot(dotB_, 1, QString("Backend exited — restarting in %1s…").arg(backendDelayMs_/1000));
                QTimer::singleShot(backendDelayMs_, this, [this]{ if (!stopping_ && backend_) backend_->start(); });
            });
    backend_->start();
    setDot(dotB_, 2, "Starting backend…");
}

QString MainWindow::findOpenRGB() {
    const QStringList candidates = {
        QStringLiteral("C:/.software/OpenRGB/OpenRGB Windows 64-bit/OpenRGB.exe"),
        qEnvironmentVariable("ProgramFiles") + "/OpenRGB/OpenRGB.exe",
        qEnvironmentVariable("LOCALAPPDATA") + "/OpenRGB/OpenRGB.exe",
    };
    for (const QString& c : candidates) if (QFile::exists(c)) return c;
    return QString();
}

void MainWindow::startOpenRGB() {
    { QTcpSocket probe; probe.connectToHost(kHost, kPort);
      if (probe.waitForConnected(300)) { probe.abort(); return; } }   // already running — use it
    const QString exe = findOpenRGB();
    if (exe.isEmpty()) {
        setDot(dotO_, 1, "OpenRGB not found — install it or start it with --server.");
        status_->setText("OpenRGB not found — install it, or start it manually with --server.");
        return;
    }
    // ALWAYS non-elevated: elevated OpenRGB probes the motherboard SMBus (DDR5), which risks the
    // RAM. Nothing needs elevation any more — the GPU has its own NVAPI pipeline (GpuDriver), which
    // uses the GPU's own I2C bus and no admin rights, so the old "Elevate OpenRGB" button is gone.
    QProcess::startDetached(exe, QStringList{ "--server", "--noautoconnect" });
    setDot(dotO_, 2, "Starting OpenRGB…");
    // USB-HID devices (Kraken, mouse) enumerate a few seconds after OpenRGB starts;
    // the initial scan can miss them. Re-scan a couple of times to catch late arrivals.
    QTimer::singleShot(6000,  this, &MainWindow::refresh);
    QTimer::singleShot(13000, this, &MainWindow::refresh);
}

// Is the Kraken present in the list AND ticked? If so we drive its ring over HID; if not
// (unticked, or hidden/absent), we leave the ring to NZXT CAM.
bool MainWindow::krakenSelected() const {
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* it = tree_->topLevelItem(i);
        if (!it->data(0, kDeviceIndexRole).isValid()) continue;
        if (!it->text(0).contains("kraken", Qt::CaseInsensitive)) continue;
        return (it->flags() & Qt::ItemIsUserCheckable) && it->checkState(0) == Qt::Checked;
    }
    return false;
}

// Open/close the Kraken HID pipeline to match krakenSelected() while mirroring. Idempotent —
// safe to call on any checkbox change or mirror start/stop.
void MainWindow::syncKrakenDriving() {
    const bool want = mirroring_ && krakenSelected();
    if (want && !krakenDriving_) {
        krakenDriving_ = kraken_.open();          // false if the Kraken isn't present / can't open
    } else if (!want && krakenDriving_) {
        kraken_.setRingColor(QColor(0, 0, 0));    // ring off, then release the handle for CAM
        kraken_.close();
        krakenDriving_ = false;
    }
}

// Device indices of EVERY ticked GPU row (OpenRGB type 2). Matched by device type, not by name, so
// it holds for any vendor. We return them all because our driver binds a card by PCI id, which
// cannot be correlated with an OpenRGB row: on a two-GPU machine, guessing wrong would leave
// OpenRGB driving the very card we stream to (two writers on one I2C bus). Releasing every ticked
// GPU row instead means the worst case is one GPU unlit, never a write conflict.
QList<int> MainWindow::gpuRowIndices() const {
    QList<int> out;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* it = tree_->topLevelItem(i);
        const QVariant v = it->data(0, kDeviceIndexRole);
        if (!v.isValid()) continue;
        if (it->data(0, kDeviceTypeRole).toInt() != 2) continue;
        if ((it->flags() & Qt::ItemIsUserCheckable) && it->checkState(0) == Qt::Checked) out << v.toInt();
    }
    return out;
}

// Every GPU row, ticked or not. Used for the "owned" set: our driver binds a card by PCI id, which
// can't be mapped to a row, so an UNTICKED GPU row could still be the card we stream to — and
// OpenRGB must not mode-switch it. Unticked rows are excluded from included_ anyway, so marking
// them owned costs nothing.
QList<int> MainWindow::allGpuRowIndices() const {
    QList<int> out;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* it = tree_->topLevelItem(i);
        const QVariant v = it->data(0, kDeviceIndexRole);
        if (!v.isValid()) continue;
        if (it->data(0, kDeviceTypeRole).toInt() == 2) out << v.toInt();
    }
    return out;
}

// Open/close the GPU NVAPI pipeline to match gpuSelected() while mirroring. Idempotent. Whenever
// ownership changes it re-pushes the included set, so OpenRGB stops/starts driving the card in the
// same breath — ownership is re-evaluated live rather than fixed at mirror start (which is what let
// both writers hold the card at once).
void MainWindow::syncGpuDriving() {
    const bool want = mirroring_ && gpuSelected();
    const bool was = gpuDriving_;
    if (want && !gpuDriving_) {
        gpuDriving_ = gpu_.open();                // false if this GPU isn't one we support
    } else if (!want && gpuDriving_) {
        gpu_.blackout();                          // really darken it (bypasses the rate cap) …
        gpu_.close();                             // … then release, so MSI Center/SignalRGB can take it
        gpuDriving_ = false;
    }
    if (gpuDriving_ != was && mirroring_) pushIncluded();   // hand the device over / back immediately
}

QList<int> MainWindow::gatherChecked() {
    QList<int> out;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* it = tree_->topLevelItem(i);
        const QVariant v = it->data(0, kDeviceIndexRole);
        if (v.isValid() && (it->flags() & Qt::ItemIsUserCheckable) && it->checkState(0) == Qt::Checked)
            out.push_back(v.toInt());
    }
    return out;
}

// Start a calibration: confirm that the music is stopped, then measure for about a second.
//
// Two things this deliberately does NOT do. It does not capture silently — storing a reference while
// music is playing bakes the reactive layer into the background and the user would never be told. And
// it does not trust a single frame: one frame carries the full per-frame noise and lands below the
// true pedestal about half the time, which the one-sided clamp then turns into a permanent floor.
// It also does not require WLED to be reachable: a DDP or E1.31 source streams without it.
void MainWindow::calibrateBg() {
    if (calibrating_) return;
    const auto r = QMessageBox::information(this, "Set the background colour",
        "Stop the music first, so the strip shows only the background colour.\n\n"
        "When you click OK the app measures for about one second, and stores what it sees as the "
        "background. Then start the music again.",
        QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok);
    if (r != QMessageBox::Ok) { refreshBgUi(); return; }

    calibrating_ = true; calFrames_ = 0; calMax_ = QColor(0, 0, 0);
    calTimer_.restart();
    if (calBtn_) { calBtn_->setEnabled(false); calBtn_->setText("Measuring…"); }
    status_->setText("Reactive only: measuring the background colour — hold still…");
    // Give up if no frames arrive at all, rather than appearing to hang.
    QTimer::singleShot(4000, this, [this]{
        if (!calibrating_) return;
        if (calFrames_ == 0) {
            calibrating_ = false;
            if (calBtn_) calBtn_->setText("Set from now");
            refreshBgUi();
            status_->setText("Reactive only: no WLED data arrived — check the WLED dot, then try again.");
        } else {
            finishCalibration();       // fewer frames than hoped, but enough to use
        }
    });
}

void MainWindow::finishCalibration() {
    if (!calibrating_) return;
    calibrating_ = false;
    bgCal_ = calMax_;
    QSettings().setValue("mirror/bgCal", bgCal_.name());
    if (calBtn_) calBtn_->setText("Set from now");
    refreshBgUi();
    status_->setText(QString("Reactive only: background set to %1 (from %2 frames). Only the reactive "
                             "part now reaches the PC.").arg(bgCal_.name()).arg(calFrames_));
}

void MainWindow::refreshBgUi() {
    if (!bgSwatch_ || !bgLabel_ || !calBtn_) return;
    const bool on = stripBgChk_ && stripBgChk_->isChecked();
    paintSwatch(bgSwatch_, bgCal_.isValid() ? bgCal_ : QColor(Qt::black));
    bgLabel_->setText(bgCal_.isValid() ? bgCal_.name() : QString("not set"));
    bgLabel_->setStyleSheet(bgCal_.isValid() ? "" : "color:#e9a13b;");
    calBtn_->setEnabled(on && !calibrating_);
    bgSwatch_->setEnabled(on);
    bgLabel_->setEnabled(on);
}

void MainWindow::repopulateBlacklist() {
    if (!blacklistCombo_) return;
    blacklistCombo_->clear();
    blacklistCombo_->addItems(blacklist_);
}

void MainWindow::pushIncluded() {
    QList<int> inc = gatherChecked();
    // Our NVAPI pipeline owns the GPU while it is open, so OpenRGB must neither drive it (included_)
    // nor mode-switch it on a reopen (ownedExternally). Both are set from one place, so ownership is
    // always consistent, and because included_ is consulted on every frame this takes effect at once
    // — no mirror reopen, and no window where both writers hold the card.
    if (gpuDriving_) {
        for (int gi : gpuRowIndices()) inc.removeAll(gi);        // OpenRGB must not DRIVE it …
        mirror_.setOwnedExternally(allGpuRowIndices());          // … nor mode-switch any GPU row
    } else {
        mirror_.setOwnedExternally({});                          // released: modes get re-asserted
    }
    mirror_.setIncluded(inc);
    if (mirroring_) status_->setText(QString("Mirroring WLED onto %1 device(s)%2.")
                                         .arg(mirror_.deviceCount())
                                         .arg(gpuDriving_ ? " + the GPU direct" : ""));
}

void MainWindow::setDot(QLabel* dot, int level, const QString& hint) {
    if (!dot) return;
    // Colour AND glyph carry the state, so a red/green-deficient viewer can still read it: an idle
    // ring, an error cross, a busy half-circle, a ready tick. The tooltip gives the detail.
    static const char* col[]   = { "#888", "#e33", "#e9a13b", "#2a8f5a" };
    static const char* glyph[] = { "○",    "✕",    "◐",       "✓"       };
    const int i = qBound(0, level, 3);
    dot->setText(glyph[i]);
    dot->setStyleSheet(QString("color:%1; font-size:15px;").arg(col[i]));
    dot->setToolTip(hint);
    dot->setAccessibleName(hint);
}

void MainWindow::refreshMirrorGate() {
    const bool ready = openrgbReady_ && !gatherChecked().isEmpty();
    if (mirBtn_)     mirBtn_->setEnabled(ready || mirroring_);
    if (trayMirror_) trayMirror_->setEnabled(ready || mirroring_);
}

void MainWindow::setAutostart(bool on) {
    QSettings run("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    if (on) {
        const QString cmd = QString("\"%1\" --minimized")
                                .arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
        run.setValue("wled-pc-rgb", cmd);
    } else {
        run.remove("wled-pc-rgb");
    }
}

static QIcon swatchIcon(const QColor& c) { QPixmap pm(14, 14); pm.fill(c); return QIcon(pm); }

void MainWindow::refresh() {
    building_ = true;
    tree_->clear();
    const int zoneLeds = zoneSpin_ ? zoneSpin_->value() : kDefaultZoneLeds;
    OrgbClient::resizeZones(kHost, kPort, zoneLeds, /*onlyZero*/true, nullptr);

    QString err;
    auto devices = OrgbClient::load(kHost, kPort, &err);
    if (!err.isEmpty()) {
        openrgbReady_ = false;
        setDot(dotO_, 1, err);
        status_->setText("⚠  " + err + "   (retrying…)");
        baseTitle_ = "WLED PC RGB — connecting to OpenRGB…";
        setWindowTitle(baseTitle_);
        building_ = false;
        refreshMirrorGate();
        QTimer::singleShot(2000, this, &MainWindow::refresh);   // keep trying until OpenRGB is up
        return;
    }
    if (devices.empty() && zeroRetries_ < 6) {          // connected but OpenRGB still detecting — wait
        ++zeroRetries_;
        setDot(dotO_, 2, "OpenRGB connected, detecting devices…");
        status_->setText("OpenRGB connected but no devices yet — waiting for detection…");
        baseTitle_ = "WLED PC RGB — detecting devices…";
        setWindowTitle(baseTitle_);
        building_ = false;
        refreshMirrorGate();
        QTimer::singleShot(2000, this, &MainWindow::refresh);
        return;
    }
    zeroRetries_ = 0;
    openrgbReady_ = true;

    int zoneTotal = 0, ledTotal = 0, mirrorable = 0;
    for (int di = 0; di < int(devices.size()); ++di) {
        const auto& d = devices[di];
        bool blacked = false;                        // hidden from the scan (a bespoke pipeline owns it)
        for (const QString& b : blacklist_) if (d.name.contains(b, Qt::CaseInsensitive)) { blacked = true; break; }
        if (blacked) continue;

        const bool isDram    = (d.type == 1);
        const bool isGpu     = (d.type == 2);
        const bool isKraken  = d.name.contains("kraken", Qt::CaseInsensitive);
        const bool canMirror = (!isDram && !d.leds.empty());

        // Short badge in the Detail column; the full explanation goes in the row tooltip so the
        // list stays scannable instead of carrying a sentence on every device name.
        QString tag, detail;
        if (isDram)        { tag = "skipped";
                             detail = "RAM — excluded for safety. The app never drives the DIMMs, to protect the memory."; }
        else if (isKraken) { tag = "direct (USB)";
                             detail = "NZXT Kraken ring — driven directly over USB. Untick to leave it to NZXT CAM."; }
        else if (isGpu)    { const bool ok = GpuDriver::supportedHere();
                             tag = ok ? "direct (NVAPI)" : "needs admin";
                             detail = ok ? "GPU — driven directly over NVAPI. No administrator rights needed."
                                         : "GPU — needs OpenRGB run as administrator to light."; }
        auto* dItem = new QTreeWidgetItem(tree_, {d.name, tag});
        if (!detail.isEmpty()) { dItem->setToolTip(0, detail); dItem->setToolTip(1, detail); }
        dItem->setData(0, kDeviceIndexRole, di);
        dItem->setData(0, kDeviceTypeRole, d.type);
        if (canMirror) {
            dItem->setFlags(dItem->flags() | Qt::ItemIsUserCheckable);
            dItem->setCheckState(0, Qt::Checked);   // all devices enabled by default, always
            ++mirrorable;
        }
        for (const auto& z : d.zones) {
            new QTreeWidgetItem(dItem, {z.name, QString("%1 LEDs").arg(z.ledCount)});
            ++zoneTotal;
        }
        const QString active = (d.activeMode >= 0 && d.activeMode < int(d.modes.size()))
                                   ? d.modes[size_t(d.activeMode)].name : QString("?");
        auto* modesNode = new QTreeWidgetItem(dItem, {QString("Modes (%1)").arg(d.modes.size()), "active: " + active});
        for (int mi = 0; mi < int(d.modes.size()); ++mi) {
            auto* mItem = new QTreeWidgetItem(modesNode, {d.modes[size_t(mi)].name, mi == d.activeMode ? "● active" : ""});
            mItem->setData(0, kDeviceIndexRole, di);
            mItem->setData(0, kModeIndexRole, mi);
        }
        auto* ledsNode = new QTreeWidgetItem(dItem, {QString("LEDs (%1)").arg(int(d.leds.size())), ""});
        for (const auto& l : d.leds) {
            auto* li = new QTreeWidgetItem(ledsNode, {l.name, l.color.name()});
            li->setIcon(1, swatchIcon(l.color));
            ++ledTotal;
        }
        dItem->setExpanded(false);
    }
    setDot(dotO_, 3, QString("%1 devices · %2 mirror-able").arg(devices.size()).arg(mirrorable));
    status_->setText(QString("%1 devices (%2 mirror-able) · %3 zones · %4 LEDs — tick devices, then Mirror WLED")
                         .arg(devices.size()).arg(mirrorable).arg(zoneTotal).arg(ledTotal));
    baseTitle_ = QString("WLED PC RGB %1 — %2 devices").arg(QCoreApplication::applicationVersion()).arg(devices.size());
    setWindowTitle(baseTitle_);
    building_ = false;
    // A rescan re-ticks rows while building_ suppresses itemChanged, so both bespoke pipelines are
    // reconciled here (e.g. a late-enumerated Kraken/GPU, or one that failed to open earlier).
    // Sync before push, so an owned GPU is never momentarily included.
    if (mirroring_) { syncKrakenDriving(); syncGpuDriving(); pushIncluded(); }
    refreshMirrorGate();
    maybeAutoMirror();

    // Diagnostic dump (temp) so device state can be verified outside the GUI.
    QFile f(QDir::tempPath() + "/wled-pc-rgb-scan.txt");
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&f);
        out << "MOTHERBOARD: " << sysinfo::motherboard() << "\n";
        out << "devices=" << devices.size() << " zones=" << zoneTotal << " leds=" << ledTotal << "\n";
        for (const auto& d : devices)
            out << "DEVICE: " << d.name << "  (type " << d.type << ", active=" << d.activeMode << ", "
                << d.leds.size() << " leds, first="
                << (d.leds.empty() ? QString("-") : d.leds.front().color.name()) << ")\n";
    }
}

void MainWindow::setSelectedMode() { activateMode(tree_->currentItem()); }

void MainWindow::activateMode(QTreeWidgetItem* item) {
    const QVariant dev  = item ? item->data(0, kDeviceIndexRole) : QVariant();
    const QVariant mode = item ? item->data(0, kModeIndexRole)   : QVariant();
    if (!item || !mode.isValid()) { status_->setText("Select or double-click a mode row (under a device's Modes)."); return; }
    // While mirroring, the app keeps every device in its host-controlled (Direct/Static)
    // mode and streams colours per-frame — so a manual mode change would just be overridden
    // (and could fight a device a pipeline owns). Only allow it when the mirror is off.
    if (mirroring_) {
        status_->setText("Devices are kept in Direct mode while mirroring — stop mirroring to change a mode.");
        return;
    }
    QString err;
    if (!OrgbClient::setDeviceMode(kHost, kPort, dev.toInt(), mode.toInt(), &err)) { status_->setText("⚠  " + err); return; }
    // Update the active marker in place — no tree rebuild, so the device stays expanded.
    if (QTreeWidgetItem* modesNode = item->parent()) {
        for (int i = 0; i < modesNode->childCount(); ++i)
            modesNode->child(i)->setText(1, modesNode->child(i) == item ? "● active" : QString());
        modesNode->setText(1, "active: " + item->text(0));
    }
    status_->setText(QString("Activated mode '%1'.").arg(item->text(0)));
}

void MainWindow::maxZones() {
    const int target = zoneSpin_ ? zoneSpin_->value() : kDefaultZoneLeds;
    QString err;
    const int n = OrgbClient::resizeZones(kHost, kPort, target, /*onlyZero*/false, &err);
    if (n >= 0) { status_->setText(QString("Sized %1 motherboard zone(s) to %2 LEDs — rescanning…").arg(n).arg(target)); refresh(); }
    else status_->setText("⚠  " + err);
}
