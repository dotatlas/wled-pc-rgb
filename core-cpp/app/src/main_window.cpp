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
#include <QSignalBlocker>
#include <QStringList>

namespace {
constexpr int kDeviceIndexRole = Qt::UserRole + 1;
constexpr int kModeIndexRole   = Qt::UserRole + 2;
constexpr auto kHost = "127.0.0.1";
constexpr quint16 kPort = 6742;
constexpr quint16 kIpcPort = 47900;
constexpr int kDefaultZoneLeds = 8;   // MSI JARGB headers are zones of 8 (user-adjustable)

// The one and only mirror colour transform: scale each channel by the single Brightness
// slider (0-100%). Black stays black (off) since 0 * anything = 0.
QColor scale(const QColor& c, int pct) {
    return QColor(c.red() * pct / 100, c.green() * pct / 100, c.blue() * pct / 100);
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
    pipelines_ = { &kraken_ };   // bespoke per-device drivers; add more here as devices need them
    // Default blacklist: any bespoke-driven device that is actually present. Probe each
    // pipeline once; if its device is here, hide it from the OpenRGB scan (the pipeline
    // drives it directly). Session only — resets on restart. Re-add it via Advanced.
    for (DevicePipeline* p : pipelines_) if (p->open()) { blacklist_ << p->match(); p->close(); }

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
    auto* tip = new QLabel("Each ticked device follows WLED live while mirroring is on. The app puts every "
                           "device in Direct mode automatically. The NZXT Kraken Elite ring is driven directly. "
                           "GPU RGB needs OpenRGB run as administrator.", central);
    tip->setWordWrap(true);
    tip->setStyleSheet("color: gray; font-size: 11px;");

    tree_ = new QTreeWidget(central);
    tree_->setHeaderLabels({"Mirror?  Device / Zone / Mode / LED", "Info"});
    tree_->setColumnWidth(0, 400);
    connect(tree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it, int col) {
        if (building_ || col != 0 || !it->data(0, kDeviceIndexRole).isValid()) return;
        if (!(it->flags() & Qt::ItemIsUserCheckable)) return;
        if (mirroring_) pushIncluded();          // in-session opt-out; devices default all-on each scan
        refreshMirrorGate();
    });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int) {
        if (it && it->data(0, kModeIndexRole).isValid()) activateMode(it);   // double-click a mode row to select it
    });

    // --- brightness (the single control for the whole mirror) -----------------
    auto* bRow = new QHBoxLayout;
    bright_ = new QSlider(Qt::Horizontal, central);
    bright_->setRange(0, 100);
    bright_->setValue(s.value("mirror/brightness", 100).toInt());
    auto* bVal = new QLabel(QString::number(bright_->value()) + "%", central);
    connect(bright_, &QSlider::valueChanged, this, [this, bVal](int v){
        bVal->setText(QString::number(v) + "%");
        QSettings().setValue("mirror/brightness", v);
        paintSwatch(swatchP_, scale(wledColour_, v));
    });
    bRow->addWidget(new QLabel("Brightness:", central));
    bRow->addWidget(bright_, 1);
    bRow->addWidget(bVal);

    // Paint the PC preview once now (slider built setValue-before-connect).
    paintSwatch(swatchP_, scale(wledColour_, bright_->value()));

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
    auto* av = new QVBoxLayout(adv);
    auto* ag = new QHBoxLayout;
    auto* bl = new QHBoxLayout;
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

    // Blacklist row: devices hidden from the scan (a bespoke driver owns them), with a way
    // to re-add one so OpenRGB handles it instead. Session only (resets on restart).
    auto* blLabel = new QLabel("Hidden (driven directly):", adv);
    blacklistCombo_ = new QComboBox(adv);
    blacklistCombo_->setMinimumWidth(220);
    auto* readd = new QPushButton("Re-add to scan", adv);
    readd->setToolTip("Show this device in the list again and let OpenRGB drive it (applies on the next Mirror start).");
    repopulateBlacklist();
    connect(readd, &QPushButton::clicked, this, [this]{
        if (!blacklistCombo_ || blacklistCombo_->count() == 0) return;
        const QString sel = blacklistCombo_->currentText();
        blacklist_.removeAll(sel);
        repopulateBlacklist();
        refresh();
        status_->setText("Re-added \"" + sel + "\" to the scan — start Mirror again for OpenRGB to drive it.");
    });
    bl->addWidget(blLabel); bl->addWidget(blacklistCombo_, 1); bl->addWidget(readd); bl->addStretch(1);

    av->addLayout(ag);
    av->addLayout(bl);

    connect(adv, &QGroupBox::toggled, this, [rescan, maxZ, setMod, readd, blLabel, this](bool on){
        rescan->setVisible(on); maxZ->setVisible(on); zoneSpin_->setVisible(on); setMod->setVisible(on);
        spreadChk_->setVisible(on); wrapChk_->setVisible(on);
        blLabel->setVisible(on); blacklistCombo_->setVisible(on); readd->setVisible(on);
    });
    rescan->setVisible(false); maxZ->setVisible(false); zoneSpin_->setVisible(false); setMod->setVisible(false);
    spreadChk_->setVisible(false); wrapChk_->setVisible(false);
    blLabel->setVisible(false); blacklistCombo_->setVisible(false); readd->setVisible(false);

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
        const int b = bright_->value();
        const bool off = !wledOn_;                         // WLED powered off → PC off
        const QColor pc = off ? QColor(0, 0, 0) : scale(avg, b);

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

        if (off) {                                         // WLED off → everything off
            mirror_.apply(QColor(0, 0, 0));
            for (DevicePipeline* p : pipelines_) if (p->isOpen()) p->apply(QColor(0, 0, 0));
            return;
        }

        if (wrap_ || spread_) {                            // positional modes need the per-LED strip
            QList<QColor> sc; sc.reserve(cols.size());
            for (const QColor& c : cols) sc.push_back(scale(c, b));
            if (wrap_) mirror_.applyWrapped(sc); else mirror_.applyBuckets(sc);
        } else {
            mirror_.apply(pc);
        }

        // The Kraken ring can't render a per-LED pattern reliably (per-LED Direct data
        // collapses on the device), but a single colour displays perfectly and fast. So the
        // ring always shows ONE colour — the average PC colour (pc) — updated every frame,
        // even in Spread/Wrap. This is the Kraken-specific pipeline.

        for (DevicePipeline* p : pipelines_) if (p->isOpen()) p->apply(pc);
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
        // OpenRGB ignores every blacklisted device. A bespoke pipeline drives its device
        // only if that device is blacklisted (hidden); otherwise the device is left to
        // OpenRGB (the fallback), so a device the pipeline can't own still lights.
        mirror_.setSkip(blacklist_);
        for (DevicePipeline* p : pipelines_) {
            if (blacklist_.contains(p->match(), Qt::CaseInsensitive)) p->open(); else p->close();
        }
        QString e;
        if (!mirror_.open(kHost, kPort, &e)) {
            status_->setText("⚠  " + e); on = false;
            for (DevicePipeline* p : pipelines_) p->close();
        } else {
            mirroring_ = true; pushIncluded();
        }
    } else if (!on && mirroring_) {
        for (DevicePipeline* p : pipelines_) { if (p->isOpen()) p->apply(QColor(0, 0, 0)); p->close(); }
        mirror_.close(); mirroring_ = false; status_->setText("Mirror off.");
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

void MainWindow::repopulateBlacklist() {
    if (!blacklistCombo_) return;
    blacklistCombo_->clear();
    blacklistCombo_->addItems(blacklist_);
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

    int zoneTotal = 0, ledTotal = 0, mirrorable = 0;
    for (int di = 0; di < int(devices.size()); ++di) {
        const auto& d = devices[di];
        bool blacked = false;                        // hidden from the scan (a bespoke pipeline owns it)
        for (const QString& b : blacklist_) if (d.name.contains(b, Qt::CaseInsensitive)) { blacked = true; break; }
        if (blacked) continue;

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
