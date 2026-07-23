// MainWindow — wled-pc-rgb: mirror a WLED instance onto this PC's RGB, live.
// Owns the setup readiness strip, the device list (per-device mirror toggle),
// the WLED-host field, the primary Mirror button, the live colour swatch, the
// tray icon, and all persisted settings.
#pragma once
#include <QMainWindow>
#include <QColor>
#include <QString>
#include <QList>
#include "orgb_client.h"

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QSlider;
class QLineEdit;
class QPushButton;
class QCheckBox;
class QProcess;
class QSystemTrayIcon;
class QAction;
class IpcClient;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

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
    void startBackend();
    void connectHostFromField();   // apply the WLED host field + restart backend
    QList<int> gatherChecked();
    void saveCheckedDevices();
    void maybeAutoMirror();
    void pushIncluded();
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
    QCheckBox*   spreadChk_ = nullptr;
    QCheckBox*   autoMirrorChk_ = nullptr;
    QCheckBox*   autostartChk_  = nullptr;
    QCheckBox*   startMinChk_   = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
    QAction* trayMirror_ = nullptr;

    IpcClient*  ipc_ = nullptr;
    QProcess*   backend_ = nullptr;
    OrgbMirror  mirror_;
    bool  mirroring_ = false, spread_ = false, building_ = false, stopping_ = false;
    bool  openrgbReady_ = false, backendUp_ = false, wledReachable_ = false, wledOn_ = true;
    int   zeroRetries_ = 0, backendFails_ = 0, backendDelayMs_ = 1500;
    QColor wledColour_;
    QString wledHost_ = "wled.local";
    QString baseTitle_ = "wled-pc-rgb";
};
