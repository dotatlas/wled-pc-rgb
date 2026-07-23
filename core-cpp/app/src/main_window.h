// MainWindow — device inspector + colour/mode/brightness control + motherboard
// panel + live WLED status (via the Java backend over IPC).
#pragma once
#include <QMainWindow>
#include <QColor>
#include <QString>

class QTreeWidget;
class QLabel;
class QSlider;
class IpcClient;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

public slots:
    void refresh();
    void setSelectedColor();
    void setSelectedMode();
    void setAllColor();
    void setRoomColor();       // colour → WLED (via the Java backend)

private:
    QColor pickColour();
    QTreeWidget* tree_   = nullptr;
    QLabel*      status_ = nullptr;
    QLabel*      mobo_   = nullptr;
    QLabel*      wled_   = nullptr;
    QSlider*     bright_ = nullptr;
    IpcClient*   ipc_    = nullptr;
    QColor       room_;
    QString      baseTitle_ = "wled-pc-rgb";
};
