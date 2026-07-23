// MainWindow — device inspector + colour/mode control + motherboard panel +
// live WLED mirroring (via the auto-launched Java backend over IPC).
#pragma once
#include <QMainWindow>
#include <QColor>
#include <QString>
#include "orgb_client.h"

class QTreeWidget;
class QLabel;
class QSlider;
class QProcess;
class IpcClient;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

public slots:
    void refresh();
    void setSelectedColor();   // colour (× PC brightness) → selected device
    void setSelectedMode();
    void setAllColor();        // colour (× PC brightness) → every device
    void setWledColor();       // colour → WLED (colour only; WLED brightness untouched)
    void maxZones();

private:
    QColor pickColour();       // raw colour from the dialog (PC brightness applied per use)
    void   startBackend();     // auto-launch + supervise the Java WLED backend

    QTreeWidget* tree_    = nullptr;
    QLabel*      status_  = nullptr;
    QLabel*      mobo_    = nullptr;
    QLabel*      wled_    = nullptr;
    QSlider*     bright_  = nullptr;   // PC-only brightness scaler
    IpcClient*   ipc_     = nullptr;
    QProcess*    backend_ = nullptr;
    OrgbMirror   mirror_;
    bool         mirroring_ = false;
    bool         spread_    = false;
    bool         stopping_  = false;
    QColor       wledColour_;
    QString      baseTitle_ = "wled-pc-rgb";
};
