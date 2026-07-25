// MainWindow — wled-pc-rgb: mirror a WLED instance onto this PC's RGB, live.
// Owns the setup readiness strip, the device list (per-device mirror toggle),
// the WLED-host field, the primary Mirror button, the live colour swatch, the
// tray icon, and all persisted settings.
#pragma once
#include <QMainWindow>
#include <QColor>
#include <QString>
#include <QList>
#include <QStringList>
#include <QElapsedTimer>
#include <QIcon>
#include "orgb_client.h"
#include "kraken_driver.h"
#include "gpu_driver.h"

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QSlider;
class QSpinBox;
class QLineEdit;
class QPushButton;
class QCheckBox;
class QRadioButton;
class QComboBox;
class QProcess;
class QSystemTrayIcon;
class QAction;
class IpcClient;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // The app icon, drawn in code (a ring of RGB dots) so no image asset needs shipping. Used for
    // the window, the taskbar and the tray. Static so main() can set it before the window exists.
    static QIcon appIcon();

public slots:
    void refresh();
    void setSelectedMode();
    void maxZones();
    void showAndRaise();           // tray / second-instance activation
    void setMirroring(bool on);    // start/stop the mirror

protected:
    void closeEvent(QCloseEvent*) override;   // hide to tray instead of quitting

private:
    void startOpenRGB();
    QString findOpenRGB();             // locate the OpenRGB exe
    void startBackend();
    void connectHostFromField();   // apply the WLED host field + restart backend
    QList<int> gatherChecked();
    void repopulateBlacklist();
    void calibrateBg();                // confirm, then start a short averaged capture
    void finishCalibration();          // store the captured value
    void refreshBgUi();                // repaint the calibration swatch/label/status
    void openAbout();                  // the About dialog (version, licence, links, credits)
    void maybeAutoMirror();
    void pushIncluded();
    bool krakenSelected() const;       // is the Kraken row present + ticked (so we HID-drive its ring)?
    void syncKrakenDriving();          // open/close the Kraken HID pipeline to match krakenSelected()
    QList<int> gpuRowIndices() const;  // device indices of every ticked GPU row
    QList<int> allGpuRowIndices() const;   // device indices of EVERY GPU row, ticked or not
    bool gpuSelected() const { return !gpuRowIndices().isEmpty(); }
    void syncGpuDriving();             // open/close the GPU NVAPI pipeline to match gpuSelected()
    void activateMode(QTreeWidgetItem*);
    void setDot(QLabel* dot, int level, const QString& hint);   // 0 grey 1 red 2 amber 3 green
    void refreshMirrorGate();
    void setAutostart(bool on);

    QTreeWidget* tree_ = nullptr;
    QLabel*  status_ = nullptr;
    QLabel*  mobo_   = nullptr;
    QLabel*  dotO_ = nullptr; QLabel* dotOtxt_ = nullptr;   // OpenRGB
    QLabel*  dotB_ = nullptr; QLabel* dotBtxt_ = nullptr;   // Backend
    QLabel*  dotW_ = nullptr; QLabel* dotWtxt_ = nullptr;   // WLED
    QLineEdit* hostEdit_ = nullptr;
    QLabel*  swatchW_ = nullptr; QLabel* swatchP_ = nullptr;
    QSlider* bright_  = nullptr;
    QPushButton* mirBtn_ = nullptr;
    // Colour-mapping mode as a 3-way radio (mutually exclusive by construction). These drive the
    // spread_/wrap_ bools the mirror logic reads; both false = one average colour on every device.
    QRadioButton* mapSame_   = nullptr;
    QRadioButton* mapSpread_ = nullptr;
    QRadioButton* mapWrap_   = nullptr;
    QCheckBox*   stripBgChk_ = nullptr;   // "Reactive only" — subtract the CALIBRATED background colour
    QPushButton* calBtn_     = nullptr;   // capture the colour showing right now as the background
    QLabel*      bgSwatch_   = nullptr;   // what got captured (so the user can see it)
    QLabel*      bgLabel_    = nullptr;
    QCheckBox*   gammaChk_   = nullptr;   // apply WLED's gamma to the PC output (matches the strip)
    QSpinBox*    zoneSpin_  = nullptr;
    QSpinBox*    originSpin_ = nullptr;   // ring bloom origins (symmetric points the pattern grows from)
    QComboBox*   blacklistCombo_ = nullptr;
    QCheckBox*   autoMirrorChk_ = nullptr;
    QCheckBox*   autostartChk_  = nullptr;
    QCheckBox*   startMinChk_   = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
    QAction* trayMirror_ = nullptr;

    IpcClient*  ipc_ = nullptr;
    QProcess*   backend_ = nullptr;
    OrgbMirror  mirror_;
    KrakenDriver kraken_;         // direct-HID pipeline for the NZXT Kraken Elite ring (SignalRGB 0x26 protocol)
    bool krakenDriving_ = false;  // true while we own the ring over HID this mirror session
    GpuDriver gpu_;               // direct-NVAPI pipeline for the GPU's own I2C bus (MSI Blackwell per-LED)
    bool gpuDriving_ = false;     // true while we own the GPU over NVAPI this mirror session
    bool  mirroring_ = false, spread_ = false, wrap_ = false, building_ = false, stopping_ = false;
    bool  openrgbReady_ = false, backendUp_ = false, wledReachable_ = false, wledOn_ = true;
    int   zeroRetries_ = 0, backendFails_ = 0, backendDelayMs_ = 1500;
    QColor wledColour_;           // the RAW WLED average (what the strip shows)
    QColor mirrorColour_;         // post-strip, pre-brightness average (what the PC mirrors)
    QColor bgCal_;                // the CALIBRATED background colour (persisted; invalid = not set)
    // Calibration capture: averaging over a short window beats a single frame, and the statistic is a
    // per-channel MAXIMUM on purpose. The removal is a one-sided clamp, so an estimate one count HIGH
    // costs almost nothing while one count LOW leaves a permanent floor — the error is asymmetric.
    bool   calibrating_ = false;
    int    calFrames_   = 0;
    QColor calMax_;
    QElapsedTimer calTimer_;
    QStringList blacklist_;       // device-name substrings hidden from the scan (session only; resets on restart)
    QString wledHost_ = "wled.local";
    QString baseTitle_ = "WLED PC RGB";
};
