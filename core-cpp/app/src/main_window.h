// MainWindow — WLED mirror control. Shows every device OpenRGB detects with a
// per-device checkbox for whether it's driven by the mirror. Motherboard panel
// + device inspector + a PC-only brightness scaler. The app's job is mirroring
// WLED onto the PC's RGB.
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
class QProcess;
class IpcClient;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

public slots:
    void refresh();
    void setSelectedMode();    // activate the selected mode row (e.g. Kraken ring -> Static)
    void maxZones();           // size motherboard ARGB zones so the fans light

private:
    void       startOpenRGB();     // auto-launch OpenRGB elevated (admin) with its SDK server
    void       startBackend();     // auto-launch + supervise the Java WLED backend
    QList<int> gatherChecked();    // device indices whose mirror checkbox is ticked
    void       pushIncluded();     // tell the mirror which devices to drive
    void       activateMode(QTreeWidgetItem* modeItem);   // set a mode in place (no tree rebuild)

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
    bool         building_  = false;   // suppress itemChanged while rebuilding the tree
    bool         stopping_  = false;
    QColor       wledColour_;
    QString      baseTitle_ = "wled-pc-rgb";
};
