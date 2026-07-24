// MainWindow — wled-pc-rgb: mirror a WLED instance onto this PC's RGB, live.
// Owns the setup-readiness strip, the device list (per-device mirror toggle),
// the WLED-host field, the primary Mirror button, the live colour swatches, the
// tray icon, and all persisted settings.
#include "main_window.h"
#include "orgb_client.h"
#include "sysinfo.h"
#include "ipc_client.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QSlider>
#include <QSpinBox>
#include <QColorDialog>
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
#include <QSignalBlocker>
#include <QStringList>

namespace {
constexpr int kDeviceIndexRole = Qt::UserRole + 1;
constexpr int kModeIndexRole   = Qt::UserRole + 2;
constexpr auto kHost = "127.0.0.1";
constexpr quint16 kPort = 6742;
constexpr quint16 kIpcPort = 47900;
constexpr int kDefaultZoneLeds = 8;   // MSI JARGB headers are zones of 8 (user-adjustable)

// Mirror colour transform: attenuate by PC brightness (0-100%), then amplify by gain
// (>=100%) so dim WLED flashes read brighter on the PC. Each channel is clamped to
// 0-255 (QColor rejects out-of-range ints, so the clamp is mandatory once gain > 1x).
QColor boost(const QColor& c, int briPct, int gainPct) {
    auto ch = [&](int v){ int x = v * briPct * gainPct / 10000; return x > 255 ? 255 : (x < 0 ? 0 : x); };
    return QColor(ch(c.red()), ch(c.green()), ch(c.blue()));
}

// Minimum-brightness floor: lifts a DIM (but non-black) colour so the PC light doesn't
// drop below `floor255` between flashes. The whole colour is scaled UP so hue is
// preserved (a dim red stays red, just brighter). Pure black is left OFF — when WLED
// goes dark, the PC goes dark too (the floor is for dim content, not for making light
// out of nothing). floor255<=0 → no floor.
QColor withFloor(const QColor& c, int floor255) {
    if (floor255 <= 0) return c;
    if (floor255 > 255) floor255 = 255;
    int m = c.red(); if (c.green() > m) m = c.green(); if (c.blue() > m) m = c.blue();
    if (m == 0 || m >= floor255) return c;   // black stays off; already-bright passes through
    auto up = [&](int v){ int x = v * floor255 / m; return x > 255 ? 255 : x; };
    return QColor(up(c.red()), up(c.green()), up(c.blue()));
}

// When an output LED would be off (pure black — the strip is dark there, or WLED is off),
// either leave it off or substitute a chosen idle colour, per the user's toggle.
QColor withIdle(const QColor& c, bool idleOn, const QColor& idle) {
    if (idleOn && c.red() == 0 && c.green() == 0 && c.blue() == 0) return idle;
    return c;
}

// Full WLED→PC colour map. Order matters: apply the floor to the raw WLED signal FIRST,
// then brightness+gain. That keeps PC brightness the master ceiling — the floor rides
// under it and flashes always vary above it. (Flooring last would flatten every frame to
// a constant glow whenever the floor exceeded the brightness-attenuated ceiling.)
QColor mapColor(const QColor& c, int briPct, int gainPct, int floorPct) {
    return boost(withFloor(c, floorPct * 255 / 100), briPct, gainPct);
}

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
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    QSettings s;
    wledHost_ = s.value("wled/host", "wled.local").toString();
    idleOn_ = s.value("mirror/idleOn", false).toBool();
    idleColor_ = QColor(s.value("mirror/idleColor", idleColor_.name()).toString());

    setWindowTitle(baseTitle_);
    resize(660, 640);
    if (s.contains("win/geometry")) restoreGeometry(s.value("win/geometry").toByteArray());

    auto* central = new QWidget(this);
    auto* layout  = new QVBoxLayout(central);

    // --- motherboard line -----------------------------------------------------
    mobo_ = new QLabel("Motherboard: " + (sysinfo::motherboard().isEmpty() ? QString("(unknown)")
                                                                           : sysinfo::motherboard()), central);
    mobo_->setStyleSheet("font-weight:600;");

    // --- setup readiness group ------------------------------------------------
    auto* setup = new QGroupBox("Setup", central);
    auto* sg = new QGridLayout(setup);

    auto makeDot = [&](QLabel*& dot, QLabel*& txt, const QString& label, int col) {
        dot = new QLabel("●", setup); dot->setStyleSheet("color:#888;");
        txt = new QLabel(label, setup);
        auto* cell = new QHBoxLayout; cell->setSpacing(4);
        cell->addWidget(dot); cell->addWidget(txt); cell->addStretch(1);
        sg->addLayout(cell, 0, col);
    };
    makeDot(dotO_, dotOtxt_, "OpenRGB", 0);
    makeDot(dotB_, dotBtxt_, "Backend", 1);
    makeDot(dotW_, dotWtxt_, "WLED",    2);

    sg->addWidget(new QLabel("WLED host:", setup), 1, 0);
    hostEdit_ = new QLineEdit(wledHost_, setup);
    hostEdit_->setPlaceholderText("wled.local or 192.168.x.x");
    auto* applyHost = new QPushButton("Apply", setup);
    { auto* hr = new QHBoxLayout; hr->addWidget(hostEdit_, 1); hr->addWidget(applyHost);
      sg->addLayout(hr, 1, 1, 1, 2); }

    swatchW_ = new QLabel(setup); swatchW_->setFixedSize(26, 16); paintSwatch(swatchW_, Qt::black);
    swatchP_ = new QLabel(setup); swatchP_->setFixedSize(26, 16); paintSwatch(swatchP_, Qt::black);
    { auto* sr = new QHBoxLayout; sr->setSpacing(6);
      sr->addWidget(new QLabel("Live:", setup));
      sr->addWidget(new QLabel("WLED", setup)); sr->addWidget(swatchW_);
      sr->addWidget(new QLabel("→  PC", setup)); sr->addWidget(swatchP_);
      sr->addStretch(1);
      sg->addLayout(sr, 2, 0, 1, 3); }

    // --- device tree ----------------------------------------------------------
    status_ = new QLabel("Tick the devices to mirror, then click “Mirror WLED”.", central);
    auto* tip = new QLabel("Each ticked device follows WLED live while mirroring is on. A device only lights in a "
                           "per-LED mode — most default to Direct; the Kraken ring needs Static (double-click its "
                           "mode row). GPU RGB needs OpenRGB run as administrator.", central);
    tip->setWordWrap(true);
    tip->setStyleSheet("color: gray; font-size: 11px;");

    tree_ = new QTreeWidget(central);
    tree_->setHeaderLabels({"Mirror?  Device / Zone / Mode / LED", "Info"});
    tree_->setColumnWidth(0, 400);
    connect(tree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it, int col) {
        if (building_ || col != 0 || !it->data(0, kDeviceIndexRole).isValid()) return;
        if (!(it->flags() & Qt::ItemIsUserCheckable)) return;
        saveCheckedDevices();
        if (mirroring_) pushIncluded();
        refreshMirrorGate();
    });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int) {
        if (it && it->data(0, kModeIndexRole).isValid()) activateMode(it);   // double-click a mode row to select it
    });

    // --- PC brightness --------------------------------------------------------
    auto* bRow = new QHBoxLayout;
    bright_ = new QSlider(Qt::Horizontal, central);
    bright_->setRange(0, 100);
    bright_->setValue(s.value("mirror/brightness", 100).toInt());
    auto* bVal = new QLabel(QString::number(bright_->value()) + "%", central);
    connect(bright_, &QSlider::valueChanged, this, [this, bVal](int v){
        bVal->setText(QString::number(v) + "%");
        QSettings().setValue("mirror/brightness", v);
        paintSwatch(swatchP_, withIdle(mapColor(wledColour_, v, gain_->value(), floor_->value()), idleOn_, idleColor_));
    });
    bRow->addWidget(new QLabel("PC brightness:", central));
    bRow->addWidget(bright_, 1);
    bRow->addWidget(bVal);

    // --- flash gain (multiplier so dim WLED flashes read brighter on the PC) ---
    auto* gRow = new QHBoxLayout;
    gain_ = new QSlider(Qt::Horizontal, central);
    gain_->setRange(100, 500);                 // 1.0x .. 5.0x
    gain_->setSingleStep(10); gain_->setPageStep(50);
    gain_->setValue(s.value("mirror/gain", 100).toInt());
    auto* gLabel = new QLabel("Flash gain:", central);
    const QString gTip = "Multiplies the mirrored colour so dim WLED flashes show up "
                         "brighter on your PC RGB. Clamps at full brightness; 1.0× = no change.";
    gLabel->setToolTip(gTip); gain_->setToolTip(gTip);
    auto* gVal = new QLabel(central);
    auto gText = [](int v){ return QString::number(v / 100.0, 'f', 1) + "×"; };
    gVal->setText(gText(gain_->value()));
    connect(gain_, &QSlider::valueChanged, this, [this, gVal, gText](int v){
        gVal->setText(gText(v));
        QSettings().setValue("mirror/gain", v);
        paintSwatch(swatchP_, withIdle(mapColor(wledColour_, bright_->value(), v, floor_->value()), idleOn_, idleColor_));
    });
    gRow->addWidget(gLabel);
    gRow->addWidget(gain_, 1);
    gRow->addWidget(gVal);

    // --- minimum brightness floor (PC never drops below this — flashes above it) ---
    auto* fRow = new QHBoxLayout;
    floor_ = new QSlider(Qt::Horizontal, central);
    floor_->setRange(0, 100);                  // 0% = follow WLED exactly (can go fully off)
    floor_->setValue(s.value("mirror/floor", 0).toInt());
    auto* fLabel = new QLabel("Min brightness:", central);
    const QString fTip = "Lifts DIM (non-black) content so it doesn't fade to near-off between "
                         "flashes; spikes still flash above it. Pure black and WLED-off stay OFF "
                         "(tick \"When off, show colour\" for an idle glow). 0% = follow WLED exactly.";
    fLabel->setToolTip(fTip); floor_->setToolTip(fTip);
    auto* fVal = new QLabel(QString::number(floor_->value()) + "%", central);
    connect(floor_, &QSlider::valueChanged, this, [this, fVal](int v){
        fVal->setText(QString::number(v) + "%");
        QSettings().setValue("mirror/floor", v);
        paintSwatch(swatchP_, withIdle(mapColor(wledColour_, bright_->value(), gain_->value(), v), idleOn_, idleColor_));
    });
    fRow->addWidget(fLabel);
    fRow->addWidget(floor_, 1);
    fRow->addWidget(fVal);

    // --- idle colour: what an OFF/black LED shows (stay off, or a chosen colour) ---
    auto* iRow = new QHBoxLayout;
    idleChk_ = new QCheckBox("When off, show colour:", central);
    idleChk_->setChecked(idleOn_);
    idleChk_->setToolTip("Unchecked: LEDs that are off/black stay off. Checked: they show the "
                         "colour on the right instead (a static idle glow) — used for dark parts "
                         "of the strip and when WLED is off.");
    idleBtn_ = new QPushButton(central);
    idleBtn_->setFixedWidth(52);
    idleBtn_->setToolTip("Pick the idle colour (used only when the box is ticked).");
    auto paintIdleBtn = [this]{ idleBtn_->setStyleSheet(
        QString("background:%1; border:1px solid #555;").arg(idleColor_.name())); };
    paintIdleBtn();
    connect(idleChk_, &QCheckBox::toggled, this, [this](bool on){ idleOn_ = on; QSettings().setValue("mirror/idleOn", on); });
    connect(idleBtn_, &QPushButton::clicked, this, [this, paintIdleBtn]{
        const QColor c = QColorDialog::getColor(idleColor_, this, "Idle colour");
        if (c.isValid()) { idleColor_ = c; paintIdleBtn(); QSettings().setValue("mirror/idleColor", c.name()); }
    });
    iRow->addWidget(idleChk_);
    iRow->addWidget(idleBtn_);
    iRow->addStretch(1);

    // Paint the PC preview once now (sliders built setValue-before-connect, so none of
    // their lambdas fired): reflects a restored floor / idle colour before the 1st frame.
    paintSwatch(swatchP_, withIdle(mapColor(wledColour_, bright_->value(), gain_->value(), floor_->value()), idleOn_, idleColor_));

    // --- primary mirror button ------------------------------------------------
    mirBtn_ = new QPushButton("▶  Mirror WLED", central);
    mirBtn_->setCheckable(true);
    mirBtn_->setMinimumHeight(38);
    mirBtn_->setStyleSheet("QPushButton{font-weight:700;font-size:14px;}"
                           "QPushButton:checked{background:#2a8f5a;color:white;}");
    connect(mirBtn_, &QPushButton::toggled, this, &MainWindow::setMirroring);

    // --- advanced group -------------------------------------------------------
    auto* adv = new QGroupBox("Advanced", central);
    adv->setCheckable(true);
    adv->setChecked(false);
    auto* ag = new QHBoxLayout(adv);
    auto* rescan = new QPushButton("Rescan", adv);
    auto* maxZ   = new QPushButton("Size zones", adv);
    zoneSpin_ = new QSpinBox(adv);
    zoneSpin_->setRange(1, 512);
    zoneSpin_->setValue(s.value("mirror/zoneLeds", kDefaultZoneLeds).toInt());
    zoneSpin_->setToolTip("LEDs per motherboard ARGB zone (e.g. 8 for an MSI JARGB header). "
                          "Set the number, then click Size zones.");
    connect(zoneSpin_, &QSpinBox::valueChanged, this, [](int v){ QSettings().setValue("mirror/zoneLeds", v); });
    auto* setMod = new QPushButton("Set selected mode", adv);
    spreadChk_   = new QCheckBox("Spread (whole strip per device)", adv);
    spread_ = s.value("mirror/spread", false).toBool();
    spreadChk_->setChecked(spread_);
    spreadChk_->setToolTip("Each device stretches the ENTIRE WLED strip across its own LEDs.");
    wrapChk_     = new QCheckBox("Wrap (strip across all devices)", adv);
    wrap_ = s.value("mirror/wrap", false).toBool();
    wrapChk_->setChecked(wrap_);
    wrapChk_->setToolTip("Distribute the WLED strip ONCE across all ticked devices in sequence, "
                         "so the colour flows from one device to the next.");
    ag->addWidget(rescan); ag->addWidget(maxZ); ag->addWidget(zoneSpin_); ag->addWidget(setMod);
    ag->addWidget(spreadChk_); ag->addWidget(wrapChk_); ag->addStretch(1);
    connect(adv, &QGroupBox::toggled, this, [rescan, maxZ, setMod, this](bool on){
        rescan->setVisible(on); maxZ->setVisible(on); zoneSpin_->setVisible(on); setMod->setVisible(on);
        spreadChk_->setVisible(on); wrapChk_->setVisible(on);
    });
    rescan->setVisible(false); maxZ->setVisible(false); zoneSpin_->setVisible(false); setMod->setVisible(false);
    spreadChk_->setVisible(false); wrapChk_->setVisible(false);

    // --- options group --------------------------------------------------------
    auto* opts = new QGroupBox("Options", central);
    auto* og = new QHBoxLayout(opts);
    autoMirrorChk_ = new QCheckBox("Auto-mirror on launch", opts);
    autostartChk_  = new QCheckBox("Launch at login", opts);
    startMinChk_   = new QCheckBox("Start minimised to tray", opts);
    autoMirrorChk_->setChecked(s.value("opts/autoMirror", false).toBool());
    autostartChk_->setChecked(s.value("opts/autostart", false).toBool());
    startMinChk_->setChecked(s.value("opts/startMin", false).toBool());
    og->addWidget(autoMirrorChk_); og->addWidget(autostartChk_); og->addWidget(startMinChk_); og->addStretch(1);

    // --- assemble -------------------------------------------------------------
    layout->addWidget(mobo_);
    layout->addWidget(setup);
    layout->addWidget(status_);
    layout->addWidget(tip);
    layout->addWidget(new QLabel("Devices to mirror:", central));
    layout->addWidget(tree_, 1);
    layout->addLayout(bRow);
    layout->addLayout(gRow);
    layout->addLayout(fRow);
    layout->addLayout(iRow);
    layout->addWidget(mirBtn_);
    layout->addWidget(adv);
    layout->addWidget(opts);
    setCentralWidget(central);

    // --- signals --------------------------------------------------------------
    connect(rescan, &QPushButton::clicked, this, &MainWindow::refresh);
    connect(maxZ,   &QPushButton::clicked, this, &MainWindow::maxZones);
    connect(setMod, &QPushButton::clicked, this, &MainWindow::setSelectedMode);
    connect(spreadChk_, &QCheckBox::toggled, this, [this](bool on){
        spread_ = on; QSettings().setValue("mirror/spread", on);
        if (on && wrapChk_->isChecked()) wrapChk_->setChecked(false);   // spread & wrap are mutually exclusive
    });
    connect(wrapChk_, &QCheckBox::toggled, this, [this](bool on){
        wrap_ = on; QSettings().setValue("mirror/wrap", on);
        if (on && spreadChk_->isChecked()) spreadChk_->setChecked(false);
    });
    connect(applyHost, &QPushButton::clicked, this, &MainWindow::connectHostFromField);
    connect(hostEdit_, &QLineEdit::returnPressed, this, &MainWindow::connectHostFromField);
    connect(autoMirrorChk_, &QCheckBox::toggled, this, [](bool on){ QSettings().setValue("opts/autoMirror", on); });
    connect(startMinChk_,   &QCheckBox::toggled, this, [](bool on){ QSettings().setValue("opts/startMin", on); });
    connect(autostartChk_,  &QCheckBox::toggled, this, [this](bool on){ setAutostart(on); QSettings().setValue("opts/autostart", on); });

    // --- tray -----------------------------------------------------------------
    tray_ = new QSystemTrayIcon(this);
    tray_->setIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));
    tray_->setToolTip(baseTitle_);
    auto* trayMenu = new QMenu(this);
    connect(trayMenu->addAction("Show wled-pc-rgb"), &QAction::triggered, this, &MainWindow::showAndRaise);
    trayMirror_ = trayMenu->addAction("Mirror WLED");
    trayMirror_->setCheckable(true);
    connect(trayMirror_, &QAction::toggled, this, &MainWindow::setMirroring);
    connect(trayMenu->addAction("Rescan devices"), &QAction::triggered, this, &MainWindow::refresh);
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
        wledColour_ = avg;
        const int b = bright_->value(), g = gain_->value(), fl = floor_->value();
        const bool off = !wledOn_;                         // WLED powered off
        const QColor idle = idleOn_ ? idleColor_ : QColor(0, 0, 0);   // what an "off" LED shows
        const QColor pc = off ? idle : withIdle(mapColor(avg, b, g, fl), idleOn_, idleColor_);

        // Cosmetics (swatches + title) don't need the full frame rate — throttle to ~10 Hz
        // so the GUI doesn't churn while devices update every frame.
        static QElapsedTimer cos;
        if (!cos.isValid() || cos.elapsed() > 100) {
            cos.restart();
            paintSwatch(swatchW_, off ? QColor(0, 0, 0) : avg);
            paintSwatch(swatchP_, pc);
            setWindowTitle(baseTitle_ + " · WLED " + avg.name());
        }
        if (!mirroring_) return;

        static QElapsedTimer reopen;                       // self-heal the mirror socket
        if (!mirror_.alive() && (!reopen.isValid() || reopen.elapsed() > 2000)) {
            reopen.restart(); QString e;
            if (mirror_.open(kHost, kPort, &e)) pushIncluded();
        }
        if (!mirror_.alive()) return;

        if (off) { mirror_.apply(idle); return; }          // WLED off → PC off (or idle colour)

        if (wrap_ || spread_) {
            QList<QColor> sc; sc.reserve(cols.size());
            for (const QColor& c : cols) sc.push_back(withIdle(mapColor(c, b, g, fl), idleOn_, idleColor_));
            if (wrap_) mirror_.applyWrapped(sc); else mirror_.applyBuckets(sc);
        } else {
            mirror_.apply(pc);
        }
    });
    ipc_->start(kIpcPort);

    refreshMirrorGate();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    QSettings().setValue("win/geometry", saveGeometry());
    if (tray_ && tray_->isVisible()) {
        static bool told = false;
        hide();
        if (!told) { tray_->showMessage("wled-pc-rgb", "Still running in the tray — right-click to quit.",
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
        QString e;
        if (!mirror_.open(kHost, kPort, &e)) { status_->setText("⚠  " + e); on = false; }
        else { mirroring_ = true; pushIncluded(); }
    } else if (!on && mirroring_) {
        mirror_.close(); mirroring_ = false; status_->setText("Mirror off.");
    }
    QSignalBlocker b1(mirBtn_), b2(trayMirror_);
    mirBtn_->setChecked(mirroring_);
    mirBtn_->setText(mirroring_ ? "■  Stop mirroring" : "▶  Mirror WLED");
    if (trayMirror_) trayMirror_->setChecked(mirroring_);
    QSettings().setValue("mirror/on", mirroring_);
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

void MainWindow::startOpenRGB() {
    { QTcpSocket probe; probe.connectToHost(kHost, kPort);
      if (probe.waitForConnected(300)) { probe.abort(); return; } }   // already running — use it
    const QStringList candidates = {
        QStringLiteral("C:/.software/OpenRGB/OpenRGB Windows 64-bit/OpenRGB.exe"),
        qEnvironmentVariable("ProgramFiles") + "/OpenRGB/OpenRGB.exe",
        qEnvironmentVariable("LOCALAPPDATA") + "/OpenRGB/OpenRGB.exe",
    };
    QString exe;
    for (const QString& c : candidates) if (QFile::exists(c)) { exe = c; break; }
    if (exe.isEmpty()) {
        setDot(dotO_, 1, "OpenRGB not found — install it or start it with --server.");
        status_->setText("OpenRGB not found — install it, or start it manually with --server.");
        return;
    }
    // NON-elevated on purpose: elevated OpenRGB probes the motherboard SMBus (DDR5), which
    // risks the RAM and hangs detection on this board. Non-elevated is safe + reliable.
    // (Trade-off: the GPU is detected but its RGB won't physically light without elevation.)
    QProcess::startDetached(exe, QStringList{ "--server", "--noautoconnect" });
    setDot(dotO_, 2, "Starting OpenRGB…");
    // USB-HID devices (Kraken, mouse) enumerate a few seconds after OpenRGB starts;
    // the initial scan can miss them. Re-scan a couple of times to catch late arrivals.
    QTimer::singleShot(6000,  this, &MainWindow::refresh);
    QTimer::singleShot(13000, this, &MainWindow::refresh);
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

void MainWindow::saveCheckedDevices() {
    QStringList names;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* it = tree_->topLevelItem(i);
        if ((it->flags() & Qt::ItemIsUserCheckable) && it->checkState(0) == Qt::Checked)
            names << it->text(0);
    }
    QSettings().setValue("mirror/devices", names);
}

void MainWindow::pushIncluded() {
    mirror_.setIncluded(gatherChecked());
    if (mirroring_) status_->setText(QString("Mirroring WLED onto %1 device(s).").arg(mirror_.deviceCount()));
}

void MainWindow::setDot(QLabel* dot, int level, const QString& hint) {
    if (!dot) return;
    static const char* col[] = { "#888", "#e33", "#e9a13b", "#2a8f5a" };
    dot->setStyleSheet(QString("color:%1; font-size:15px;").arg(col[qBound(0, level, 3)]));
    dot->setToolTip(hint);
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
        baseTitle_ = "wled-pc-rgb — connecting to OpenRGB…";
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
        baseTitle_ = "wled-pc-rgb — detecting devices…";
        setWindowTitle(baseTitle_);
        building_ = false;
        refreshMirrorGate();
        QTimer::singleShot(2000, this, &MainWindow::refresh);
        return;
    }
    zeroRetries_ = 0;
    openrgbReady_ = true;

    // Persisted per-device tick set (by name). Absent key => mirror all eligible by default.
    QSettings s;
    const bool haveSaved = s.contains("mirror/devices");
    const QStringList saved = s.value("mirror/devices").toStringList();

    int zoneTotal = 0, ledTotal = 0, mirrorable = 0;
    for (int di = 0; di < int(devices.size()); ++di) {
        const auto& d = devices[di];
        const bool isDram    = (d.type == 1);
        const bool isGpu     = (d.type == 2);
        const bool canMirror = (!isDram && !d.leds.empty());

        QString suffix;
        if (isDram) suffix = "   (RAM — excluded for safety)";
        else if (isGpu) suffix = "   (needs OpenRGB as admin to light)";
        auto* dItem = new QTreeWidgetItem(tree_, {d.name + suffix, "device"});
        dItem->setData(0, kDeviceIndexRole, di);
        if (canMirror) {
            dItem->setFlags(dItem->flags() | Qt::ItemIsUserCheckable);
            const bool checked = haveSaved ? saved.contains(d.name) : true;
            dItem->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
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
    baseTitle_ = QString("wled-pc-rgb — %1 devices").arg(devices.size());
    setWindowTitle(baseTitle_);
    building_ = false;
    if (mirroring_) pushIncluded();
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
    QString err;
    if (!OrgbClient::setDeviceMode(kHost, kPort, dev.toInt(), mode.toInt(), &err)) { status_->setText("⚠  " + err); return; }
    // Update the active marker in place — no tree rebuild, so the device stays expanded.
    if (QTreeWidgetItem* modesNode = item->parent()) {
        for (int i = 0; i < modesNode->childCount(); ++i)
            modesNode->child(i)->setText(1, modesNode->child(i) == item ? "● active" : QString());
        modesNode->setText(1, "active: " + item->text(0));
    }
    status_->setText(QString("Activated mode '%1'.").arg(item->text(0)));
    if (mirroring_) { QString e; mirror_.open(kHost, kPort, &e); pushIncluded(); }  // refresh mirror's cached mode
}

void MainWindow::maxZones() {
    const int target = zoneSpin_ ? zoneSpin_->value() : kDefaultZoneLeds;
    QString err;
    const int n = OrgbClient::resizeZones(kHost, kPort, target, /*onlyZero*/false, &err);
    if (n >= 0) { status_->setText(QString("Sized %1 motherboard zone(s) to %2 LEDs — rescanning…").arg(n).arg(target)); refresh(); }
    else status_->setText("⚠  " + err);
}
